#include "Stains.h"

#include "Settings.h"
#include "Revealing.h"
#include "Colossal.h"

#include <chrono>
#include <cmath>
#include <unordered_map>
#include <xmmintrin.h>

namespace OStrands::Stains
{
	namespace
	{
		constexpr float kUnitsPerMeter = 70.0f;
		constexpr float kGravity = 9.81f;
		constexpr std::array<const char*, 3> kTextures{ "textures\\OStrands\\decals\\stain01.dds", "textures\\OStrands\\decals\\stain02.dds",
			"textures\\OStrands\\decals\\stain03.dds" };
		constexpr const char* kNormal = "textures\\OStrands\\decals\\stain_n.dds";

		std::array<RE::BGSTextureSet*, 3> g_sets{};
		bool                              g_tried{ false };
		double                            g_window{ 0.0 };
		int                               g_inWindow{ 0 };
		int                               g_clothes{ 0 }, g_world{ 0 };

		double Clock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

		RE::BGSTextureSet* Set(int a_index)
		{
			if (!g_tried) {
				g_tried = true;
				auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSTextureSet>();
				for (std::size_t i = 0; i < g_sets.size() && factory; ++i) {
					auto* set = factory->Create();
					if (!set) {
						continue;
					}
					set->textures[0].textureName = kTextures[i];
					set->textures[1].textureName = kNormal;
					auto* decal = new RE::DecalData{};
					decal->data.decalMinWidth = decal->data.decalMinHeight = 6.0f;
					decal->data.decalMaxWidth = decal->data.decalMaxHeight = 12.0f;
					decal->data.depth = 8.0f;
					decal->data.shininess = 40.0f;
					decal->data.parallaxScale = 0.0f;
					decal->data.parallaxPasses = 0;
					// alpha-tested as well as blended (a world decal ignored blending alone and drew the whole square), and one splat
					// per texture (without kNoSubtextures the game takes a quarter of it, as vanilla decals are 2x2 sheets)
					decal->data.flags = static_cast<RE::DECAL_DATA_DATA::Flag>(RE::DECAL_DATA_DATA::Flag::kAlphaBlending | RE::DECAL_DATA_DATA::Flag::kAlphaTesting |
						RE::DECAL_DATA_DATA::Flag::kNoSubtextures);
					decal->data.color = RE::Color{ 255, 255, 255, 255 };
					set->decalData = decal;
					g_sets[i] = set;
				}
				logger::info("stains: decal texture sets {}", g_sets[0] ? "made" : "could not be made");
			}
			return g_sets[static_cast<std::size_t>(a_index) % g_sets.size()];
		}

		struct Hit
		{
			RE::NiPoint3       point;
			RE::NiPoint3       normal;
			RE::TESObjectREFR* ref{ nullptr };
			bool               actor{ false };
		};

		std::optional<Hit> Cast(RE::bhkWorld* a_world, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, RE::Actor* a_ignore)
		{
			if (!a_world) {
				return std::nullopt;
			}
			const float         scale = RE::bhkWorld::GetWorldScale();
			RE::NiPoint3        from = a_from;
			RE::BSReadLockGuard guard(a_world->worldLock);
			for (int attempt = 0; attempt < 4; ++attempt) {
				RE::bhkPickData pick{};
				pick.rayInput.from = RE::hkVector4(from * scale);
				pick.rayInput.to = RE::hkVector4(a_to * scale);
				pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraPick);
				pick.rayInput.enableShapeCollectionFilter = true;
				if (!a_world->PickObject(pick) || !pick.rayOutput.HasHit() || !pick.rayOutput.rootCollidable) {
					return std::nullopt;
				}
				const float fraction = pick.rayOutput.hitFraction;
				if (!std::isfinite(fraction) || fraction < 0.0f || fraction > 1.0f) {
					return std::nullopt;
				}
				Hit hit;
				hit.point = from + (a_to - from) * fraction;
				float n[4];
				_mm_storeu_ps(n, pick.rayOutput.normal.quad);
				hit.normal = { n[0], n[1], n[2] };
				hit.ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
				auto* actor = hit.ref ? hit.ref->As<RE::Actor>() : nullptr;
				if (actor && actor == a_ignore) {   // through the giver himself
					from = hit.point + (a_to - from) * 0.02f;
					continue;
				}
				hit.actor = actor != nullptr;
				return hit;
			}
			return std::nullopt;
		}

		void Decal(const Hit& a_hit, const std::array<float, 3>& a_colour, float a_size, bool a_skinned)
		{
			auto* manager = RE::BGSDecalManager::GetSingleton();
			auto* node = a_hit.ref ? a_hit.ref->Get3D() : nullptr;
			if (!manager || !node) {
				return;
			}
			auto* set = Set(static_cast<int>(Clock() * 7.0) % 3);
			if (!set) {
				return;
			}
			RE::DECAL_CREATION_DATA data{};
			data.origin = a_hit.point;
			data.direction = a_hit.normal;
			data.surfaceNormal = a_hit.normal;
			data.objRef = a_hit.ref->CreateRefHandle();
			data.avObj = RE::NiPointer<RE::NiAVObject>(node);
			data.texSet = set;
			data.width = a_size;
			data.height = a_size;
			data.depth = a_skinned ? 6.0f : 10.0f;
			data.rotation = RE::NiMatrix3{};
			data.parentCell = a_hit.ref->GetParentCell();
			data.shininess = 40.0f;
			data.colorR = a_colour[0];
			data.colorG = a_colour[1];
			data.colorB = a_colour[2];
			data.alphaBlending = 1;
			data.alphaTesting = 1;   // 0.8.0.8: the floor drew whole squares without it
			manager->ApplyDecal(data, true, nullptr);
		}
	}

	void Spurt(RE::Actor* a_giver, const RE::NiPoint3& a_from, const RE::NiPoint3& a_direction, float a_speed, const std::array<float, 3>& a_colour, float a_amount)
	{
		const auto& settings = Settings::Get();
		if (!settings.stains || (!settings.stainClothes && !settings.stainWorld) || !a_giver) {
			return;
		}
		const double now = Clock();
		if (now - g_window > 1.0) {
			g_window = now;
			g_inWindow = 0;
		}
		if (g_inWindow >= settings.stainsPerSecond) {
			return;
		}
		auto* cell = a_giver->GetParentCell();
		auto* world = cell ? cell->GetbhkWorld() : nullptr;
		// the spurt's flight: speed along its direction, gravity pulling it down (units per second)
		const float  arc = std::max(settings.stainArc, 0.1f);
		RE::NiPoint3 velocity = a_direction * (a_speed * kUnitsPerMeter * arc);
		RE::NiPoint3 at = a_from;
		constexpr float kStep = 0.035f;
		for (int i = 0; i < 40; ++i) {
			RE::NiPoint3 next = at + velocity * kStep;
			velocity.z -= kGravity * kUnitsPerMeter * kStep;
			if (auto hit = Cast(world, at, next, a_giver)) {
				const float size = std::clamp(5.0f + 6.0f * a_amount, 4.0f, 18.0f) * settings.stainSize;
				if (hit->actor) {
					auto* target = hit->ref->As<RE::Actor>();
					if (settings.stainClothes && target && !Revealing::Exposed(target)) {   // dressed: a stain on the clothes
						Decal(*hit, a_colour, size * 0.8f, true);
						++g_clothes;
						++g_inWindow;
					}
				} else if (settings.stainWorld) {
					Decal(*hit, a_colour, size, false);
					++g_world;
					++g_inWindow;
				}
				return;
			}
			at = next;
		}
	}

	namespace
	{
		bool Roofed(const RE::NiPoint3& a_point, RE::TESObjectCELL* a_cell)   // under a roof / an overhang (asked every 3 s a spot)
		{
			static std::unordered_map<std::uint64_t, std::pair<double, bool>> cache;
			const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::int32_t>(a_point.x / 64.0f)) << 32) ^ static_cast<std::uint32_t>(static_cast<std::int32_t>(a_point.y / 64.0f));
			auto&               entry = cache[key];
			if (Clock() - entry.first > 3.0) {
				entry.first = Clock();
				entry.second = Cast(a_cell->GetbhkWorld(), a_point + RE::NiPoint3{ 0.0f, 0.0f, 10.0f }, a_point + RE::NiPoint3{ 0.0f, 0.0f, 2000.0f }, nullptr).has_value();
			}
			return entry.second;
		}
	}

	float LordRate(const RE::NiPoint3& a_point, RE::TESObjectCELL* a_cell, const std::vector<RE::ActorHandle>& a_hosts)
	{
		const auto& settings = Settings::Get();
		if (!settings.lordCold && settings.lordSun <= 1.0f) {
			return 1.0f;
		}
		const bool lord = std::ranges::any_of(a_hosts, [](const RE::ActorHandle& h) {
			const auto actor = h.get();
			return actor && Colossal::BeastForm(actor.get()) == 3;
		});
		if (!lord) {
			return 1.0f;
		}
		float rate = settings.lordCold ? 0.7f : 1.0f;   // cold: it stays wet longer
		auto* sky = RE::Sky::GetSingleton();
		auto* calendar = RE::Calendar::GetSingleton();
		if (settings.lordSun > 1.0f && a_cell && !a_cell->IsInteriorCell() && sky && sky->currentWeather && calendar) {
			const float hour = calendar->GetHour();
			const auto  flags = sky->currentWeather->data.flags;
			const bool  sunny = hour > 6.5f && hour < 19.5f && !flags.any(RE::TESWeather::WeatherDataFlag::kRainy, RE::TESWeather::WeatherDataFlag::kSnow) &&
			                   !flags.any(RE::TESWeather::WeatherDataFlag::kCloudy);
			if (sunny && !Roofed(a_point, a_cell)) {
				rate = settings.lordSun;
			}
		}
		return rate;
	}

	float WashRate(const RE::NiPoint3& a_point, RE::TESObjectCELL* a_cell, const std::vector<RE::ActorHandle>& a_hosts)
	{
		const auto& settings = Settings::Get();
		if (!settings.weather) {
			return 1.0f;
		}
		for (const auto& handle : a_hosts) {   // standing in water
			if (const auto actor = handle.get(); actor && actor->AsActorState() && actor->AsActorState()->IsSwimming()) {
				return settings.waterWash;
			}
		}
		auto* sky = RE::Sky::GetSingleton();
		if (!a_cell || a_cell->IsInteriorCell() || !sky || !sky->currentWeather ||
			!sky->currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
			return 1.0f;
		}
		if (settings.shelter && Roofed(a_point, a_cell)) {   // under a roof / an overhang: dry
			return 1.0f;
		}
		return settings.rainWash;
	}

	std::string Status() { return std::format("stains: {} on clothes, {} splashes", g_clothes, g_world); }
}
