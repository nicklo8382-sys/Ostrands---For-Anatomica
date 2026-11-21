#include "StrandManager.h"

#include "Revealing.h"
#include "Settings.h"
#include "Colossal.h"
#include "AnatomicaBridge.h"
#include "FluidManager.h"

namespace OStrands
{
	using namespace Anatomy;

	namespace
	{
		constexpr const char* kMeshPath = "OStrands\\strand.nif";
		constexpr const char* kGlintPath = "OStrands\\glint.nif";
		constexpr float       kPi = 3.14159265f;
		constexpr float       kInsideMargin = 0.6f;
		constexpr float       kDripSeed = 0.04f;
		constexpr int         kSubsteps = 2;
		constexpr int         kIterations = 6;
		constexpr float       kMaxBudget = 3.0f;
		constexpr float       kFadeIn = 0.12f;
		constexpr float       kSettle = 1.0f;
		constexpr float       kSwingRelax = 0.1f;
		constexpr float       kMinSwing = 0.6f;
		constexpr float       kStrokeNear = 0.1f;
		constexpr float       kStrokeNearMax = 0.6f;
		constexpr float       kStrokeGap = 0.1f;
		constexpr float       kStrokeGapMax = 0.6f;
		constexpr float       kContactNear = 0.15f;
		constexpr float       kContactNearMax = 0.8f;
		constexpr float       kContactGap = 0.1f;
		constexpr float       kContactGapMax = 0.5f;
		constexpr float       kEndClearance = 0.5f;
		constexpr float       kSkinMargin = 0.15f;
		constexpr float       kSkinFriction = 0.3f;
		constexpr float       kWrapClearance = 4.0f;

		using Point = StrandManager::Point;

		Pair SelfPair(RE::ActorHandle a_actor)
		{
			return { a_actor, a_actor, Act::kVaginal };
		}

		struct MeshNames
		{
			std::array<RE::BSFixedString, StrandManager::kBones> strandBones{ "Strand00", "Strand01", "Strand02",
				"Strand03", "Strand04", "Strand05", "Strand06", "Strand07", "Strand08", "Strand09" };
			RE::BSFixedString strandShape{ "Strand" };
			RE::BSFixedString glintBone{ "Glint" };
			RE::BSFixedString glintShape{ "GlintShape" };

			static const MeshNames& Get()
			{
				static const MeshNames names;
				return names;
			}
		};

		RE::NiPoint3 Lerp(const RE::NiPoint3& a, const RE::NiPoint3& b, float t) { return a + (b - a) * t; }

		float Smooth(float t)
		{
			t = std::clamp(t, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		RE::NiPoint3 Normalized(RE::NiPoint3 v, const RE::NiPoint3& fallback)
		{
			return v.Unitize() > 1e-5f ? v : fallback;
		}

		RE::NiMatrix3 Rotation(const GeometryMath::Frame& a_frame)
		{
			const auto& [x, y, z] = a_frame;
			RE::NiMatrix3 m;
			m.entry[0][0] = x.x; m.entry[1][0] = x.y; m.entry[2][0] = x.z;
			m.entry[0][1] = y.x; m.entry[1][1] = y.y; m.entry[2][1] = y.z;
			m.entry[0][2] = z.x; m.entry[1][2] = z.y; m.entry[2][2] = z.z;
			return m;
		}

		float ShaftRadiusFor(const std::vector<std::string>& a_models, float a_rig)
		{
			const auto& settings = Settings::Get();
			for (const auto& [key, radius] : settings.shaftRadiusOverrides) {
				if (std::ranges::any_of(a_models, [&](const std::string& path) { return path.find(key) != std::string::npos; })) {
					return radius;
				}
			}
			return a_rig > 0.0f ? a_rig : settings.shaftRadius;
		}

		Point LowPoint(const std::vector<Point>& a_points, std::mt19937& a_rng)
		{
			std::vector<const Point*> sorted;
			for (const auto& point : a_points) {
				sorted.push_back(&point);
			}
			std::ranges::sort(sorted, {}, [](const Point* p) { return p->World().z; });
			if (sorted.size() < 2) {
				return *sorted.front();
			}
			const float        t = std::uniform_real_distribution<float>(0.3f, 0.7f)(a_rng);
			const RE::NiPoint3 between = sorted[0]->World() * (1.0f - t) + sorted[1]->World() * t;
			return { sorted[0]->node, sorted[0]->node->world.Invert() * between };
		}

		template <class T>
		const T& Pick(const std::vector<T>& a_items, std::mt19937& a_rng)
		{
			return a_items[std::uniform_int_distribution<std::size_t>(0, a_items.size() - 1)(a_rng)];
		}
	}

	StrandManager::Crossing StrandManager::Swing::Step(float a_value, float a_delta, const Bands& a_bands, bool a_armed)
	{
		if (!a_armed) {
			return Crossing::kNone;
		}
		if (!primed) {
			low = high = a_value;
			primed = true;
			return Crossing::kNone;
		}
		const float relax = Range() * kSwingRelax * a_delta;
		low = std::min(low + relax, a_value);
		high = std::max(high - relax, a_value);
		const float range = Range();
		if (range < kMinSwing) {
			return Crossing::kNone;
		}
		const float nearAt = low + std::min(range * a_bands.nearFrac, a_bands.nearMax);
		const float leaveAt = nearAt + std::min(range * a_bands.gapFrac, a_bands.gapMax);
		if (!engaged && a_value < nearAt) {
			engaged = true;
			return Crossing::kArrived;
		}
		if (engaged && a_value > leaveAt) {
			engaged = false;
			return Crossing::kLeft;
		}
		return Crossing::kNone;
	}

	StrandManager& StrandManager::Get()
	{
		static StrandManager manager;
		return manager;
	}

	void StrandManager::SetPairs(std::int32_t a_thread, std::vector<Pair> a_pairs)
	{
		std::scoped_lock guard(_lock);
		if (a_pairs.empty()) {
			_threads.erase(a_thread);
		} else {
			_threads[a_thread] = std::move(a_pairs);
		}
		_dirty = true;
	}

	void StrandManager::ClearThread(std::int32_t a_thread)
	{
		SetPairs(a_thread, {});
	}

	void StrandManager::SetAroused(std::vector<Aroused> a_aroused)
	{
		std::vector<Excited> next;
		next.reserve(a_aroused.size());
		for (const auto& who : a_aroused) {
			const auto old = std::ranges::find_if(_aroused, [&](const Excited& a_entry) { return a_entry.who.actor == who.actor; });
			next.push_back(old != _aroused.end() ? Excited{ who, old->vulva, old->tip } : Excited{ who });
		}
		_aroused = std::move(next);
	}

	void StrandManager::Recalibrate()
	{
		std::scoped_lock guard(_lock);
		_dirty = true;
	}

	void StrandManager::Reset()
	{
		{
			std::scoped_lock guard(_lock);
			_threads.clear();
			_dirty = false;
		}
		for (auto& strand : _strands) {
			Retire(strand);
		}
		_strands.clear();
		_pairs.clear();
		_lingering.clear();
		_aroused.clear();
		_kisses.clear();
		_tongues.clear();
		for (auto& glint : _glints) {
			RetireGlint(glint);
		}
		_glints.clear();
	}

	bool StrandManager::EnsureMesh()
	{
		if (_prototype) {
			return true;
		}
		if (_meshFailed) {
			return false;
		}
		RE::NiPointer<RE::NiNode>         model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		const auto error = RE::BSModelDB::Demand(kMeshPath, model, args);
		if (error != RE::BSResource::ErrorCode::kNone || !model) {
			logger::error("could not load meshes\\{} (error {}); strands disabled", kMeshPath, std::to_underlying(error));
			_meshFailed = true;
			return false;
		}
		_prototype = model;
		logger::info("loaded meshes\\{}", kMeshPath);
		return true;
	}

	bool StrandManager::EnsureGlintMesh()
	{
		if (_glintPrototype) {
			return true;
		}
		if (_glintMeshFailed) {
			return false;
		}
		RE::NiPointer<RE::NiNode>         model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		const auto error = RE::BSModelDB::Demand(kGlintPath, model, args);
		if (error != RE::BSResource::ErrorCode::kNone || !model) {
			logger::error("could not load meshes\\{} (error {}); glints disabled", kGlintPath, std::to_underlying(error));
			_glintMeshFailed = true;
			return false;
		}
		_glintPrototype = model;
		logger::info("loaded meshes\\{}", kGlintPath);
		return true;
	}

	StrandManager::Glint* StrandManager::AcquireGlint()
	{
		const auto& settings = Settings::Get();
		if (std::ranges::count_if(_glints, [](const Glint& g) { return g.active; }) >= settings.glintMax) {
			return nullptr;
		}
		auto it = std::ranges::find_if(_glints, [](const Glint& g) { return !g.active; });
		if (it == _glints.end()) {
			_glints.emplace_back();
			it = std::prev(_glints.end());
		}
		Glint& glint = *it;
		if (glint.root) {
			return &glint;
		}
		if (!_glintPrototype) {
			return nullptr;
		}
		auto* clone = _glintPrototype->Clone();
		auto* node = clone ? clone->AsNode() : nullptr;
		if (!node) {
			logger::error("cloning the glint mesh failed");
			return nullptr;
		}
		glint.root.reset(node);
		const auto& names = MeshNames::Get();
		glint.bone = node->GetObjectByName(names.glintBone);
		if (!glint.bone) {
			logger::error("glint mesh has no Glint bone; is glint.nif from this version?");
			_glintMeshFailed = true;
			_glintPrototype.reset();
			glint.root.reset();
			return nullptr;
		}
		node->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		glint.brightness = -1.0f;
		if (auto* shape = node->GetObjectByName(names.glintShape)) {
			shape->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
			if (auto* geometry = shape->AsGeometry()) {
				glint.shader = geometry->GetGeometryRuntimeData().shaderProperty;
			}
		}
		return &glint;
	}

	void StrandManager::SpawnGlints(float a_delta)
	{
		const auto& settings = Settings::Get();
		if (!settings.glints || settings.glintRate <= 0.0f || !EnsureGlintMesh()) {
			return;
		}
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const float                           chance = settings.glintRate * a_delta;
		for (std::size_t i = 0; i < _strands.size(); ++i) {
			const Strand& strand = _strands[i];
			if (!strand.active || strand.breakAt >= 0 || strand.age < kFadeIn || !strand.attachedTo || unit(_rng) >= chance) {
				continue;
			}
			Glint* glint = AcquireGlint();
			if (!glint) {
				return;
			}
			glint->strand = i;
			glint->serial = strand.serial;
			glint->segment = std::uniform_int_distribution<int>(0, static_cast<int>(kBones) - 2)(_rng);
			glint->along = unit(_rng);
			glint->age = 0.0f;
			glint->life = settings.glintLife * (0.6f + 0.8f * unit(_rng));
			glint->size = 0.6f + 0.8f * unit(_rng);
			glint->angle = unit(_rng) * 2.0f * kPi;
			glint->spin = (unit(_rng) - 0.5f) * 2.0f;
			glint->root->local = RE::NiTransform{};
			strand.attachedTo->AsNode()->AttachChild(glint->root.get(), true);
			glint->attachedTo = strand.attachedTo;
			glint->active = true;
		}
	}

	void StrandManager::UpdateGlints(float a_delta)
	{
		const auto& settings = Settings::Get();
		auto*       camera = RE::PlayerCamera::GetSingleton();
		const bool  ready = settings.glints && camera && camera->cameraRoot;
		const auto  eye = ready ? camera->cameraRoot->world.translate : RE::NiPoint3{};
		for (auto& glint : _glints) {
			if (!glint.active) {
				continue;
			}
			glint.age += a_delta;
			const bool valid = glint.strand < _strands.size() && _strands[glint.strand].active &&
			                   _strands[glint.strand].serial == glint.serial && _strands[glint.strand].breakAt < 0;
			if (!ready || !valid || glint.age >= glint.life || glint.root->parent != glint.attachedTo) {
				RetireGlint(glint);
				continue;
			}
			const Strand& strand = _strands[glint.strand];
			RE::NiPoint3  point = Lerp(strand.pos[glint.segment], strand.pos[glint.segment + 1], glint.along);
			const auto    toEye = Normalized(eye - point, { 0.0f, 0.0f, 1.0f });
			point += toEye * (strand.radius * 1.2f);

			const float x = glint.age / glint.life;
			const float envelope = x < 0.25f ? Smooth(x / 0.25f) : 1.0f - Smooth((x - 0.25f) / 0.75f);
			glint.angle += glint.spin * a_delta;
			const auto  side = Normalized(toEye.Cross({ 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f });
			const auto  up = side.Cross(toEye);
			const float c = std::cos(glint.angle);
			const float s = std::sin(glint.angle);

			RE::NiTransform world;
			world.rotate = Rotation({ side * c + up * s, toEye, up * c - side * s });
			world.translate = point;
			world.scale = std::max(glint.size * settings.glintSize * envelope, 0.0005f);
			glint.bone->local = glint.attachedTo->world.Invert() * world;

			if (glint.shader && glint.brightness != settings.glintBrightness) {
				if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(glint.shader.get())) {
					lighting->emissiveMult = settings.glintBrightness;
				}
				glint.brightness = settings.glintBrightness;
			}
			RE::NiUpdateData update{};
			glint.root->Update(update);
		}
	}

	void StrandManager::RetireGlint(Glint& a_glint)
	{
		if (a_glint.root && a_glint.root->parent) {
			a_glint.root->parent->DetachChild2(a_glint.root.get());
		}
		a_glint.active = false;
		a_glint.attachedTo = nullptr;
	}

	StrandManager::Strand* StrandManager::AcquireStrand()
	{
		if (CountActive() >= Settings::Get().maxTotal) {
			return nullptr;
		}
		auto it = std::ranges::find_if(_strands, [](const Strand& s) { return !s.active; });
		if (it == _strands.end()) {
			_strands.emplace_back();
			it = std::prev(_strands.end());
		}
		Strand& strand = *it;
		strand.frames = {};
		strand.previousStep = 0.0f;
		strand.serial = ++_serial;
		strand.tint.clear();
		if (strand.root) {
			return &strand;
		}
		if (!_prototype) {
			return nullptr;
		}

		auto* clone = _prototype->Clone();
		auto* node = clone ? clone->AsNode() : nullptr;
		if (!node) {
			logger::error("cloning the strand mesh failed");
			return nullptr;
		}
		strand.root.reset(node);
		const auto& names = MeshNames::Get();
		for (std::size_t i = 0; i < kBones; ++i) {
			strand.bones[i] = node->GetObjectByName(names.strandBones[i]);
			if (!strand.bones[i]) {
				logger::error("strand mesh has no {}; is strand.nif from this version?", names.strandBones[i].c_str());
				_meshFailed = true;
				_prototype.reset();
				strand.root.reset();
				return nullptr;
			}
		}
		node->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		strand.shader.reset();
		strand.opacity = -1.0f;
		strand.shine = -1.0f;
		strand.gloss = -1.0f;
		if (auto* shape = node->GetObjectByName(names.strandShape)) {
			shape->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
			if (auto* geometry = shape->AsGeometry()) {
				strand.shader = geometry->GetGeometryRuntimeData().shaderProperty;
			}
		}
		return &strand;
	}

	void StrandManager::ApplyPendingPairs()
	{
		std::vector<Pair> wanted;
		{
			std::scoped_lock guard(_lock);
			if (!_dirty) {
				return;
			}
			_dirty = false;
			for (const auto& [thread, pairs] : _threads) {
				wanted.insert(wanted.end(), pairs.begin(), pairs.end());
			}
		}
		for (const auto& old : _pairs) {
			if (std::ranges::find(wanted, old.pair) == wanted.end()) {
				BeginLinger(old);
			}
		}
		std::erase_if(_lingering, [&](const Linger& a_linger) {
			return std::ranges::any_of(wanted, [&](const Pair& a_pair) {
				return a_pair.giver == a_linger.actor || a_pair.receiver == a_linger.actor;
			});
		});
		std::vector<PairState> next;
		for (const auto& pair : wanted) {
			auto it = std::ranges::find_if(_pairs, [&](const PairState& s) { return s.pair == pair; });
			next.push_back(it != _pairs.end() ? *it : PairState{ .pair = pair });
			next.back().stroke = {};
			next.back().contact = {};
			next.back().bridgePending = false;
			next.back().contactPending = false;
			next.back().settle = kSettle;
		}
		_pairs = std::move(next);
		if (Settings::Get().debugLog) {
			logger::info("tracking {} pair(s)", _pairs.size());
		}
	}

	const char* StrandManager::Resolve(const Pair& a_pair, Anchors& a_out)
	{
		const auto giver = a_pair.giver.get();
		const auto receiver = a_pair.receiver.get();
		if (!giver || !receiver) {
			return "actor no longer exists";
		}
		auto* giver3D = giver->Get3D(false);
		auto* receiver3D = receiver->Get3D(false);
		if (!giver3D || !receiver3D) {
			return "actor has no 3D loaded";
		}
		const auto& names = Names::Get();
		const auto& settings = Settings::Get();
		a_out.receiverRoot = receiver3D;
		a_out.giverRoot = giver3D;

		const Opening opening = OpeningOf(a_pair.act);
		if (const char* failure = FindOpening(receiver3D, opening, a_out.opening, a_out.center, a_out.inward)) {
			return failure;
		}
		a_out.vulva.clear();
		if (opening == Opening::kVagina) {
			a_out.vulva = a_out.opening;
		} else if (opening == Opening::kAnus) {
			RE::NiPoint3 ignored;
			VulvaSite(receiver3D, a_out.vulva, ignored);
		}

		a_out.chain.clear();
		a_out.crotch.clear();
		a_out.giverSite.clear();
		a_out.toy = nullptr;
		auto* pelvis = giver3D->GetObjectByName(names.pelvis);
		a_out.chainScale = std::max(pelvis ? pelvis->world.scale : giver3D->world.scale, 0.01f);

		if (IsPenis(a_pair.act)) {
			const auto shaft = ShaftOf(giver.get());
			if (shaft.bones.empty()) {
				return "giver skeleton has no NPC Genitals01-06 chain or [CreatureRigs] chain";
			}
			if (shaft.Creature()) {
				a_out.crotch.push_back(shaft.bones.front());
			} else if (auto* base = giver3D->GetObjectByName(names.genitalsBase)) {
				a_out.chain.push_back(base);
				a_out.crotch.push_back(base);
				if (auto* scrotum = giver3D->GetObjectByName(names.scrotum)) {
					a_out.crotch.push_back(scrotum);
				}
			}
			a_out.chain.insert(a_out.chain.end(), shaft.bones.begin(), shaft.bones.end());
			const auto tip = Body::TipOf(giver.get());
			if (!tip) {
				return "giver skeleton has no NPC Genitals01-06 chain or [CreatureRigs] chain";
			}
			a_out.tip = tip->point;
			VulvaSite(giver3D, a_out.giverSite, a_out.giverCenter);
		} else if (IsFingers(a_pair.act)) {
			std::size_t hand = names.hands.size();
			float       nearest = std::numeric_limits<float>::max();
			for (std::size_t h = 0; h < names.hands.size(); ++h) {
				if (auto* tip = giver3D->GetObjectByName(names.hands[h].back())) {
					const float distance = tip->world.translate.GetDistance(a_out.center);
					if (distance < nearest) {
						nearest = distance;
						hand = h;
					}
				}
			}
			if (hand == names.hands.size()) {
				return "giver skeleton has no finger bones";
			}
			for (const auto& name : names.hands[hand]) {
				auto* node = giver3D->GetObjectByName(name);
				if (!node) {
					return "giver skeleton has no finger bones";
				}
				a_out.chain.push_back(node);
			}
			auto fingertip = [&](RE::NiAVObject* a_last, RE::NiAVObject* a_before) {
				const RE::NiPoint3 last = a_last->world.translate;
				return last + Normalized(last - a_before->world.translate, a_out.inward) * (settings.fingerTipExtension * a_last->world.scale);
			};
			auto* index = a_out.chain.back();
			a_out.tip = fingertip(index, a_out.chain[a_out.chain.size() - 2]);
			a_out.giverSite.push_back({ index, index->world.Invert() * a_out.tip });
			RE::NiPoint3 sum = a_out.tip;
			auto*        middleBase = giver3D->GetObjectByName(names.middleFingers[hand][0]);
			auto*        middleTip = giver3D->GetObjectByName(names.middleFingers[hand][1]);
			if (middleBase && middleTip) {
				const RE::NiPoint3 tip = fingertip(middleTip, middleBase);
				a_out.giverSite.push_back({ middleTip, middleTip->world.Invert() * tip });
				sum += tip;
			}
			a_out.giverCenter = sum / static_cast<float>(a_out.giverSite.size());
		} else if (IsToy(a_pair.act)) {
			float nearest = std::numeric_limits<float>::max();
			for (auto* root : { giver3D, receiver3D }) {
				for (const auto& name : names.animObjects) {
					auto* slot = root->GetObjectByName(name);
					auto* node = slot ? slot->AsNode() : nullptr;
					if (!node) {
						continue;
					}
					for (const auto& child : node->GetChildren()) {
						if (!child || child->worldBound.radius <= 0.0f || child->worldBound.radius > settings.toyMaxRadius) {
							continue;
						}
						const float distance = child->worldBound.center.GetDistance(a_out.center) - child->worldBound.radius;
						if (distance < nearest) {
							nearest = distance;
							a_out.toy = child.get();
						}
					}
				}
			}
			if (!a_out.toy) {
				return "no toy attached to either actor";
			}
			a_out.tip = a_out.toy->worldBound.center;
			a_out.toyRadius = a_out.toy->worldBound.radius;
			a_out.chainScale = std::max(a_out.toy->world.scale, 0.01f);
		} else if (a_pair.act == Act::kTribbing) {
			if (!VulvaSite(giver3D, a_out.giverSite, a_out.giverCenter)) {
				return "giver skeleton has none of the vulva bones";
			}
		} else {
			RE::NiPoint3 forward;
			if (!MouthSite(giver3D, a_out.giverSite, a_out.giverCenter, forward)) {
				return "giver skeleton has no NPC Head / NPC Head MagicNode";
			}
		}
		return nullptr;
	}

	void StrandManager::Check(PairState& a_state)
	{
		const auto& settings = Settings::Get();
		const Act   act = a_state.pair.act;
		std::string rig;
		a_state.checked = true;
		if (IsFingers(act)) {
			a_state.shaftRadius = settings.fingerRadius;
		} else if (IsToy(act)) {
			a_state.shaftRadius = settings.toyRadius;
		} else if (IsPenis(act)) {
			const auto giver = a_state.pair.giver.get();
			const auto models = WornModels(giver.get());
			const auto shaft = ShaftOf(giver.get());
			a_state.strapWearer = IsStrapWearer(giver.get(), models);
			float      radius = shaft.radius;
			if (radius <= 0.0f && shaft.Creature()) {
				if (const auto measured = Body::ShaftBindRadiusOf(giver.get())) {
					radius = *measured * a_state.anchors.chainScale;
				}
			}
			a_state.shaftRadius = ShaftRadiusFor(models, radius);
			rig = shaft.rig;
		}
		if (settings.debugLog) {
			logger::info("{} ready: {} opening point(s){}{}{}", ActName(act), a_state.anchors.opening.size(),
				a_state.strapWearer ? ", giver wears a strap-on" : "", rig.empty() ? std::string{} : std::format(", {} penis", rig),
				HasShaft(act) ? std::format(", shaft radius {}", a_state.shaftRadius) : std::string{});
		}
	}

	namespace
	{
		// 0.8.0.8: OStim's excitement (0-100) from its OStimExcitementFaction rank; -1 = not known
		float Excitement(RE::Actor* a_actor)
		{
			static RE::TESFaction* faction = nullptr;
			static bool            looked = false;
			if (!looked) {
				looked = true;
				if (auto* dh = RE::TESDataHandler::GetSingleton()) {
					faction = dh->LookupForm<RE::TESFaction>(0xD93, "OStim.esp");
				}
			}
			if (!faction || !a_actor) {
				return -1.0f;
			}
			const auto rank = a_actor->GetFactionRank(faction, a_actor->IsPlayerRef());
			return rank < 0 ? -1.0f : static_cast<float>(rank);
		}
	}

	void StrandManager::TickPrecum(PairState& a_state, float a_delta)
	{
		const auto& settings = Settings::Get();
		if (!settings.precum || !IsPenis(a_state.pair.act) || a_state.anchors.toy) {
			return;
		}
		const auto giver = a_state.pair.giver.get();
		const float e = Excitement(giver.get());
		if (e < settings.precumFrom) {
			a_state.precumTimer = -1.0f;
			return;
		}
		const float k = std::clamp((e - settings.precumFrom) / std::max(100.0f - settings.precumFrom, 1.0f), 0.0f, 1.0f);
		const float interval = (4.0f - 3.3f * k) / std::max(settings.precumRate, 0.05f);
		if (a_state.precumTimer < 0.0f) {
			a_state.precumTimer = interval * std::uniform_real_distribution<float>(0.3f, 1.0f)(_rng);
			return;
		}
		a_state.precumTimer -= a_delta;
		if (a_state.precumTimer > 0.0f) {
			return;
		}
		a_state.precumTimer = interval * std::uniform_real_distribution<float>(0.6f, 1.4f)(_rng);
		const bool inside = a_state.anchors.Depth(a_state.anchors.tip) > 0.5f;
		if (!inside) {
			SpawnPrecum(a_state.pair, giver.get(), a_state.anchors.giverRoot, 0.55f + 0.5f * k);
			return;
		}
		const auto  receiver = a_state.pair.receiver.get();
		const Opening opening = OpeningOf(a_state.pair.act);
		if (opening != Opening::kMouth && AnatomicaBridge::Precum(receiver.get(), giver.get(), 0.015f + 0.025f * k)) {
			return;   // Anatomica takes it (he holds it, it wets him)
		}
		FluidManager::Get().Held(a_state.pair.receiver, a_state.pair.giver, static_cast<int>(opening), 2.0f + 3.0f * k);
	}

	void StrandManager::TickKissing(float a_delta)
	{
		const auto& settings = Settings::Get();
		if (!settings.oral || (!settings.kissStrings && !settings.snowball)) {
			_kisses.clear();
			return;
		}
		static RE::TESFaction* faction = nullptr;
		static bool            looked = false;
		if (!looked) {
			if (auto* dh = RE::TESDataHandler::GetSingleton()) {
				looked = true;
				faction = dh->LookupForm<RE::TESFaction>(0xE72, "OStim.esp");   // OStimKissingFaction
				if (!faction) {
					faction = dh->LookupForm<RE::TESFaction>(0xEC7, "OStim.esp");
				}
				logger::info("kissing: OStim kissing faction {}", faction ? "found" : "not found (no kissing strings)");
			}
		}
		if (!faction) {
			return;
		}
		auto mouth = [](RE::Actor* a_actor, std::vector<Point>& a_lips, RE::NiPoint3& a_center) {
			auto* root = a_actor ? a_actor->Get3D(false) : nullptr;
			RE::NiPoint3 forward;
			return root && MouthSite(root, a_lips, a_center, forward) && a_lips.size() >= 4;
		};
		_kissPoll -= a_delta;
		if (_kissPoll <= 0.0f) {   // who is kissing whom
			_kissPoll = 0.3f;
			std::vector<RE::Actor*> kissers;
			auto consider = [&](RE::Actor* a_actor) {
				if (a_actor && a_actor->Is3DLoaded() && a_actor->IsInFaction(faction)) {
					kissers.push_back(a_actor);
				}
			};
			consider(RE::PlayerCharacter::GetSingleton());
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				for (auto& handle : lists->highActorHandles) {
					if (auto actor = handle.get()) {
						consider(actor.get());
					}
				}
			}
			for (std::size_t i = 0; i < kissers.size(); ++i) {
				std::vector<Point> li;
				RE::NiPoint3       ci;
				if (!mouth(kissers[i], li, ci)) {
					continue;
				}
				RE::Actor* best = nullptr;
				float      bestDistance = 14.0f;
				for (std::size_t j = 0; j < kissers.size(); ++j) {
					std::vector<Point> lj;
					RE::NiPoint3       cj;
					if (j == i || !mouth(kissers[j], lj, cj)) {
						continue;
					}
					if (const float d = ci.GetDistance(cj); d < bestDistance) {
						bestDistance = d;
						best = kissers[j];
					}
				}
				if (!best) {
					continue;
				}
				const auto a = kissers[i]->GetHandle(), b = best->GetHandle();
				const bool known = std::ranges::any_of(_kisses, [&](const Kiss& k) { return (k.a == a && k.b == b) || (k.a == b && k.b == a); });
				if (!known) {
					_kisses.push_back({ a, b });
				}
			}
		}
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		for (auto it = _kisses.begin(); it != _kisses.end();) {
			const auto a = it->a.get(), b = it->b.get();
			std::vector<Point> la, lb;
			RE::NiPoint3       ca, cb;
			if (!a || !b || !mouth(a.get(), la, ca) || !mouth(b.get(), lb, cb)) {
				it = _kisses.erase(it);
				continue;
			}
			const bool kissing = a->IsInFaction(faction) || b->IsInFaction(faction);
			it->gone = kissing ? 0.0f : it->gone + a_delta;
			if (it->gone > 2.0f) {
				it = _kisses.erase(it);
				continue;
			}
			const float scale = a->Get3D(false)->world.scale;
			const float d = ca.GetDistance(cb);
			if (d < 3.2f * scale) {
				it->close += a_delta;
				it->lips = true;
				if (settings.snowball && !it->passed && it->close > 1.0f) {
					it->passed = true;
					FluidManager::Get().Snowball(it->a, it->b);
				}
			} else if (it->lips && d > 4.2f * scale) {   // the kiss parts: strings between the lips
				if (settings.kissStrings && it->close > 0.5f && EnsureMesh()) {
					PairState state;
					state.pair = Pair{ it->b, it->a, Act::kFellatio };
					state.anchors.receiverRoot = a->Get3D(false);
					const int count = std::clamp(1 + (unit(_rng) < 0.5f * std::max(settings.spit, 0.3f) ? 1 : 0), 1, 3);
					for (int n = 0; n < count; ++n) {
						Strand* strand = AcquireStrand();
						if (!strand) {
							break;
						}
						int ia = unit(_rng) < 0.6f ? 1 : static_cast<int>(unit(_rng) * 4.0f) % 4;   // mostly the lower lip
						int ib = unit(_rng) < 0.6f ? 0 : static_cast<int>(unit(_rng) * 4.0f) % 4;
						if (la.size() >= 6 && unit(_rng) < 0.4f) {   // 0.8.0.8: from a Vampire Lord's fangs
							ia = 4 + static_cast<int>(unit(_rng) * 2.0f) % 2;
						}
						if (lb.size() >= 6 && unit(_rng) < 0.4f) {
							ib = 4 + static_cast<int>(unit(_rng) * 2.0f) % 2;
						}
						StartBetween(*strand, Kind::kKiss, state, End{ RE::NiPointer<RE::NiAVObject>(la[ia].node), la[ia].local },
							End{ RE::NiPointer<RE::NiAVObject>(lb[ib].node), lb[ib].local }, settings.StrandRadius(false) * (0.55f + 0.25f * unit(_rng)));
						strand->creature = false;
						++_statBridges;
					}
					if (settings.debugLog) {
						logger::info("kissing: {} string(s) as the kiss parts", count);
					}
				}
				it->lips = false;
				it->close = 0.0f;
				it->passed = false;
			}
			++it;
		}
	}

	void StrandManager::TickTongues(float a_delta)
	{
		const auto& settings = Settings::Get();
		if (!settings.tongueDrool || !settings.beastPeople) {
			_tongues.clear();
			return;
		}
		_tonguePoll -= a_delta;
		if (_tonguePoll <= 0.0f) {   // beast forms nearby that are excited (OStim excitement 40+)
			_tonguePoll = 1.0f;
			auto consider = [&](RE::Actor* a_actor) {
				const int form = a_actor && a_actor->Is3DLoaded() ? Colossal::BeastForm(a_actor) : 0;
				if (form == 0 || (form == 3 && !settings.lordDrool) || Excitement(a_actor) < 40.0f) {
					return;
				}
				const auto handle = a_actor->GetHandle();
				auto it = std::ranges::find_if(_tongues, [&](const Tongue& t) { return t.actor == handle; });
				if (it == _tongues.end()) {
					_tongues.push_back({ handle, std::uniform_real_distribution<float>(1.0f, 4.0f)(_rng), 0.0f });
				} else {
					it->seen = 0.0f;
				}
			};
			consider(RE::PlayerCharacter::GetSingleton());
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				for (auto& handle : lists->highActorHandles) {
					if (auto actor = handle.get()) {
						consider(actor.get());
					}
				}
			}
		}
		for (auto it = _tongues.begin(); it != _tongues.end();) {
			it->seen += a_delta;
			const auto actor = it->actor.get();
			auto*      root = actor ? actor->Get3D(false) : nullptr;
			if (!root || it->seen > 6.0f) {   // calmed down (or gone): the last drips keep falling
				it = _tongues.erase(it);
				continue;
			}
			it->timer -= a_delta;
			if (it->timer <= 0.0f) {
				const float e = std::clamp((Excitement(actor.get()) - 40.0f) / 60.0f, 0.0f, 1.0f);
				it->timer = (6.0f - 4.0f * e) * std::uniform_real_distribution<float>(0.7f, 1.3f)(_rng);
				RE::NiAVObject* tip = nullptr;
				for (const char* name : { "WWTongue 5", "WWTongue 4", "WWTongue 3" }) {
					if ((tip = root->GetObjectByName(RE::BSFixedString(name)))) {
						break;
					}
				}
				// 0.8.0.8: a Vampire Lord very excited, or casting (Vampiric Drain): blood in it
				std::string tint;
				if (settings.lordBloodDrool && Colossal::BeastForm(actor.get()) == 3) {
					auto* caster = actor->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand);
					const bool casting = caster && caster->state.get() != RE::MagicCaster::State::kNone;
					if (casting || Excitement(actor.get()) >= 80.0f) {
						tint = TintTexturePath({ 0.55f, 0.04f, 0.04f });
					}
				}
				if (tip && EnsureMesh()) {
					SpawnDripAt(SelfPair(it->actor), Source::kGiver, root, { Point{ tip, {} } }, 0.7f, tint);
				} else if (!tip && EnsureMesh()) {   // 0.8.0.8: no tongue bones (a Vampire Lord, a werebear): from the lower lip
					std::vector<Point> lips;
					RE::NiPoint3       center, forward;
					if (Anatomy::MouthSite(root, lips, center, forward) && lips.size() > 1) {
						std::vector<Point> from{ lips[1] };
						if (lips.size() >= 6) {   // and the fangs
							from.push_back(lips[4]);
							from.push_back(lips[5]);
						}
						SpawnDripAt(SelfPair(it->actor), Source::kGiver, root, { from[static_cast<std::size_t>(std::uniform_int_distribution<int>(0, static_cast<int>(from.size()) - 1)(_rng))] }, 0.7f, tint);
					}
				}
			}
			++it;
		}
	}

	void StrandManager::TrackStroke(PairState& a_state, float a_delta)
	{
		const auto& settings = Settings::Get();
		const auto& anchors = a_state.anchors;
		const float depth = anchors.Depth(anchors.tip);
		a_state.cooldown -= a_delta;
		a_state.bridgeBudget = std::min(a_state.bridgeBudget + settings.bridgeRate * a_delta, kMaxBudget);

		// 0.8.0.8: precum strings - the tip touches the opening / lips and pulls away again without going in
		if (!anchors.toy && settings.precumStrings) {
			const float tipGap = anchors.tip.GetDistance(anchors.center);
			if (depth > 0.5f) {
				a_state.touched = false;
			} else if (tipGap < 2.2f && !a_state.wasIn) {
				a_state.touched = true;
			} else if (a_state.touched && tipGap > 3.8f) {
				a_state.touched = false;
				if (Excitement(a_state.pair.giver.get().get()) >= 20.0f && std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) < 0.6f) {
					SpawnPullStrings(a_state, 1 + (std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) < 0.4f ? 1 : 0), 0.6f);
				}
			}
		}
		// 0.8.0.7: the tip slides right out - strings stretch between the rim and the tip, thin and snap
		if (!anchors.toy) {
			if (depth > 0.5f) {
				a_state.wasIn = true;
			} else if (depth < -0.5f) {
				if (a_state.wasIn && settings.pullStrings > 0 && a_state.settle <= 0.0f && anchors.tip.GetDistance(anchors.center) < 12.0f) {
					// 0.8.0.8: a knot (OStim Beast Form / TWO werewolf knot bones) pops out: more, thicker strings and a gush
					auto* giver3D = anchors.giverRoot;
					const bool knot = settings.knotStrings && giver3D &&
					                  (giver3D->GetObjectByName(RE::BSFixedString("WWLK 1")) || giver3D->GetObjectByName(RE::BSFixedString("WWRK 1")));
					if (knot) {
						SpawnPullStrings(a_state, settings.pullStrings + 2, 1.6f);
						const auto receiver = a_state.pair.receiver.get();
						const Opening opening = OpeningOf(a_state.pair.act);
						if (opening == Opening::kMouth || !AnatomicaBridge::TakesOver(receiver.get())) {
							FluidManager::Get().Squeeze(a_state.pair.receiver, static_cast<int>(opening));
							FluidManager::Get().Squeeze(a_state.pair.receiver, static_cast<int>(opening));
						}
						if (settings.debugLog) {
							logger::info("knot pulled out: thicker strings and a gush");
						}
					} else {
						SpawnPullStrings(a_state);
					}
				}
				a_state.wasIn = false;
			}
		}
		const Crossing crossing = a_state.stroke.Step(-depth, a_delta, { kStrokeNear, kStrokeNearMax, kStrokeGap, kStrokeGapMax }, a_state.settle <= 0.0f);
		_statStroke = std::max(_statStroke, a_state.stroke.Range());
		if (crossing == Crossing::kArrived) {
			a_state.bridgePending = false;
			// 0.8.0.8: a deep thrust into a full opening squeezes some out (Anatomica does its own men's)
			const auto receiver = a_state.pair.receiver.get();
			if (!(OpeningOf(a_state.pair.act) != Opening::kMouth && AnatomicaBridge::TakesOver(receiver.get()))) {
				FluidManager::Get().Squeeze(a_state.pair.receiver, static_cast<int>(OpeningOf(a_state.pair.act)));
			}
		} else if (crossing == Crossing::kLeft) {
			a_state.bridgePending = true;
		}
		if (!a_state.bridgePending || a_state.cooldown > 0.0f) {
			return;
		}
		const bool inside = anchors.toy ? anchors.tip.GetDistance(anchors.center) < anchors.toyRadius + 0.5f : depth > 0.0f;
		if (!inside || SpawnBridges(a_state)) {
			a_state.bridgePending = false;
		}
	}

	bool StrandManager::TrackContact(PairState& a_state, float a_delta, const RE::NiPoint3& a_point, float a_rate, float a_maxGap)
	{
		const auto& anchors = a_state.anchors;
		const float distance = a_point.GetDistance(anchors.center);
		const float depth = anchors.Depth(a_point);
		_statClosest = std::min(_statClosest, distance);
		a_state.contactBudget = std::min(a_state.contactBudget + a_rate * a_delta, kMaxBudget);

		const Crossing crossing = a_state.contact.Step(depth > 0.0f ? -depth : distance, a_delta,
			{ kContactNear, kContactNearMax, kContactGap, kContactGapMax }, a_state.settle <= 0.0f);
		_statSwing = std::max(_statSwing, a_state.contact.Range());
		if (crossing == Crossing::kArrived) {
			a_state.contactPending = false;
		} else if (crossing == Crossing::kLeft) {
			a_state.contactPending = a_state.contact.low <= a_maxGap;
		}
		if (!a_state.contactPending || depth > 0.0f) {
			return false;
		}
		a_state.contactPending = false;
		return true;
	}

	void StrandManager::TrackCrotch(PairState& a_state, float a_delta)
	{
		const auto& settings = Settings::Get();
		const auto& anchors = a_state.anchors;
		if (!settings.crotch || anchors.crotch.empty()) {
			return;
		}
		if (TrackContact(a_state, a_delta, anchors.crotch.front()->world.translate, settings.crotchRate, settings.crotchMaxGap)) {
			SpawnCrotch(a_state);
		}
	}

	void StrandManager::TrackSurface(PairState& a_state, float a_delta, const SurfaceSettings& a_surface)
	{
		const auto& anchors = a_state.anchors;
		if (anchors.giverSite.empty()) {
			return;
		}
		if (TrackContact(a_state, a_delta, anchors.giverCenter, a_surface.rate, a_surface.maxGap)) {
			SpawnSurface(a_state, a_surface);
		}
	}

	void StrandManager::TickDrips(PairState& a_state, float a_delta)
	{
		const auto& settings = Settings::Get();
		if (!settings.drips) {
			return;
		}
		const Act     act = a_state.pair.act;
		const Opening opening = OpeningOf(act);
		// 0.8.0.8: Spit - the mouth drools more (or less, or not at all) than other openings drip
		const float spit = opening == Opening::kMouth ? settings.spit : 1.0f;
		if (spit <= 0.0f && opening == Opening::kMouth && !(act == Act::kFellatio && settings.spitShaft)) {
			return;
		}
		const float interval = settings.dripInterval / std::max(spit, 0.05f);
		std::exponential_distribution<float> next(1.0f / interval);
		auto tick = [&](float& a_timer, Source a_source) {
			if (a_timer < 0.0f) {
				a_timer = std::uniform_real_distribution<float>(0.3f, interval)(_rng);
				return;
			}
			a_timer -= a_delta;
			if (a_timer <= 0.0f) {
				SpawnDrip(a_state, a_source);
				a_timer = std::max(next(_rng), 0.4f);
			}
		};
		if (act == Act::kFellatio && settings.spitShaft && settings.spit > 0.0f) {   // 0.8.0.8: spit drips off the shaft
			a_state.shaftSpit -= a_delta;
			if (a_state.shaftSpit <= 0.0f) {
				SpawnShaftSpit(a_state);
				a_state.shaftSpit = std::max(std::exponential_distribution<float>(settings.spit / settings.dripInterval)(_rng), 0.4f);
			}
		}
		// 0.8.0.9: super messy - webs of spit between his lips as he sucks (break after a few seconds)
		if (settings.superMessy && settings.messyWebs > 0.0f && opening == Opening::kMouth && settings.spit > 0.0f && a_state.anchors.opening.size() >= 4 && a_state.anchors.receiverRoot) {
			a_state.webTimer -= a_delta;
			if (a_state.webTimer <= 0.0f && EnsureMesh()) {
				std::uniform_real_distribution<float> unit(0.0f, 1.0f);
				a_state.webTimer = (1.5f + 3.0f * unit(_rng)) / std::max(settings.spit * settings.messyWebs, 0.05f);
				const auto& lips = a_state.anchors.opening;   // 0 upper lip, 1 lower lip, 2 / 3 the corners
				auto along = [&](std::size_t a_lip) {         // somewhere along that lip, towards a random corner
					const std::size_t corner = unit(_rng) < 0.5f ? 2 : 3;
					const float       t = unit(_rng) * 0.8f;
					const RE::NiPoint3 at = lips[a_lip].World() * (1.0f - t) + lips[corner].World() * t;
					return End{ RE::NiPointer<RE::NiAVObject>(lips[a_lip].node), lips[a_lip].node->world.Invert() * at };
				};
				if (Strand* web = AcquireStrand()) {
					StartBetween(*web, Kind::kKiss, a_state, along(0), along(1), settings.StrandRadius(false) * (0.4f + 0.25f * unit(_rng)));
					web->creature = false;
					++_statBridges;
					++_statMessy[1];
				}
			}
		}
		// 0.8.0.9: super messy - spit drips off his lower lip and the corners of his mouth as he sucks (never the upper lip)
		if (settings.superMessy && settings.messyLipDrips > 0.0f && opening == Opening::kMouth && settings.spit > 0.0f && a_state.anchors.opening.size() >= 4 && a_state.anchors.receiverRoot) {
			a_state.lipDrip -= a_delta;
			if (a_state.lipDrip <= 0.0f) {
				std::uniform_real_distribution<float> unit(0.0f, 1.0f);
				a_state.lipDrip = std::max(std::exponential_distribution<float>(2.0f * settings.spit * settings.messyLipDrips / settings.dripInterval)(_rng), 0.25f);
				const auto&       lips = a_state.anchors.opening;   // 0 upper lip, 1 lower lip, 2 / 3 the corners
				const float       r = unit(_rng);
				const std::size_t corner = unit(_rng) < 0.5f ? 2 : 3;
				Point             from = lips[corner];               // a corner (40%)
				if (r < 0.6f) {                                      // or somewhere along the lower lip
					const float        t = r < 0.25f ? 0.0f : unit(_rng) * 0.75f;
					const RE::NiPoint3 at = lips[1].World() * (1.0f - t) + lips[corner].World() * t;
					from = Point{ lips[1].node, lips[1].node->world.Invert() * at };
				}
				const int before = _statDrips;
				SpawnDripAt(a_state.pair, Source::kReceiver, a_state.anchors.receiverRoot, { from }, (0.6f + 0.25f * unit(_rng)) * settings.messyDripSize, {},
					settings.dripMaxPerSource * 2);
				if (_statDrips != before) {
					++_statMessy[0];
				}
			}
		}
		// 0.8.0.9: super messy - a string of spit runs from his lower lip (off centre) down his chin as he sucks
		if (settings.superMessy && settings.messyChin > 0.0f && opening == Opening::kMouth && settings.spit > 0.0f && a_state.anchors.opening.size() >= 4 &&
			a_state.anchors.receiverRoot) {
			a_state.chinRun -= a_delta;
			if (a_state.chinRun <= 0.0f && EnsureMesh()) {
				std::uniform_real_distribution<float> unit(0.0f, 1.0f);
				a_state.chinRun = std::max(std::exponential_distribution<float>(0.35f * settings.spit * settings.messyChin)(_rng), 1.0f);
				const auto&        lips = a_state.anchors.opening;
				const std::size_t  corner = unit(_rng) < 0.5f ? 2 : 3;
				const float        t = 0.3f + 0.6f * unit(_rng);   // between the middle of his lower lip and that corner
				const RE::NiPoint3 lower = lips[1].World(), upper = lips[0].World();
				const RE::NiPoint3 from = lower * (1.0f - t) + lips[corner].World() * t;
				RE::NiPoint3       down = lower - upper;
				const float        h = std::max(down.Length(), 0.5f);
				down = down / h;
				const RE::NiPoint3 chin = from + down * (h * (1.8f + 1.2f * unit(_rng)));
				if (Strand* run = AcquireStrand()) {
					StartBetween(*run, Kind::kKiss, a_state, End{ RE::NiPointer<RE::NiAVObject>(lips[1].node), lips[1].node->world.Invert() * from },
						End{ RE::NiPointer<RE::NiAVObject>(lips[1].node), lips[1].node->world.Invert() * chin }, settings.StrandRadius(false) * (0.4f + 0.2f * unit(_rng)));
					run->creature = false;
					++_statBridges;
					++_statMessy[2];
				}
			}
		}
		const bool    receiverDrips = opening == Opening::kMouth ? settings.oralDrips : !a_state.anchors.vulva.empty();
		const bool    giverDrips = act == Act::kTribbing || (IsOralGiver(act) && settings.oralDrips) ||
		                        (act == Act::kVaginalFingering && settings.fingerDrips) ||
		                        (IsPenis(act) && a_state.strapWearer && settings.wearerDrips);
		if (receiverDrips) {
			tick(a_state.receiverDrip, Source::kReceiver);
		}
		if (giverDrips && !a_state.anchors.giverSite.empty()) {
			tick(a_state.giverDrip, Source::kGiver);
		}
	}

	int StrandManager::Count(const Pair& a_pair, Kind a_kind, std::optional<Source> a_source) const
	{
		return static_cast<int>(std::ranges::count_if(_strands, [&](const Strand& s) {
			return s.active && s.kind == a_kind && s.pair == a_pair && (!a_source || s.source == *a_source);
		}));
	}

	int StrandManager::CountActive() const
	{
		return static_cast<int>(std::ranges::count_if(_strands, [](const Strand& s) { return s.active; }));
	}

	void StrandManager::Attach(Strand& a_strand, RE::NiAVObject* a_parent)
	{
		a_strand.root->local = RE::NiTransform{};
		a_parent->AsNode()->AttachChild(a_strand.root.get(), true);
		a_strand.attachedTo = a_parent;
		a_strand.active = true;
		Pose(a_strand);
	}

	void StrandManager::StartBetween(Strand& a_strand, Kind a_kind, const PairState& a_state, End a_top, End a_bottom, float a_radius)
	{
		const RE::NiPoint3 top = a_top.World();
		const RE::NiPoint3 bottom = a_bottom.World();
		const float        length = top.GetDistance(bottom);

		a_strand.kind = a_kind;
		a_strand.source = Source::kReceiver;
		a_strand.owner = a_state.pair.receiver;
		a_strand.pair = a_state.pair;
		a_strand.top = std::move(a_top);
		a_strand.bottom = std::move(a_bottom);
		for (std::size_t i = 0; i < kBones; ++i) {
			a_strand.pos[i] = a_strand.prev[i] = Lerp(top, bottom, static_cast<float>(i) / (kBones - 1));
		}
		a_strand.rest.fill(std::max(length / (kBones - 1), 0.02f));
		a_strand.volume = kPi * a_radius * a_radius * std::max(length, 1.0f);
		a_strand.startLength = length;
		a_strand.radius = a_radius;
		a_strand.radius0 = a_radius;
		a_strand.age = 0.0f;
		a_strand.fade = 1.0f;
		a_strand.fadeRate = 0.0f;
		a_strand.breakAt = -1;
		const auto giver = a_state.pair.giver.get();
		const auto receiver = a_state.pair.receiver.get();
		a_strand.creature = Colossal::OwnMinRadius(Colossal::Of(giver.get(), receiver.get()));
		Attach(a_strand, a_state.anchors.receiverRoot);
	}

	bool StrandManager::SpawnBridges(PairState& a_state)
	{
		const auto& settings = Settings::Get();
		if (a_state.bridgeBudget < 1.0f) {
			return true;
		}

		const auto&     anchors = a_state.anchors;
		RE::NiPoint3    crossing = anchors.center;
		RE::NiPoint3    axis = anchors.inward;
		RE::NiAVObject* bone = anchors.toy;
		if (!bone) {
			std::vector<RE::NiPoint3> chain;
			for (auto* node : anchors.chain) {
				chain.push_back(node->world.translate);
			}
			chain.push_back(anchors.tip);
			int   segment = -1;
			float t = 0.0f;
			for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
				const float d0 = anchors.Depth(chain[i]);
				const float d1 = anchors.Depth(chain[i + 1]);
				if (d0 <= 0.0f && d1 > 0.0f) {
					segment = static_cast<int>(i);
					t = d0 / (d0 - d1);
					break;
				}
			}
			if (segment < 0) {
				return false;
			}
			crossing = Lerp(chain[segment], chain[segment + 1], t);
			axis = Normalized(chain[segment + 1] - chain[segment], anchors.inward);
			bone = anchors.chain[segment];
		}
		a_state.cooldown = settings.cooldown;

		std::uniform_real_distribution<float> chance(0.0f, 1.0f);
		int count = 1 + (chance(_rng) < 0.4f ? 1 : 0);
		count = std::min({ count, static_cast<int>(a_state.bridgeBudget), settings.maxPerStroke, settings.maxPerPair - Count(a_state.pair, Kind::kBridge) });

		std::vector<Point> rims = anchors.opening;
		std::ranges::shuffle(rims, _rng);
		for (int n = 0; n < count && n < static_cast<int>(rims.size()); ++n) {
			Strand* strand = AcquireStrand();
			if (!strand) {
				return true;
			}
			RE::NiPoint3 radial = rims[n].World() - crossing;
			radial -= axis * radial.Dot(axis);
			const float        radius = a_state.shaftRadius * bone->world.scale / anchors.chainScale;
			const RE::NiPoint3 shaftPoint = crossing + Normalized(radial, anchors.inward.Cross(axis)) * radius;
			StartBetween(*strand, Kind::kBridge, a_state, End{ RE::NiPointer<RE::NiAVObject>(rims[n].node), rims[n].local },
				End{ RE::NiPointer<RE::NiAVObject>(bone), bone->world.Invert() * shaftPoint }, Colossal::StrandRadius(Colossal::Of(a_state.pair.giver.get().get(), a_state.pair.receiver.get().get())));
			a_state.bridgeBudget -= 1.0f;
			++_statBridges;
		}
		return true;
	}

	void StrandManager::SpawnPullStrings(PairState& a_state, int a_count, float a_radius)
	{
		const auto& settings = Settings::Get();
		const auto& anchors = a_state.anchors;
		if (anchors.chain.empty() || anchors.opening.empty()) {
			return;
		}
		RE::NiAVObject* bone = anchors.chain.back();
		std::uniform_int_distribution<int> extra(0, 1);
		int count = std::min(a_count >= 0 ? a_count : settings.pullStrings + extra(_rng), settings.maxPerPair - Count(a_state.pair, Kind::kBridge));
		std::vector<Point> rims = anchors.opening;
		std::ranges::shuffle(rims, _rng);
		std::uniform_real_distribution<float> spread(-0.35f, 0.35f);
		const RE::NiPoint3 side = Normalized(anchors.inward.Cross(RE::NiPoint3{ 0.0f, 0.0f, 1.0f }), RE::NiPoint3{ 1.0f, 0.0f, 0.0f });
		const RE::NiPoint3 up = Normalized(anchors.inward.Cross(side), RE::NiPoint3{ 0.0f, 0.0f, 1.0f });
		const float radius = Colossal::StrandRadius(Colossal::Of(a_state.pair.giver.get().get(), a_state.pair.receiver.get().get()));
		for (int n = 0; n < count && n < static_cast<int>(rims.size()); ++n) {
			Strand* strand = AcquireStrand();
			if (!strand) {
				return;
			}
			const RE::NiPoint3 at = anchors.tip + side * spread(_rng) + up * spread(_rng);   // round the tip, not all from one point
			StartBetween(*strand, Kind::kBridge, a_state, End{ RE::NiPointer<RE::NiAVObject>(rims[n].node), rims[n].local },
				End{ RE::NiPointer<RE::NiAVObject>(bone), bone->world.Invert() * at }, radius * a_radius);
			++_statBridges;
		}
		if (settings.debugLog) {
			logger::info("pull-out: {} string(s) between the rim and the tip", std::min(count, static_cast<int>(rims.size())));
		}
	}

	void StrandManager::SpawnCrotch(PairState& a_state)
	{
		const auto& settings = Settings::Get();
		if (a_state.contactBudget < 1.0f) {
			return;
		}
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const auto& anchors = a_state.anchors;
		int count = std::uniform_int_distribution<int>(1, std::max(settings.crotchMaxPerContact, 1))(_rng);
		count = std::min({ count, static_cast<int>(a_state.contactBudget),
			settings.crotchMaxPerPair - Count(a_state.pair, Kind::kContact) });

		for (int n = 0; n < count; ++n) {
			Strand* strand = AcquireStrand();
			if (!strand) {
				return;
			}
			const Point&       rim = Pick(anchors.opening, _rng);
			const RE::NiPoint3 rimPos = rim.World();
			RE::NiPoint3       outward = rimPos - anchors.center;
			outward -= anchors.inward * outward.Dot(anchors.inward);
			const RE::NiPoint3 sideways = Normalized(outward, { 1.0f, 0.0f, 0.0f }).Cross(anchors.inward);
			const RE::NiPoint3 receiverPoint = rimPos + Normalized(outward, sideways) * (settings.crotchSpread * unit(_rng)) +
			                                   sideways * (settings.crotchSpread * 0.5f * (unit(_rng) - 0.5f));

			RE::NiAVObject* giver = nullptr;
			RE::NiPoint3    giverPoint;
			if (a_state.strapWearer && !anchors.giverSite.empty() && unit(_rng) < 0.5f) {
				const Point& site = Pick(anchors.giverSite, _rng);
				giver = site.node;
				giverPoint = site.World();
			} else {
				const std::size_t pick = std::uniform_int_distribution<std::size_t>(0, anchors.crotch.size() - 1)(_rng);
				giver = anchors.crotch[pick];
				const RE::NiPoint3 bone = giver->world.translate;
				if (pick == 0) {
					const RE::NiPoint3 axis = Normalized(anchors.chain[1]->world.translate - bone, anchors.inward);
					RE::NiPoint3       toward = receiverPoint - bone;
					toward -= axis * toward.Dot(axis);
					const RE::NiPoint3 side = Normalized(toward, Normalized(axis.Cross({ 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f }));
					const float        angle = std::uniform_real_distribution<float>(-kPi, kPi)(_rng);
					const RE::NiPoint3 around = side * std::cos(angle) + axis.Cross(side) * std::sin(angle);
					const float        shaft = a_state.shaftRadius * giver->world.scale / anchors.chainScale;
					giverPoint = bone + around * (shaft + settings.crotchSkin);
				} else {
					giverPoint = bone + Normalized(receiverPoint - bone, -anchors.inward) * settings.crotchSkin;
				}
			}

			StartBetween(*strand, Kind::kContact, a_state,
				End{ RE::NiPointer<RE::NiAVObject>(rim.node), rim.node->world.Invert() * receiverPoint },
				End{ RE::NiPointer<RE::NiAVObject>(giver), giver->world.Invert() * giverPoint }, Colossal::CrotchRadius(Colossal::Of(a_state.pair.giver.get().get(), a_state.pair.receiver.get().get())));
			a_state.contactBudget -= 1.0f;
			++_statContacts;
		}
	}

	void StrandManager::SpawnSurface(PairState& a_state, const SurfaceSettings& a_surface)
	{
		const auto& settings = Settings::Get();
		if (a_state.contactBudget < 1.0f) {
			return;
		}
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const auto& anchors = a_state.anchors;
		int count = std::uniform_int_distribution<int>(1, std::max(a_surface.maxPerContact, 1))(_rng);
		count = std::min({ count, static_cast<int>(a_state.contactBudget), a_surface.maxPerPair - Count(a_state.pair, Kind::kContact) });

		const RE::NiPoint3 between = Normalized(anchors.center - anchors.giverCenter, anchors.inward);
		auto around = [&](const Point& a_point, const RE::NiPoint3& a_center) {
			const RE::NiPoint3 pos = a_point.World();
			RE::NiPoint3       outward = pos - a_center;
			outward -= between * outward.Dot(between);
			const RE::NiPoint3 sideways = Normalized(outward, { 1.0f, 0.0f, 0.0f }).Cross(between);
			return pos + Normalized(outward, sideways) * (a_surface.spread * unit(_rng)) + sideways * (a_surface.spread * 0.5f * (unit(_rng) - 0.5f));
		};

		for (int n = 0; n < count; ++n) {
			Strand* strand = AcquireStrand();
			if (!strand) {
				return;
			}
			const Point&       receiver = Pick(anchors.opening, _rng);
			const Point&       giver = Pick(anchors.giverSite, _rng);
			const RE::NiPoint3 receiverPoint = around(receiver, anchors.center);
			const RE::NiPoint3 giverPoint = around(giver, anchors.giverCenter);
			StartBetween(*strand, Kind::kContact, a_state,
				End{ RE::NiPointer<RE::NiAVObject>(receiver.node), receiver.node->world.Invert() * receiverPoint },
				End{ RE::NiPointer<RE::NiAVObject>(giver.node), giver.node->world.Invert() * giverPoint }, Colossal::CrotchRadius(Colossal::Of(a_state.pair.giver.get().get(), a_state.pair.receiver.get().get())));
			a_state.contactBudget -= 1.0f;
			++_statContacts;
		}
	}

	void StrandManager::SpawnDrip(PairState& a_state, Source a_source)
	{
		const auto& anchors = a_state.anchors;
		const auto& points = a_source == Source::kGiver                  ? anchors.giverSite :
		                     OpeningOf(a_state.pair.act) == Opening::kMouth ? anchors.opening :
		                                                                      anchors.vulva;
		if (a_source == Source::kReceiver && OpeningOf(a_state.pair.act) == Opening::kMouth && points.size() >= 4) {
			// 0.8.0.8: drool hangs from the corners of the mouth (the lip points are top, bottom, then the two corners)
			SpawnDripAt(a_state.pair, a_source, anchors.receiverRoot, { points[2], points[3] });
			return;
		}
		SpawnDripAt(a_state.pair, a_source, a_source == Source::kGiver ? anchors.giverRoot : anchors.receiverRoot, points);
	}

	void StrandManager::SpawnShaftSpit(PairState& a_state)
	{
		// 0.8.0.8: a drip of spit from the underside of the shaft, just outside the lips
		const auto& anchors = a_state.anchors;
		if (anchors.chain.empty()) {
			return;
		}
		RE::NiAVObject* best = nullptr;
		float           bestDepth = -1e9f;
		for (auto* node : anchors.chain) {
			const float d = anchors.Depth(node->world.translate);
			if (d <= 0.0f && d > bestDepth) {
				bestDepth = d;
				best = node;
			}
		}
		if (!best || bestDepth < -6.0f) {
			return;
		}
		const float        radius = a_state.shaftRadius > 0.0f ? a_state.shaftRadius : 1.0f;
		const RE::NiPoint3 under = best->world.translate + RE::NiPoint3{ 0.0f, 0.0f, -radius * 0.9f };
		SpawnDripAt(a_state.pair, Source::kGiver, anchors.giverRoot ? anchors.giverRoot : best, { Point{ best, best->world.Invert() * under } }, 0.8f);
	}

	void StrandManager::SpawnDripAt(const Pair& a_pair, Source a_source, RE::NiAVObject* a_root, const std::vector<Point>& a_points, float a_scale,
		const std::string& a_tint, int a_cap)
	{
		const auto& settings = Settings::Get();
		if (a_points.empty() || !a_root || Count(a_pair, Kind::kDrip, a_source) >= (a_cap > 0 ? a_cap : settings.dripMaxPerSource)) {
			return;
		}
		Strand* strand = AcquireStrand();
		if (!strand) {
			return;
		}
		const Point        point = LowPoint(a_points, _rng);
		const RE::NiPoint3 top = point.World();

		strand->kind = Kind::kDrip;
		strand->source = a_source;
		strand->owner = a_source == Source::kGiver ? a_pair.giver : a_pair.receiver;
		strand->pair = a_pair;
		strand->top = End{ RE::NiPointer<RE::NiAVObject>(point.node), point.local };
		strand->bottom = End{};
		for (std::size_t i = 0; i < kBones; ++i) {
			strand->pos[i] = strand->prev[i] = top - RE::NiPoint3{ 0.0f, 0.0f, kDripSeed * static_cast<float>(i) };
		}
		const float radius = settings.dripRadius * a_scale;
		strand->rest.fill(kDripSeed);
		strand->volume = kPi * radius * radius;
		strand->startLength = 0.0f;
		strand->radius = radius;
		strand->age = 0.0f;
		strand->fade = 1.0f;
		strand->fadeRate = 0.0f;
		strand->breakAt = -1;
		strand->tint = a_tint;
		Attach(*strand, a_root);
		++_statDrips;
	}

	void StrandManager::BeginLinger(const PairState& a_state)
	{
		const auto& settings = Settings::Get();
		if (!settings.drips || settings.dripLinger <= 0.0f || !a_state.resolved) {
			return;
		}
		auto add = [&](Source a_source, RE::ActorHandle a_actor, float a_timer) {
			if (std::ranges::any_of(_lingering, [&](const Linger& a_linger) { return a_linger.actor == a_actor; })) {
				return;
			}
			const float timer = a_timer > 0.0f ? a_timer : std::uniform_real_distribution<float>(0.3f, settings.dripInterval)(_rng);
			_lingering.push_back({ a_state.pair, a_source, a_actor, settings.dripLinger, timer });
			if (settings.debugLog) {
				if (const auto actor = a_actor.get()) {
					logger::info("{} keeps dripping for {}s", actor->GetDisplayFullName(), settings.dripLinger);
				}
			}
		};
		const Act act = a_state.pair.act;
		if (OpeningOf(act) != Opening::kMouth && !a_state.anchors.vulva.empty()) {
			add(Source::kReceiver, a_state.pair.receiver, a_state.receiverDrip);
		}
		if (OpeningOf(act) == Opening::kMouth && settings.droolTime > 0.0f && settings.spit > 0.0f &&
			!std::ranges::any_of(_lingering, [&](const Linger& a_linger) { return a_linger.actor == a_state.pair.receiver; })) {
			// 0.8.0.8: drool strings keep coming from the corners of the mouth for a while
			_lingering.push_back({ a_state.pair, Source::kReceiver, a_state.pair.receiver, settings.droolTime * (settings.superMessy ? settings.messyDrool : 1.0f),
				std::uniform_real_distribution<float>(0.3f, 1.5f)(_rng), true });
		}
		const bool giverVulva = act == Act::kTribbing || (IsPenis(act) && a_state.strapWearer && settings.wearerDrips);
		if (giverVulva && !a_state.anchors.giverSite.empty()) {
			add(Source::kGiver, a_state.pair.giver, a_state.giverDrip);
		}
	}

	void StrandManager::TickLinger(float a_delta)
	{
		const auto& settings = Settings::Get();
		const auto& names = Names::Get();
		std::erase_if(_lingering, [&](Linger& a_linger) {
			a_linger.remaining -= a_delta;
			if (!settings.drips || a_linger.remaining <= 0.0f) {
				return true;
			}
			const auto actor = a_linger.actor.get();
			auto*      root = actor ? actor->Get3D(false) : nullptr;
			if (!root || (!a_linger.mouth && !Revealing::Exposed(actor.get()))) {
				return true;
			}
			a_linger.timer -= a_delta;
			if (a_linger.timer > 0.0f) {
				return false;
			}
			if (a_linger.mouth) {   // 0.8.0.8
				std::vector<Point> lips;
				RE::NiPoint3       center, forward;
				if (!MouthSite(root, lips, center, forward) || lips.size() < 4) {
					return true;
				}
				// 0.8.0.8: one corner at a time - both sides, the lower one a little more often (it used to drip between the two
				// lowest points, so always from about the same place)
				std::uniform_real_distribution<float> unit(0.0f, 1.0f);
				const bool rightLower = lips[2].World().z <= lips[3].World().z;
				const std::size_t c = (unit(_rng) < 0.65f) == rightLower ? 2 : 3;
				Point from = lips[c];
				if (lips.size() >= 6 && unit(_rng) < 0.3f) {   // a Vampire Lord's fang on that side
					from = lips[c == 2 ? 4 : 5];
				}
				SpawnDripAt(a_linger.pair, a_linger.source, root, { from }, 0.85f);
				// and now and then a string that runs from that corner down the side of his chin
				if (settings.spit > 0.0f && unit(_rng) < (settings.superMessy ? 0.85f : 0.5f) && EnsureMesh()) {
					if (Strand* run = AcquireStrand()) {
						const RE::NiPoint3 corner = lips[c].World();
						const RE::NiPoint3 upper = lips[0].World(), lower = lips[1].World();
						RE::NiPoint3       down = lower - upper;
						const float        h = std::max(down.Length(), 0.5f);
						down = down / h;
						const RE::NiPoint3 chin = corner + down * (h * (2.0f + unit(_rng))) + (lower - corner) * 0.2f;
						PairState state;
						state.pair = a_linger.pair;
						state.anchors.receiverRoot = root;
						StartBetween(*run, Kind::kKiss, state, End{ RE::NiPointer<RE::NiAVObject>(lips[c].node), lips[c].local },
							End{ RE::NiPointer<RE::NiAVObject>(lips[c].node), lips[c].node->world.Invert() * chin },
							settings.StrandRadius(false) * (0.45f + 0.2f * unit(_rng)));
						run->creature = false;
						run->owner = a_linger.actor;
						++_statBridges;
					}
				}
				const float left = std::max(a_linger.remaining / std::max(settings.droolTime, 0.01f), 0.2f);
				a_linger.timer = std::max(std::exponential_distribution<float>(left * std::max(settings.spit, 0.1f) / settings.dripInterval)(_rng), 0.5f);
				return false;
			}
			std::vector<Point> vulva;
			RE::NiPoint3       center;
			if (!VulvaSite(root, vulva, center)) {
				return true;
			}
			SpawnDripAt(a_linger.pair, a_linger.source, root, vulva);
			const float left = std::max(a_linger.remaining / std::max(settings.dripLinger, 0.01f), 0.2f);
			a_linger.timer = std::max(std::exponential_distribution<float>(left / settings.dripInterval)(_rng), 0.4f);
			return false;
		});
	}

	void StrandManager::TickAroused(float a_delta)
	{
		const auto& settings = Settings::Get();
		const auto& names = Names::Get();
		const float span = std::max(100.0f - settings.arousalThreshold, 1.0f);
		for (auto& entry : _aroused) {
			const auto actor = entry.who.actor.get();
			auto*      root = actor ? actor->Get3D(false) : nullptr;
			if (!root || !Revealing::Exposed(actor.get())) {
				entry.vulva = -1.0f;
				entry.tip = -1.0f;
				continue;
			}
			const float heat = std::clamp((entry.who.level - settings.arousalThreshold) / span, 0.0f, 1.0f);
			const float interval = settings.arousalInterval * (4.0f - 3.0f * heat);
			const auto  due = [&](float& a_timer) {
				if (a_timer < 0.0f) {
					a_timer = std::uniform_real_distribution<float>(0.3f, interval)(_rng);
					return false;
				}
				a_timer -= a_delta;
				if (a_timer > 0.0f) {
					return false;
				}
				a_timer = std::max(std::exponential_distribution<float>(1.0f / interval)(_rng), 0.4f);
				return true;
			};
			const Pair pair = SelfPair(entry.who.actor);
			const bool lingering = std::ranges::any_of(_lingering, [&](const Linger& a_linger) { return a_linger.actor == entry.who.actor; });
			if (settings.arousalWetness && !lingering && due(entry.vulva)) {
				std::vector<Point> vulva;
				RE::NiPoint3       center;
				if (VulvaSite(root, vulva, center)) {
					SpawnDripAt(pair, Source::kReceiver, root, vulva, 0.8f + 0.4f * heat);
				}
			}
			if (settings.arousalPrecum && due(entry.tip)) {
				SpawnPrecum(pair, actor.get(), root, 0.5f + 0.2f * heat);
			}
		}
	}

	void StrandManager::SpawnPrecum(const Pair& a_pair, RE::Actor* a_actor, RE::NiAVObject* a_root, float a_scale)
	{
		const auto shaft = GetShaftState(a_actor);
		if (!shaft.schlong || shaft.strapOn) {
			return;
		}
		const auto tip = Body::TipOf(a_actor);
		const auto bones = ShaftOf(a_actor).bones;
		if (!tip || bones.empty()) {
			return;
		}
		auto* bone = bones.back();
		SpawnDripAt(a_pair, Source::kGiver, a_root, { Point{ bone, bone->world.Invert() * tip->point } }, a_scale);
	}

	void StrandManager::Snap(Strand& a_strand, int a_segment, float a_fadeTime)
	{
		if (a_strand.breakAt >= 0) {
			return;
		}
		a_strand.breakAt = std::clamp(a_segment, 0, static_cast<int>(kBones) - 2);
		a_strand.fadeRate = 1.0f / std::max(a_fadeTime, 0.05f);
	}

	void StrandManager::Simulate(Strand& a_strand, float a_delta, std::span<const Body::Capsule> a_bodies, std::span<const Fluid::Skin* const> a_skins)
	{
		const auto&        settings = Settings::Get();
		const bool         drip = a_strand.kind == Kind::kDrip;
		const RE::NiPoint3 a = a_strand.top.World();
		const RE::NiPoint3 b = drip ? RE::NiPoint3{} : a_strand.bottom.World();
		const RE::NiPoint3 gravity{ 0.0f, 0.0f, -settings.gravity };
		const float        h = a_delta / kSubsteps;
		const float        damping = GeometryMath::DampingPerStep(settings.damping, h);
		const int          last = static_cast<int>(kBones) - 1;
		const int          freeEnd = drip ? last : last - 1;

		for (int step = 0; step < kSubsteps; ++step) {
			const float velocityScale = damping * GeometryMath::DisplacementScale(h, a_strand.previousStep);
			if (drip && a_strand.breakAt < 0) {
				const float growth = std::exp(h / std::max(settings.dripStretchTime, 0.05f));
				for (auto& rest : a_strand.rest) {
					rest *= growth;
				}
			}
			for (int i = 1; i <= freeEnd; ++i) {
				float weight = 1.0f;
				if (drip && i == last) {
					weight = settings.dripWeight;
				} else if (drip && i == last - 1) {
					weight = 0.5f * (1.0f + settings.dripWeight);
				}
				const RE::NiPoint3 velocity = (a_strand.pos[i] - a_strand.prev[i]) * velocityScale;
				a_strand.prev[i] = a_strand.pos[i];
				a_strand.pos[i] += velocity + gravity * (weight * h * h);
			}
			a_strand.previousStep = h;
			a_strand.pos[0] = a;
			if (!drip) {
				a_strand.pos[last] = b;
			}

			for (int iteration = 0; iteration < kIterations; ++iteration) {
				for (int k = 0; k < last; ++k) {
					if (k == a_strand.breakAt) {
						continue;
					}
					RE::NiPoint3 d = a_strand.pos[k + 1] - a_strand.pos[k];
					const float  length = d.Length();
					if (length < 1e-5f) {
						continue;
					}
					const RE::NiPoint3 correction = d * ((length - a_strand.rest[k]) / length);
					const bool         pinnedA = k == 0;
					const bool         pinnedB = !drip && k + 1 == last;
					if (pinnedA && pinnedB) {
						continue;
					}
					if (pinnedA) {
						a_strand.pos[k + 1] -= correction;
					} else if (pinnedB) {
						a_strand.pos[k] += correction;
					} else {
						a_strand.pos[k] += correction * 0.5f;
						a_strand.pos[k + 1] -= correction * 0.5f;
					}
				}
			}

			for (int i = 1; i <= freeEnd; ++i) {
				const RE::NiPoint3 before = a_strand.pos[i];
				bool               moved = Body::PushOut(a_bodies, {}, a_strand.pos[i], a_strand.radius + kSkinMargin);
				if (a_strand.pos[i].GetDistance(a) > kWrapClearance && (drip || a_strand.pos[i].GetDistance(b) > kWrapClearance)) {
					for (const auto* skin : a_skins) {
						moved = Body::PushOut(*skin, a_strand.pos[i], a_strand.radius + kSkinMargin) || moved;
					}
				}
				if (moved) {
					a_strand.prev[i] += a_strand.pos[i] - before;
					a_strand.prev[i] += (a_strand.pos[i] - a_strand.prev[i]) * kSkinFriction;
				}
			}

			if (!drip) {
				const float flow = std::min(1.0f, settings.viscosity * h);
				for (int k = 0; k < last; ++k) {
					const float length = a_strand.pos[k].GetDistance(a_strand.pos[k + 1]);
					if (length > a_strand.rest[k]) {
						a_strand.rest[k] += (length - a_strand.rest[k]) * flow;
					}
				}
			}
		}

		float total = 0.0f;
		for (int k = 0; k < last; ++k) {
			if (k != a_strand.breakAt) {
				total += a_strand.pos[k].GetDistance(a_strand.pos[k + 1]);
			}
		}
		a_strand.radius = std::sqrt(a_strand.volume / (kPi * std::max(total, 0.01f)));
		a_strand.age += a_delta;

		if (a_strand.breakAt < 0) {
			if (drip) {
				if (a_strand.radius < settings.dripMinRadius || total > settings.dripMaxLength || a_strand.age > settings.dripMaxLife) {
					Snap(a_strand, std::uniform_int_distribution<int>(1, 2)(_rng), settings.dripFadeTime);
				}
			} else {
				const bool contact = a_strand.kind == Kind::kContact;
				if (a_strand.radius < (contact ? settings.CrotchMinRadius(a_strand.creature) : settings.StrandMinRadius(a_strand.creature)) ||
					total - a_strand.startLength > (contact ? settings.crotchMaxLength : settings.maxLength) ||
					a_strand.age > (contact ? settings.crotchMaxLife : settings.maxLife)) {
					Snap(a_strand, std::uniform_int_distribution<int>(static_cast<int>(kBones) / 2 - 2, static_cast<int>(kBones) / 2)(_rng),
						settings.fadeTime);
				}
			}
		}
		if (a_strand.breakAt >= 0) {
			a_strand.fade -= a_strand.fadeRate * a_delta;
		}
	}

	void StrandManager::Pose(Strand& a_strand)
	{
		const auto&           settings = Settings::Get();
		const bool            drip = a_strand.kind == Kind::kDrip;
		const float           minRadius = drip                              ? settings.dripMinRadius :
		                                  a_strand.kind == Kind::kContact ? settings.CrotchMinRadius(a_strand.creature) :
		                                                                    settings.StrandMinRadius(a_strand.creature);
		const RE::NiTransform toRoot = a_strand.root->parent ? a_strand.root->parent->world.Invert() : RE::NiTransform{};
		const int             last = static_cast<int>(kBones) - 1;
		GeometryMath::UpdateFrames(a_strand.frames, a_strand.pos, a_strand.breakAt);

		float beading = std::clamp(1.0f - (a_strand.radius - minRadius) / (0.8f * minRadius), 0.0f, 1.0f);
		if (!drip && settings.beads > 0.0f && a_strand.radius0 > 0.0f) {   // 0.8.0.7: beads as it thins, not only just before it snaps
			const float thin = std::clamp((1.0f - a_strand.radius / a_strand.radius0) / 0.45f, 0.0f, 1.0f);
			beading = std::max(beading, settings.beads * thin);
		}

		for (int i = 0; i <= last; ++i) {
			const float t = static_cast<float>(i) / last;
			float       radius;
			if (drip) {
				radius = a_strand.radius * (1.0f + 0.6f * std::max(0.0f, 1.0f - 4.0f * t));
				if (i == last) {
					radius = std::max(a_strand.radius * 2.4f, settings.dripRadius * 1.1f);
				} else if (i == last - 1) {
					radius = std::max(a_strand.radius * 1.5f, settings.dripRadius * 0.6f);
				}
			} else {
				radius = a_strand.radius * (1.0f + 0.6f * (1.0f - std::sin(kPi * t)));
			}
			if (i > 0 && i < last - 1) {
				radius *= 1.0f + 0.5f * beading * ((i % 2) ? 1.0f : -0.6f);
			}
			if (a_strand.breakAt >= 0 && (i == a_strand.breakAt || i == a_strand.breakAt + 1)) {
				radius = 0.0f;
			}
			radius *= std::max(a_strand.fade, 0.0f);
			if (!drip) {
				radius *= std::min(a_strand.age / kFadeIn, 1.0f);
			}

			RE::NiTransform world;
			world.rotate = Rotation(a_strand.frames[i]);
			world.translate = a_strand.pos[i];
			world.scale = std::max(radius, 0.0005f);
			a_strand.bones[i]->local = toRoot * world;
		}
		const float opacity = 1.0f - settings.transparency;
		if (a_strand.shader && a_strand.opacity != opacity) {
			a_strand.shader->SetMaterialAlpha(opacity);
			a_strand.opacity = opacity;
		}
		if (a_strand.shader && (a_strand.shine != settings.shine || a_strand.gloss != settings.gloss)) {
			if (a_strand.shader->GetMaterialType() == RE::BSShaderMaterial::Type::kLighting) {
				if (auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(a_strand.shader->GetBaseMaterial())) {
					material->specularColorScale = settings.shine;
					material->specularPower = settings.gloss;
				}
			}
			a_strand.shine = settings.shine;
			a_strand.gloss = settings.gloss;
		}
		if (a_strand.tint != a_strand.tintApplied) {
			Retexture(a_strand);
		}
		RE::NiUpdateData update{};
		a_strand.root->Update(update);
	}

	void StrandManager::Retexture(Strand& a_strand)
	{
		a_strand.tintApplied = a_strand.tint;
		auto* shape = a_strand.root ? a_strand.root->GetObjectByName(MeshNames::Get().strandShape) : nullptr;
		auto* geometry = shape ? shape->AsGeometry() : nullptr;
		auto* shader = a_strand.shader ? netimmerse_cast<RE::BSLightingShaderProperty*>(a_strand.shader.get()) : nullptr;
		auto* material = shader ? static_cast<RE::BSLightingShaderMaterialBase*>(shader->material) : nullptr;
		if (!geometry || !material) {
			return;
		}
		if (a_strand.baseDiffuse.empty()) {
			const char* path = material->textureSet ? material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse) : nullptr;
			a_strand.baseDiffuse = path && *path ? path : "textures\\OStrands\\semen.dds";
		}
		const std::string& want = a_strand.tint.empty() ? a_strand.baseDiffuse : a_strand.tint;
		auto* copy = static_cast<RE::BSLightingShaderMaterialBase*>(material->Create());
		if (!copy) {
			return;
		}
		copy->CopyMembers(material);
		if (auto* textures = RE::BSShaderTextureSet::Create()) {
			for (std::uint32_t i = 0; i < RE::BSTextureSet::Texture::kTotal; ++i) {
				const auto  slot = static_cast<RE::BSTextureSet::Texture>(i);
				const char* path = material->textureSet ? material->textureSet->GetTexturePath(slot) : nullptr;
				if (path && *path) {
					textures->SetTexturePath(slot, path);
				}
			}
			textures->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, want.c_str());
			copy->ClearTextures();
			copy->OnLoadTextureSet(0, textures);
		}
		shader->SetMaterial(copy, true);
		shader->SetupGeometry(geometry);
		shader->FinishSetupGeometry(geometry);
		copy->~BSLightingShaderMaterialBase();
		RE::free(copy);
		a_strand.opacity = -1.0f;   // set again on the new material
		a_strand.shine = -1.0f;
	}

	void StrandManager::TickBats(float a_delta)
	{
		(void)a_delta;
		const auto& settings = Settings::Get();
		if (!settings.lordBats) {
			_lordAt.clear();
			return;
		}
		std::unordered_map<std::uint32_t, RE::NiPoint3> now;
		std::vector<RE::ActorHandle>                    burst;
		auto look = [&](const RE::ActorHandle& a_handle) {
			const auto key = a_handle.native_handle();
			if (now.contains(key)) {
				return;
			}
			const auto actor = a_handle.get();
			if (!actor || Colossal::BeastForm(actor.get()) != 3) {
				return;
			}
			const auto at = actor->GetPosition();
			now[key] = at;
			if (auto it = _lordAt.find(key); it != _lordAt.end() && it->second.GetDistance(at) > 250.0f) {
				burst.push_back(a_handle);   // gone somewhere else in an instant: bats
			}
		};
		for (const auto& strand : _strands) {
			if (strand.active && strand.breakAt < 0) {
				look(strand.pair.giver);
				look(strand.pair.receiver);
			}
		}
		_lordAt = std::move(now);
		for (const auto& handle : burst) {
			int snapped = 0;
			for (auto& strand : _strands) {
				if (strand.active && strand.breakAt < 0 && (strand.pair.giver == handle || strand.pair.receiver == handle)) {
					Snap(strand, static_cast<int>(kBones) / 2, 0.15f);
					++snapped;
				}
			}
			if (settings.debugLog && snapped > 0) {
				if (const auto actor = handle.get()) {
					logger::info("{} bursts into bats: {} strand(s) snap", actor->GetDisplayFullName(), snapped);
				}
			}
		}
	}

	void StrandManager::Retire(Strand& a_strand)
	{
		if (a_strand.root && a_strand.root->parent) {
			a_strand.root->parent->DetachChild2(a_strand.root.get());
		}
		a_strand.active = false;
		a_strand.attachedTo = nullptr;
		a_strand.top = End{};
		a_strand.bottom = End{};
	}

	void StrandManager::Update(float a_delta)
	{
		auto settingsGuard = Settings::Lock();
		const auto& settings = Settings::Get();
		if (!settings.enabled) {
			for (auto& strand : _strands) {
				if (strand.active) {
					Retire(strand);
				}
			}
			for (auto& glint : _glints) {
				RetireGlint(glint);
			}
			return;
		}
		if (a_delta <= 0.0f) {
			return;
		}
		a_delta = std::min(a_delta, 0.05f);
		const auto start = std::chrono::steady_clock::now();

		ApplyPendingPairs();
		const bool anyActive = std::ranges::any_of(_strands, [](const Strand& s) { return s.active; }) ||
		                       std::ranges::any_of(_glints, [](const Glint& g) { return g.active; });
		TickKissing(a_delta);   // 0.8.0.8 (OStim's kissing is not a pair of ours)
		TickTongues(a_delta);   // 0.8.0.8
		TickBats(a_delta);      // 0.8.0.8
		if (_pairs.empty() && _lingering.empty() && _aroused.empty() && _kisses.empty() && _tongues.empty() && !anyActive) {
			return;
		}
		if (!EnsureMesh()) {
			return;
		}

		const SurfaceSettings tribbing{ settings.tribbingRate, settings.tribbingMaxPerContact, settings.tribbingMaxPerPair,
			settings.tribbingMaxGap, settings.tribbingSpread };
		const SurfaceSettings oral{ settings.oralRate, settings.oralMaxPerContact, settings.oralMaxPerPair, settings.oralMaxGap,
			settings.oralSpread };
		const SurfaceSettings fingers{ settings.fingerRate, settings.fingerMaxPerContact, settings.fingerMaxPerPair,
			settings.fingerMaxGap, settings.fingerSpread };

		for (auto& state : _pairs) {
			const char* failure = Resolve(state.pair, state.anchors);
			const bool  resolved = failure == nullptr;
			if (!resolved && (state.resolved || !state.reported)) {
				logger::warn("{} not usable: {}", ActName(state.pair.act), failure);
				state.reported = true;
			}
			state.resolved = resolved;
			if (!resolved) {
				continue;
			}
			if (IsPenis(state.pair.act)) {
				const auto giver = state.pair.giver.get();
				const bool strap = GetShaftState(giver.get()).strapOn;
				if (strap != state.strapCached) {
					state.strapCached = strap;
					state.checked = false;
				}
			}
			if (!state.checked) {
				Check(state);
			}
			state.settle -= a_delta;
			const Act act = state.pair.act;
			if (HasShaft(act)) {
				TrackStroke(state, a_delta);
			}
			if (IsPenis(act)) {
				TrackCrotch(state, a_delta);
			} else if (act == Act::kTribbing) {
				TrackSurface(state, a_delta, tribbing);
			} else if (IsOralGiver(act)) {
				TrackSurface(state, a_delta, oral);
			} else if (IsFingers(act)) {
				TrackSurface(state, a_delta, fingers);
			}
			TickDrips(state, a_delta);
			TickPrecum(state, a_delta);   // 0.8.0.8
		}
		TickLinger(a_delta);
		TickAroused(a_delta);

		for (auto& strand : _strands) {
			if (!strand.active) {
				continue;
			}
			const auto owner = strand.owner.get();
			if (!owner || owner->Get3D(false) != strand.attachedTo || strand.root->parent != strand.attachedTo) {
				strand.root.reset();
				Retire(strand);
				continue;
			}
			auto pair = std::ranges::find_if(_pairs, [&](const PairState& s) { return s.pair == strand.pair && s.resolved; });
			const int middle = static_cast<int>(kBones) / 2 - 1;
			if (strand.kind == Kind::kDrip) {
				const bool lingering = std::ranges::any_of(_lingering, [&](const Linger& a_linger) {
					return a_linger.pair == strand.pair && a_linger.source == strand.source;
				});
				const bool aroused = strand.pair == SelfPair(strand.owner) &&
				                     (std::ranges::any_of(_aroused, [&](const Excited& a_entry) { return a_entry.who.actor == strand.owner; }) ||
				                      std::ranges::any_of(_tongues, [&](const Tongue& a_tongue) { return a_tongue.actor == strand.owner; }));   // 0.8.0.8: tongue drool
				if (pair == _pairs.end() && !lingering && !aroused) {
					Snap(strand, 1, settings.dripFadeTime);
				}
			} else if (strand.kind == Kind::kKiss) {   // 0.8.0.8: stretches, then breaks
				if (strand.age > 6.0f || strand.top.World().GetDistance(strand.bottom.World()) > strand.startLength + 7.0f * strand.attachedTo->world.scale) {
					Snap(strand, middle, 0.6f);
				}
			} else if (pair == _pairs.end()) {
				Snap(strand, middle, 0.15f);
			} else if (strand.kind == Kind::kContact) {
				if (pair->contact.engaged && strand.age > 0.3f) {
					Snap(strand, middle, 0.15f);
				}
			} else if (pair->anchors.Depth(strand.bottom.World()) > kInsideMargin) {
				Snap(strand, static_cast<int>(kBones) - 2, 0.15f);
			}
			_bodies.clear();
			_skins.clear();
			const auto gather = [&](RE::ActorHandle a_handle) {
				const auto actor = a_handle.get();
				if (!actor) {
					return;
				}
				if (const auto* grid = Body::GridOf(actor.get())) {
					_skins.push_back(grid);
					const auto extras = Body::SurfaceOf(actor.get()).extras;
					_bodies.insert(_bodies.end(), extras.begin(), extras.end());
					return;
				}
				const auto capsules = Body::Of(actor.get());
				_bodies.insert(_bodies.end(), capsules.begin(), capsules.end());
			};
			if (strand.kind == Kind::kDrip) {
				gather(strand.owner);
			} else {
				gather(strand.pair.giver);
				if (strand.pair.receiver != strand.pair.giver) {
					gather(strand.pair.receiver);
				}
			}
			const RE::NiPoint3 top = strand.top.World();
			const bool         pinned = strand.kind != Kind::kDrip && strand.bottom.node;
			const RE::NiPoint3 bottom = pinned ? strand.bottom.World() : top;
			std::erase_if(_bodies, [&](const Body::Capsule& a_capsule) {
				const float clearance = a_capsule.radius + kEndClearance;
				return Body::Closest(a_capsule, top).GetDistance(top) < clearance ||
				       (pinned && Body::Closest(a_capsule, bottom).GetDistance(bottom) < clearance);
			});
			Simulate(strand, a_delta, _bodies, _skins);
			if (strand.fade <= 0.0f) {
				Retire(strand);
				continue;
			}
			Pose(strand);
		}
		SpawnGlints(a_delta);
		UpdateGlints(a_delta);

		if (settings.debugLog) {
			_statMicros += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
			++_statFrames;
			_statTime += a_delta;
			if (_statTime >= 10.0f) {
				logger::info("last {:.0f}s: {} bridge, {} contact and {} drip strand(s) spawned, {} active now, update cost {:.1f} us/frame",
					_statTime, _statBridges, _statContacts, _statDrips, CountActive(), _statMicros / std::max(_statFrames, 1));
				if (settings.superMessy && (_statMessy[0] + _statMessy[1] + _statMessy[2]) > 0) {   // 0.8.0.9
					logger::info("super messy: {} lip drip(s), {} spit web(s), {} string(s) down the chin", _statMessy[0], _statMessy[1], _statMessy[2]);
				}
				_statMessy[0] = _statMessy[1] = _statMessy[2] = 0;
				if (_statClosest < std::numeric_limits<float>::max()) {
					logger::info("closest contact distance: {:.1f}, contact swing {:.1f}, stroke swing {:.1f}", _statClosest, _statSwing,
						_statStroke);
				}
				_statTime = 0.0f;
				_statMicros = 0.0;
				_statFrames = 0;
				_statBridges = 0;
				_statContacts = 0;
				_statDrips = 0;
				_statClosest = std::numeric_limits<float>::max();
				_statSwing = 0.0f;
				_statStroke = 0.0f;
			}
		}
	}
}
