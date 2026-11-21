#include "Anatomy.h"

#include "AnatomicaBridge.h"
#include "Colossal.h"
#include "Settings.h"

#include <unordered_map>

#include <bit>

#include <mutex>

#include <format>

namespace OStrands
{
	std::string_view ActName(Act a_act)
	{
		switch (a_act) {
		case Act::kVaginal:
			return "vaginal";
		case Act::kAnal:
			return "anal";
		case Act::kFellatio:
			return "blowjob";
		case Act::kCunnilingus:
			return "cunnilingus";
		case Act::kAnilingus:
			return "rimming";
		case Act::kVaginalFingering:
			return "vaginal fingering";
		case Act::kAnalFingering:
			return "anal fingering";
		case Act::kTribbing:
			return "scissoring";
		case Act::kVaginalToy:
			return "vaginal toy";
		case Act::kAnalToy:
			return "anal toy";
		}
		return "?";
	}

	namespace Anatomy
	{
		namespace
		{
			using CacheClock = std::chrono::steady_clock;

			struct ShaftEntry
			{
				RE::NiPointer<RE::NiAVObject>              root;
				std::uint64_t                              equipment{ 0 };
				CacheClock::time_point                     checked{};
				CacheClock::time_point                     seen{};
				ShaftState                                 state;
				std::vector<RE::NiPointer<RE::NiAVObject>> bones;
				std::string                                rig;
				int                                        axis{ 1 };
				float                                      tipExtension{ 0.0f };
				float                                      radius{ 0.0f };
				bool                                       fromMesh{ false };
			};

			std::unordered_map<std::uint32_t, ShaftEntry> shaftCache;
			CacheClock::time_point                        lastPruned{};

			std::uint64_t EquipmentSignature(RE::Actor* a_actor)
			{
				const auto biped = a_actor->GetBiped(false);
				std::uint64_t signature = 14695981039346656037ull;
				const auto mix = [&](const void* a_pointer) {
					signature = (signature ^ reinterpret_cast<std::uintptr_t>(a_pointer)) * 1099511628211ull;
				};
				mix(biped.get());
				if (biped) {
					for (const auto& object : biped->objects) {
						mix(object.item);
						mix(object.addon);
						mix(object.partClone.get());
					}
				}
				return signature;
			}

			bool NameHas(const RE::NiAVObject* a_object, std::initializer_list<std::string_view> a_needles)
			{
				std::string name = a_object && a_object->name.c_str() ? a_object->name.c_str() : "";
				std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return std::ranges::any_of(a_needles, [&](std::string_view a_needle) { return name.find(a_needle) != std::string::npos; });
			}

			bool Culled(RE::NiAVObject* a_object, RE::NiAVObject* a_root)
			{
				for (auto* node = a_object; node && node != a_root; node = node->parent) {
					if (node->GetAppCulled()) {
						return true;
					}
				}
				return false;
			}

			bool BodyBone(const RE::NiAVObject* a_bone) { return NameHas(a_bone, { "pelv", "spine" }); }

			std::vector<RE::NiAVObject*> SkinChain(RE::NiSkinInstance* a_skin)
			{
				constexpr std::size_t        kGap = 2;
				std::vector<RE::NiAVObject*> best;
				float                        bestReach = -1.0f;
				const std::uint32_t          count = a_skin->skinData->bones;
				const auto                   skinned = [&](const RE::NiAVObject* a_bone) { return std::find(a_skin->bones, a_skin->bones + count, a_bone) != a_skin->bones + count; };
				for (std::uint32_t i = 0; i < count; ++i) {
					if (!a_skin->bones[i] || BodyBone(a_skin->bones[i])) {
						continue;
					}
					std::vector<RE::NiAVObject*> run{ a_skin->bones[i] };
					std::size_t                  gap = 0;
					for (auto* parent = a_skin->bones[i]->parent; parent && gap <= kGap; parent = parent->parent) {
						if (!skinned(parent)) {
							++gap;
							continue;
						}
						if (BodyBone(parent)) {
							break;
						}
						run.insert(run.begin(), parent);
						gap = 0;
					}
					const float reach = run.back()->world.translate.GetDistance(run.front()->world.translate);
					if (run.size() > best.size() || (run.size() == best.size() && reach > bestReach)) {
						best = std::move(run);
						bestReach = reach;
					}
				}
				return best.size() >= 2 ? best : std::vector<RE::NiAVObject*>{};
			}

			struct MeshShaft
			{
				std::string                  mesh;
				std::vector<RE::NiAVObject*> bones;
			};

			MeshShaft FindMeshShaft(RE::NiAVObject* a_root)
			{
				MeshShaft     best;
				std::uint32_t most = 0;
				RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
					auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
					if (!skin || !skin->skinData || !skin->bones || !skin->skinPartition || Culled(a_geometry, a_root) ||
						!NameHas(a_geometry, { "penis", "dick", "cock", "genital", "schlong" }) || NameHas(a_geometry, { "collision", "virtual" })) {
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					auto                chain = SkinChain(skin);
					const std::uint32_t vertices = skin->skinPartition->vertexCount;
					if (!chain.empty() && (chain.size() > best.bones.size() || (chain.size() == best.bones.size() && vertices > most))) {
						best = { a_geometry->name.c_str(), std::move(chain) };
						most = vertices;
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				return best;
			}

			int ForwardAxis(const std::vector<RE::NiAVObject*>& a_bones)
			{
				const auto&        tip = a_bones.back()->world;
				const RE::NiPoint3 local = tip.rotate.Transpose() * (tip.translate - a_bones[a_bones.size() - 2]->world.translate);
				const float        x = std::abs(local.x), y = std::abs(local.y), z = std::abs(local.z);
				return x >= y && x >= z ? 0 : y >= z ? 1 : 2;
			}

			void FindRig(RE::Actor* a_actor, RE::NiAVObject* a_root, ShaftEntry& a_entry)
			{
				const auto previous = a_entry.rig;
				a_entry.bones.clear();
				a_entry.rig.clear();
				a_entry.axis = 1;
				a_entry.tipExtension = 0.0f;
				a_entry.radius = 0.0f;
				a_entry.fromMesh = false;
				const auto collect = [&](auto&& a_names) {
					std::vector<RE::NiPointer<RE::NiAVObject>> bones;
					for (const auto& name : a_names) {
						auto* node = a_root->GetObjectByName(RE::BSFixedString(name));
						if (!node) {
							return false;
						}
						bones.emplace_back(node);
					}
					a_entry.bones = std::move(bones);
					return true;
				};
				auto        guard = Settings::Lock();
				const auto& settings = Settings::Get();
				for (const auto& rig : settings.rigs) {
					if (rig.bones.size() < 2 || !a_root->GetObjectByName(RE::BSFixedString(rig.bones.front())) || !collect(rig.bones)) {
						continue;
					}
					a_entry.rig = rig.name;
					a_entry.axis = rig.axis;
					a_entry.tipExtension = rig.tipExtension;
					a_entry.radius = rig.radius;
					if (settings.debugLog && rig.name != previous) {
						logger::info("{}: penis rig {}, {} bones from {} to {}", a_actor->GetDisplayFullName(), rig.name, rig.bones.size(), rig.bones.front(),
							rig.bones.back());
					}
					return;
				}
				if (!IsPerson(a_actor)) {
					if (auto shaft = FindMeshShaft(a_root); !shaft.bones.empty()) {
						for (auto* bone : shaft.bones) {
							a_entry.bones.emplace_back(bone);
						}
						a_entry.rig = "mesh";
						a_entry.fromMesh = true;
						a_entry.axis = ForwardAxis(shaft.bones);
						a_entry.tipExtension = 0.1f;
						if (settings.debugLog && a_entry.rig != previous) {
							logger::info("{}: no penis rig; read {} bones from the '{}' mesh, {} to {}", a_actor->GetDisplayFullName(), shaft.bones.size(),
								shaft.mesh, shaft.bones.front()->name.c_str(), shaft.bones.back()->name.c_str());
						}
						return;
					}
				}
				collect(Names::Get().shaft);
			}

			bool ScanSchlong(RE::NiAVObject* a_root, const std::vector<RE::NiPointer<RE::NiAVObject>>& a_bones)
			{
				if (a_bones.size() < 2) {
					return false;
				}
				std::vector<RE::NiAVObject*> shaft;
				for (auto i = a_bones.size() - std::min<std::size_t>(a_bones.size(), 3); i < a_bones.size(); ++i) {
					shaft.push_back(a_bones[i].get());
				}
				bool                                 found = false;
				RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
					auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
					if (!skin || !skin->bones || !skin->skinData) {
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					for (std::uint32_t i = 0; i < skin->skinData->bones; ++i) {
						if (skin->bones[i] && std::ranges::find(shaft, skin->bones[i]) != shaft.end()) {
							found = true;
							return RE::BSVisit::BSVisitControl::kStop;
						}
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				return found;
			}

			RE::NiPoint3 Lerp(const RE::NiPoint3& a, const RE::NiPoint3& b, float t) { return a + (b - a) * t; }

			RE::NiPoint3 Normalized(RE::NiPoint3 v, const RE::NiPoint3& fallback)
			{
				return v.Unitize() > 1e-5f ? v : fallback;
			}

			RE::NiPoint3 BodyUp(RE::NiAVObject* a_root, const RE::NiPoint3& a_inward)
			{
				static const RE::BSFixedString spine{ "NPC Spine [Spn0]" };
				const RE::NiPoint3             fallback = a_root->world.rotate * RE::NiPoint3{ 0.0f, 0.0f, 1.0f };
				auto*                          pelvis = a_root->GetObjectByName(Names::Get().pelvis);
				auto*                          chest = a_root->GetObjectByName(spine);
				RE::NiPoint3                   up = pelvis && chest ? chest->world.translate - pelvis->world.translate : fallback;
				up -= a_inward * up.Dot(a_inward);
				return Normalized(up, fallback);
			}

			void Shift(std::vector<Point>& a_points, RE::NiPoint3& a_center, const RE::NiPoint3& a_offset)
			{
				if (a_offset.SqrLength() < 1e-8f) {
					return;
				}
				a_center += a_offset;
				for (auto& point : a_points) {
					const auto& world = point.node->world;
					point.local += world.rotate.Transpose() * a_offset / std::max(world.scale, 1e-4f);
				}
			}

			void Align(RE::NiAVObject* a_root, std::vector<Point>& a_points, RE::NiPoint3& a_center, const RE::NiPoint3& a_inward, float a_out, float a_up)
			{
				Shift(a_points, a_center, (BodyUp(a_root, a_inward) * a_up - a_inward * a_out) * a_root->world.scale);
			}
		}

		const Names& Names::Get()
		{
			static const Names names;
			return names;
		}

		Opening OpeningOf(Act a_act)
		{
			switch (a_act) {
			case Act::kAnal:
			case Act::kAnilingus:
			case Act::kAnalFingering:
			case Act::kAnalToy:
				return Opening::kAnus;
			case Act::kFellatio:
				return Opening::kMouth;
			default:
				return Opening::kVagina;
			}
		}

		bool IsPenis(Act a_act) { return a_act == Act::kVaginal || a_act == Act::kAnal || a_act == Act::kFellatio; }
		bool IsFingers(Act a_act) { return a_act == Act::kVaginalFingering || a_act == Act::kAnalFingering; }
		bool IsOralGiver(Act a_act) { return a_act == Act::kCunnilingus || a_act == Act::kAnilingus; }
		bool IsToy(Act a_act) { return a_act == Act::kVaginalToy || a_act == Act::kAnalToy; }
		bool HasShaft(Act a_act) { return IsPenis(a_act) || IsFingers(a_act) || IsToy(a_act); }

		bool Collect(RE::NiAVObject* a_root, const std::vector<RE::BSFixedString>& a_names, std::vector<Point>& a_points, RE::NiPoint3& a_center)
		{
			a_points.clear();
			RE::NiPoint3 sum;
			for (const auto& name : a_names) {
				if (auto* node = a_root->GetObjectByName(name)) {
					a_points.push_back({ node, {} });
					sum += node->world.translate;
				}
			}
			if (a_points.empty()) {
				return false;
			}
			a_center = sum / static_cast<float>(a_points.size());
			return true;
		}

		// 0.8.0.9: live lips. The SMP Head lip bones stay where the closed mouth is when an expression opens it (the face
		// morphs move the skin, not the bones), so strings hung from them floated over the open mouth. Each lip bone gets the
		// face vertex nearest to it, and a node of ours under the head follows that vertex every frame (morphs, DeepFit's
		// pushes and all); the strings and drips hang from those nodes.
		namespace
		{
			constexpr std::uint32_t kFaceStride = 16;   // a face's dynamic positions: x, y, z, w

			float HalfToFloat(std::uint16_t a_value)
			{
				const std::uint32_t sign = static_cast<std::uint32_t>(a_value & 0x8000u) << 16;
				std::uint32_t       exponent = (a_value >> 10) & 0x1Fu;
				std::uint32_t       mantissa = a_value & 0x3FFu;
				if (exponent == 0) {
					if (mantissa == 0) {
						return std::bit_cast<float>(sign);
					}
					exponent = 113;
					while (!(mantissa & 0x400u)) {
						mantissa <<= 1;
						--exponent;
					}
					mantissa &= 0x3FFu;
				} else if (exponent == 31) {
					return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
				} else {
					exponent += 112;
				}
				return std::bit_cast<float>(sign | (exponent << 23) | (mantissa << 13));
			}

			struct LipTrack
			{
				RE::NiPointer<RE::NiAVObject>                   root;
				RE::NiPointer<RE::BSGeometry>                   face;
				RE::NiPointer<RE::NiAVObject>                   head;
				std::array<std::uint32_t, 4>                    vertex{};
				std::array<RE::NiPointer<RE::NiNode>, 4>        nodes;
				bool                                            ok{ false };
			};
			std::mutex            lipLock;
			std::vector<LipTrack> lipTracks;

			// where a face vertex is now: its dynamic (morphed) position, skinned with its own bones
			bool FaceVertexWorld(RE::BSGeometry* a_face, std::uint32_t a_vertex, RE::NiPoint3& a_out)
			{
				auto* shape = a_face ? a_face->AsDynamicTriShape() : nullptr;
				auto* skin = a_face ? a_face->GetGeometryRuntimeData().skinInstance.get() : nullptr;
				auto* partition = skin ? skin->skinPartition.get() : nullptr;
				if (!shape || !skin || !skin->skinData || !skin->bones || !partition || partition->numPartitions == 0 ||
					!partition->partitions[0].buffData || !partition->partitions[0].buffData->rawVertexData || a_vertex >= partition->vertexCount) {
					return false;
				}
				auto& runtime = shape->GetDynamicTrishapeRuntimeData();
				RE::NiPoint3 local;
				{
					RE::BSSpinLockGuard guard(runtime.lock);
					if (!runtime.dynamicData || runtime.dataSize < partition->vertexCount * kFaceStride) {
						return false;
					}
					std::array<float, 3> xyz{};
					std::memcpy(xyz.data(), static_cast<const std::uint8_t*>(runtime.dynamicData) + static_cast<std::size_t>(a_vertex) * kFaceStride, sizeof(xyz));
					local = { xyz[0], xyz[1], xyz[2] };
				}
				const auto&         first = partition->partitions[0];
				const std::uint32_t stride = static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(first.vertexDesc) & 0xF) * 4;
				const std::uint32_t skinning = first.vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);
				if (stride == 0 || !first.vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_SKINNED) || skinning + 12 > stride) {
					return false;
				}
				const std::uint8_t*          vertex = first.buffData->rawVertexData + static_cast<std::size_t>(a_vertex) * stride;
				std::array<std::uint16_t, 4> halves{};
				std::array<std::uint8_t, 4>  bones{};
				std::memcpy(halves.data(), vertex + skinning, sizeof(halves));
				std::memcpy(bones.data(), vertex + skinning + 8, sizeof(bones));
				const std::uint32_t count = skin->skinData->bones;
				RE::NiPoint3        sum;
				float               total = 0.0f;
				for (std::size_t j = 0; j < 4; ++j) {
					const float w = HalfToFloat(halves[j]);
					if (!(w > 0.0f) || bones[j] >= count || !skin->bones[bones[j]]) {
						continue;
					}
					const RE::NiTransform t = skin->bones[bones[j]]->world * skin->skinData->GetBoneDataSkinToBone(bones[j]);
					sum += (t * local) * w;
					total += w;
				}
				if (total <= 0.0f) {
					return false;
				}
				a_out = sum / total;
				return true;
			}

			RE::BSGeometry* FaceOf(RE::NiAVObject* a_root)
			{
				RE::BSGeometry* best = nullptr;
				std::uint32_t   most = 0;
				RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
					auto* shape = a_geometry->AsDynamicTriShape();
					auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
					auto* partition = skin ? skin->skinPartition.get() : nullptr;
					if (shape && partition && partition->vertexCount > most && shape->GetDynamicTrishapeRuntimeData().dynamicData) {
						best = a_geometry;
						most = partition->vertexCount;
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				return best;
			}

			void PlaceLips(LipTrack& a_track)
			{
				auto* head = a_track.head.get();
				if (!head) {
					return;
				}
				const RE::NiTransform toHead = head->world.Invert();
				for (std::size_t i = 0; i < 4; ++i) {
					RE::NiPoint3 at;
					auto*        node = a_track.nodes[i].get();
					if (node && FaceVertexWorld(a_track.face.get(), a_track.vertex[i], at)) {
						node->local.translate = toHead * at;
						node->world = head->world * node->local;
					}
				}
			}
		}

		// the four live lip nodes for this 3D (from its SMP Head lip bones), or all null
		std::array<RE::NiAVObject*, 4> LiveLips(RE::NiAVObject* a_root, RE::NiAVObject* a_head, const std::array<RE::NiAVObject*, 4>& a_bones)
		{
			std::array<RE::NiAVObject*, 4> out{};
			if (!a_root || !a_head || !a_bones[0]) {
				return out;
			}
			std::scoped_lock guard(lipLock);
			auto it = std::ranges::find_if(lipTracks, [&](const LipTrack& a_track) { return a_track.root.get() == a_root; });
			if (it != lipTracks.end() && (!it->ok || it->head.get() != a_head || !it->face || !it->face->parent)) {
				lipTracks.erase(it);
				it = lipTracks.end();
			}
			if (it == lipTracks.end()) {
				LipTrack track;
				track.root = RE::NiPointer<RE::NiAVObject>(a_root);
				track.head = RE::NiPointer<RE::NiAVObject>(a_head);
				auto* face = FaceOf(a_root);
				auto* skin = face ? face->GetGeometryRuntimeData().skinInstance.get() : nullptr;
				auto* partition = skin ? skin->skinPartition.get() : nullptr;
				auto* headNode = a_head->AsNode();
				if (face && partition && headNode) {
					track.face = RE::NiPointer<RE::BSGeometry>(face);
					std::vector<RE::NiPoint3> where(partition->vertexCount);
					std::vector<bool>         known(partition->vertexCount, false);
					for (std::uint32_t v = 0; v < partition->vertexCount; ++v) {
						known[v] = FaceVertexWorld(face, v, where[v]);
					}
					track.ok = true;
					for (std::size_t i = 0; i < 4 && track.ok; ++i) {
						const RE::NiPoint3 bone = a_bones[i]->world.translate;
						float              best = 1e9f;
						for (std::uint32_t v = 0; v < partition->vertexCount; ++v) {
							if (known[v]) {
								const float d = where[v].GetDistance(bone);
								if (d < best) {
									best = d;
									track.vertex[i] = v;
								}
							}
						}
						if (best > 2.0f * a_head->world.scale) {
							track.ok = false;   // no face vertex on that lip
							break;
						}
						auto node = RE::NiPointer<RE::NiNode>(RE::NiNode::Create(0));
						if (!node) {
							track.ok = false;
							break;
						}
						node->name = std::format("OStrandsLip{}", i);
						headNode->AttachChild(node.get(), true);
						track.nodes[i] = node;
					}
					if (track.ok) {
						PlaceLips(track);
					}
				}
				if (lipTracks.size() > 16) {
					lipTracks.erase(lipTracks.begin());
				}
				lipTracks.push_back(std::move(track));
				it = std::prev(lipTracks.end());
			}
			if (!it->ok) {
				return out;
			}
			for (std::size_t i = 0; i < 4; ++i) {
				out[i] = it->nodes[i].get();
			}
			return out;
		}

		void TickLips()
		{
			std::scoped_lock guard(lipLock);
			std::erase_if(lipTracks, [](const LipTrack& a_track) { return !a_track.root || a_track.root->GetRefCount() <= 1; });
			for (auto& track : lipTracks) {
				if (track.ok) {
					PlaceLips(track);
				}
			}
		}

		// 0.8.0.9: the SMP Head lip bones (upper centre, bottom centre, the two corners), or all null; FSMP may put a prefix
		// in front of the names, so they are matched by their ending. Looked up once per 3D.
		std::array<RE::NiAVObject*, 4> HeadLipBones(RE::NiAVObject* a_root, RE::NiAVObject* a_head)
		{
			struct Cached { std::array<RE::NiPointer<RE::NiAVObject>, 4> bones; int none{ 0 }; };   // none: calls left before looking again (a head can change)
			static std::unordered_map<RE::NiAVObject*, Cached> cache;
			std::array<RE::NiAVObject*, 4> out{};
			auto rooted = [&](RE::NiAVObject* a_node) {
				for (auto* n = a_node; n; n = n->parent) {
					if (n == a_root) {
						return true;
					}
				}
				return false;
			};
			if (auto it = cache.find(a_root); it != cache.end()) {
				if (it->second.none > 0) {
					--it->second.none;
					return out;
				}
				if (it->second.bones[0] && rooted(it->second.bones[0].get())) {
					for (std::size_t i = 0; i < 4; ++i) {
						out[i] = it->second.bones[i].get();
					}
					return out;
				}
				cache.erase(it);
			}
			if (cache.size() > 64) {
				cache.clear();
			}
			static constexpr std::string_view kEnds[4] = { "Upper Lip Center", "Bottom Lip Center", "Corner Lip Right", "Corner Lip Left" };
			std::array<RE::NiAVObject*, 4> found{};
			std::function<void(RE::NiAVObject*)> walk = [&](RE::NiAVObject* a_node) {
				if (!a_node) {
					return;
				}
				const std::string_view name = a_node->name.c_str() ? a_node->name.c_str() : "";
				for (std::size_t i = 0; i < 4; ++i) {
					if (!found[i] && name.ends_with(kEnds[i])) {
						found[i] = a_node;
					}
				}
				if (auto* node = a_node->AsNode()) {
					for (auto& child : node->GetChildren()) {
						walk(child.get());
					}
				}
			};
			walk(a_head ? a_head : a_root);
			if (!(found[0] && found[1] && found[2] && found[3]) && a_head) {
				found = {};
				walk(a_root);
			}
			Cached& c = cache[a_root];
			if (!(found[0] && found[1] && found[2] && found[3])) {
				c.none = 600;
				return out;
			}
			for (std::size_t i = 0; i < 4; ++i) {
				c.bones[i] = RE::NiPointer<RE::NiAVObject>(found[i]);
			}
			return found;
		}

		bool MouthSite(RE::NiAVObject* a_root, std::vector<Point>& a_points, RE::NiPoint3& a_center, RE::NiPoint3& a_forward)
		{
			const auto& names = Names::Get();
			const auto& settings = Settings::Get();
			auto*       head = a_root->GetObjectByName(names.head);
			auto*       magic = a_root->GetObjectByName(names.headMagic);
			if (!head || !magic) {
				return false;
			}
			const RE::NiPoint3 headPos = head->world.translate;
			const RE::NiPoint3 magicPos = magic->world.translate;
			a_forward = Normalized(magicPos - headPos, head->world.rotate * RE::NiPoint3{ 0.0f, 1.0f, 0.0f });
			a_center = Lerp(headPos, magicPos, settings.mouthBlend);
			const RE::NiPoint3    up = head->world.rotate * RE::NiPoint3{ 0.0f, 0.0f, 1.0f } * head->world.scale;
			const RE::NiPoint3    right = head->world.rotate * RE::NiPoint3{ 1.0f, 0.0f, 0.0f } * head->world.scale;
			a_center += a_forward * (settings.mouthOut * head->world.scale) + up * settings.mouthUp;
			const RE::NiTransform toHead = head->world.Invert();
			a_points.clear();
			float muzzle = 1.0f;   // 0.8.0.8: a beast form's mouth is wider
			bool  fangs = false;   // 0.8.0.8: a Vampire Lord's fangs (points 4 and 5)
			if (auto* ref = a_root->GetUserData(); ref && settings.beastPeople) {
				if (auto* actor = ref->As<RE::Actor>()) {
					const int form = Colossal::BeastForm(actor);
					if (form != 0) {
						muzzle = settings.beastMouth;
					}
					fangs = form == 3 && settings.lordFangs;
				}
			}
			// 0.8.0.9: a High Poly Head (SMP Head) has bones on its lips - the strings and drips hang from the real lips and
			// corners (and move with them when DeepFit opens the mouth), not from points worked out from the head bone
			if (const auto bones = HeadLipBones(a_root, head); bones[0]) {
				const auto live = LiveLips(a_root, head, bones);
				const auto& lips = live[0] ? live : bones;   // the lips as they are now (the bones where they are closed)
				for (auto* bone : lips) {
					a_points.push_back({ bone, {} });
				}
				a_center = (lips[0]->world.translate + lips[1]->world.translate) * 0.5f;
			} else
			for (const RE::NiPoint3& offset : { up * (settings.lipHeight * std::sqrt(muzzle)), up * (-settings.lipHeight * std::sqrt(muzzle)), right * (settings.lipWidth * muzzle), right * (-settings.lipWidth * muzzle) }) {
				a_points.push_back({ head, toHead * (a_center + offset) });
			}
			if (fangs) {   // just inside the corners, hanging a little below the upper lip
				for (const float side : { 0.45f, -0.45f }) {
					const RE::NiPoint3 fang = a_center + right * (settings.lipWidth * muzzle * side) + up * (settings.lipHeight * std::sqrt(muzzle) * 0.2f) + a_forward * (0.3f * head->world.scale);
					a_points.push_back({ head, toHead * fang });
				}
			}
			return true;
		}

		namespace
		{
			bool HasVagina(RE::NiAVObject* a_root)
			{
				const auto& names = Names::Get();
				return a_root->GetObjectByName(names.vaginaDeep) &&
				       std::ranges::any_of(names.vaginaRim, [&](const RE::BSFixedString& a_name) { return a_root->GetObjectByName(a_name) != nullptr; });
			}

			RE::Actor* ActorOf(RE::NiAVObject* a_root)
			{
				auto* ref = a_root ? a_root->GetUserData() : nullptr;
				return ref ? ref->As<RE::Actor>() : nullptr;
			}

			bool AnatomicaHole(RE::NiAVObject* a_root, std::vector<Point>& a_points, RE::NiPoint3& a_center, RE::NiPoint3& a_inward)
			{
				RE::NiPoint3 center;
				RE::NiPoint3 deep;
				if (!AnatomicaBridge::HoleFrame(ActorOf(a_root), center, deep)) {
					return false;
				}
				a_inward = deep - center;
				if (a_inward.Unitize() < 1e-3f) {
					return false;
				}
				RE::NiPoint3 boneCenter;
				if (Collect(a_root, Names::Get().anusRim, a_points, boneCenter)) {
					Shift(a_points, boneCenter, center - boneCenter);
				} else {
					auto* pelvis = a_root->GetObjectByName(Names::Get().pelvis);
					auto* holder = pelvis ? pelvis : a_root;
					const RE::NiPoint3    up = BodyUp(a_root, a_inward);
					const RE::NiPoint3    side = Normalized(up.Cross(a_inward), { 1.0f, 0.0f, 0.0f });
					const float           radius = 0.6f * a_root->world.scale;
					const RE::NiTransform toHolder = holder->world.Invert();
					a_points.clear();
					for (const RE::NiPoint3& offset : { up * radius, up * -radius, side * radius, side * -radius }) {
						a_points.push_back({ holder, toHolder * (center + offset) });
					}
				}
				a_center = center;
				return true;
			}
		}

		Opening EffectiveOpening(RE::NiAVObject* a_root, Opening a_opening)
		{
			if (a_opening == Opening::kVagina && a_root && Settings::Get().noVaginaUsesAnus && !HasVagina(a_root)) {
				return Opening::kAnus;
			}
			return a_opening;
		}

		bool IsCreature(RE::Actor* a_actor)
		{
			return a_actor && (!IsPerson(a_actor) || ShaftOf(a_actor).Creature());
		}

		const char* FindOpening(RE::NiAVObject* a_root, Opening a_opening, std::vector<Point>& a_points, RE::NiPoint3& a_center, RE::NiPoint3& a_inward)
		{
			a_opening = EffectiveOpening(a_root, a_opening);
			if (a_opening == Opening::kAnus && AnatomicaHole(a_root, a_points, a_center, a_inward)) {
				return nullptr;
			}
			if (a_opening == Opening::kMouth) {
				RE::NiPoint3 forward;
				if (!MouthSite(a_root, a_points, a_center, forward)) {
					return "receiver skeleton has no NPC Head / NPC Head MagicNode";
				}
				a_inward = -forward;
				return nullptr;
			}
			const auto& names = Names::Get();
			const bool  vagina = a_opening == Opening::kVagina;
			if (!Collect(a_root, vagina ? names.vaginaRim : names.anusRim, a_points, a_center)) {
				return "receiver skeleton has none of the rim bones";
			}
			auto* deep = a_root->GetObjectByName(vagina ? names.vaginaDeep : names.anusDeep);
			if (!deep) {
				return vagina ? "receiver skeleton has no VaginaDeep1" : "receiver skeleton has no NPC Anus Deep1";
			}
			a_inward = deep->world.translate - a_center;
			if (a_inward.Unitize() < 1e-3f) {
				return "deep bone sits on the rim";
			}
			const auto& settings = Settings::Get();
			Align(a_root, a_points, a_center, a_inward, vagina ? settings.vaginaOut : settings.anusOut, vagina ? settings.vaginaUp : settings.anusUp);
			return nullptr;
		}

		bool VulvaSite(RE::NiAVObject* a_root, std::vector<Point>& a_points, RE::NiPoint3& a_center)
		{
			const auto& names = Names::Get();
			if (!Collect(a_root, names.vaginaRim, a_points, a_center)) {
				return false;
			}
			RE::NiPoint3 inward;
			if (auto* deep = a_root->GetObjectByName(names.vaginaDeep)) {
				inward = deep->world.translate - a_center;
				if (inward.Unitize() < 1e-3f) {
					inward = {};
				}
			}
			const auto& settings = Settings::Get();
			Align(a_root, a_points, a_center, inward, settings.vaginaOut, settings.vaginaUp);
			return true;
		}

		bool Allowed(Act a_act)
		{
			const auto& settings = Settings::Get();
			switch (a_act) {
			case Act::kFellatio:
			case Act::kCunnilingus:
			case Act::kAnilingus:
				return settings.oral;
			case Act::kVaginalFingering:
			case Act::kAnalFingering:
				return settings.fingering;
			case Act::kTribbing:
				return settings.tribbing;
			case Act::kVaginalToy:
			case Act::kAnalToy:
				return settings.toys;
			default:
				return true;
			}
		}

		bool IsPerson(RE::Actor* a_actor)
		{
			static auto* const person = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
			const auto*        race = a_actor ? a_actor->GetRace() : nullptr;
			if (!race) {
				return false;
			}
			if (!person || race->HasKeyword(person)) {
				return true;
			}
			return std::ranges::any_of(race->skeletonModels, [](const RE::TESModel& a_model) {
				const char* path = a_model.GetModel();
				return path && _strnicmp(path, "actors\\character\\", 17) == 0;
			});
		}

		void UpdateShaftCache()
		{
			const auto now = CacheClock::now();
			if (now - lastPruned >= 1s) {
				std::erase_if(shaftCache, [&](const auto& a_entry) {
					return now - a_entry.second.seen >= 30s || !a_entry.second.root || !a_entry.second.root->parent;
				});
				lastPruned = now;
			}
		}

		void ResetShaftCache()
		{
			shaftCache.clear();
			lastPruned = {};
		}

		ShaftEntry* RefreshShaft(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return nullptr;
			}
			const auto handle = a_actor->GetHandle().native_handle();
			auto* root = a_actor->Get3D(false);
			if (!root) {
				shaftCache.erase(handle);
				return nullptr;
			}
			const auto now = CacheClock::now();
			const auto equipment = EquipmentSignature(a_actor);
			auto [it, inserted] = shaftCache.try_emplace(handle);
			auto& entry = it->second;
			entry.seen = now;
			if (inserted || entry.root.get() != root || entry.equipment != equipment || now - entry.checked >= 500ms) {
				entry.root.reset(root);
				entry.equipment = equipment;
				FindRig(a_actor, entry.root.get(), entry);
				const auto* base = a_actor->GetActorBase();
				const bool  female = base && base->GetSex() == RE::SEX::kFemale;
				if (!entry.rig.empty()) {
					entry.state.schlong = !female && ScanSchlong(entry.root.get(), entry.bones);
					entry.state.strapOn = false;
				} else {
					entry.state.schlong = ScanSchlong(entry.root.get(), entry.bones);
					entry.state.strapOn = female && IsStrapWearer(a_actor, WornModels(a_actor));
				}
				entry.checked = now;
			}
			return &entry;
		}

		ShaftState GetShaftState(RE::Actor* a_actor)
		{
			const auto* entry = RefreshShaft(a_actor);
			return entry ? entry->state : ShaftState{};
		}

		Shaft ShaftOf(RE::Actor* a_actor)
		{
			const auto* entry = RefreshShaft(a_actor);
			if (!entry || entry->bones.empty()) {
				return {};
			}
			Shaft shaft;
			for (const auto& bone : entry->bones) {
				shaft.bones.push_back(bone.get());
			}
			shaft.rig = entry->rig;
			shaft.axis = entry->axis;
			shaft.radius = entry->radius;
			shaft.fromMesh = entry->fromMesh;
			if (entry->rig.empty()) {
				auto guard = Settings::Lock();
				shaft.tipExtension = Settings::Get().tipExtension;
			} else {
				shaft.tipExtension = entry->tipExtension;
			}
			return shaft;
		}

		RE::NiPoint3 AxisVector(int a_axis)
		{
			return { a_axis == 0 ? 1.0f : 0.0f, a_axis == 1 ? 1.0f : 0.0f, a_axis == 2 ? 1.0f : 0.0f };
		}

		bool HasSchlong(RE::Actor* a_actor)
		{
			return GetShaftState(a_actor).schlong;
		}

		RE::TESObjectARMO* BodyArmor(RE::Actor* a_actor)
		{
			if (!a_actor || !a_actor->GetParentCell()) {
				return nullptr;
			}
			const auto& biped = a_actor->GetBiped(false);
			auto*       item = biped ? biped->objects[RE::BIPED_OBJECT::kBody].item : nullptr;
			auto*       armor = item ? item->As<RE::TESObjectARMO>() : nullptr;
			return armor && armor != a_actor->GetSkin() ? armor : nullptr;
		}

		std::vector<std::string> WornModels(RE::Actor* a_actor)
		{
			std::vector<std::string> paths;
			const auto&              biped = a_actor && a_actor->GetParentCell() ? a_actor->GetBiped(false) : RE::BSTSmartPointer<RE::BipedAnim>{};
			if (!biped) {
				return paths;
			}
			auto*                           skin = a_actor->GetSkin();
			std::vector<RE::TESObjectARMO*> seen;
			for (const auto& object : biped->objects) {
				auto* armor = object.item ? object.item->As<RE::TESObjectARMO>() : nullptr;
				if (!armor || armor == skin || std::ranges::find(seen, armor) != seen.end()) {
					continue;
				}
				seen.push_back(armor);
				for (auto* addon : armor->armorAddons) {
					if (!addon) {
						continue;
					}
					for (const auto& model : addon->bipedModels) {
						std::string path = model.GetModel() ? model.GetModel() : "";
						std::ranges::transform(path, path.begin(), [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
						if (!path.empty() && std::ranges::find(paths, path) == paths.end()) {
							paths.push_back(std::move(path));
						}
					}
				}
			}
			return paths;
		}

		bool IsStrapWearer(RE::Actor* a_actor, const std::vector<std::string>& a_models)
		{
			auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
			if (!base || base->GetSex() != RE::SEX::kFemale) {
				return false;
			}
			static constexpr std::array<std::string_view, 5> names{ "strapon", "strap-on", "strap_on", "strap on", "dildo" };
			return std::ranges::any_of(a_models, [](const std::string& path) {
				return std::ranges::any_of(names, [&](std::string_view name) { return path.find(name) != std::string::npos; });
			});
		}
	}
}
