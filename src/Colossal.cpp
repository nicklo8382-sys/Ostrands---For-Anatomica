#include "Colossal.h"

#include <algorithm>
#include <cmath>

#include "AnatomicaBridge.h"
#include "Anatomy.h"

namespace OStrands::Colossal
{
	bool MncLoaded()
	{
		static int state = -1;
		if (state < 0) {
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return false;
			}
			state = data->LookupModByName("MoreNastyCritters.esp") ? 1 : 0;
		}
		return state == 1;
	}

	Kind Of(RE::Actor* a_giver, RE::Actor* a_receiver)
	{
		Kind kind;
		if (!a_giver) {
			return kind;
		}
		if (AnatomicaBridge::LoadKind(a_giver, a_receiver, kind)) {
			return kind;
		}
		const auto& settings = Settings::Get();
		kind.colossal = settings.creatureSemen && MncLoaded() && Anatomy::IsCreature(a_giver);
		kind.weight = kind.colossal ? 1.0f : 0.0f;
		return kind;
	}

	SemenProfile Profile(const Kind& a_kind)
	{
		const auto& settings = Settings::Get();
		if (!a_kind.linked) {
			return settings.Semen(a_kind.colossal);
		}
		auto p = settings.Semen(false);
		p.amount = std::clamp(p.amount * a_kind.amount, Settings::kAmountMin, Settings::kAmountMax * 3.0f);
		p.strength = std::clamp(p.strength * a_kind.strength, Settings::kStrengthMin, Settings::kStrengthMax * 2.0f);
		p.pulses = std::clamp(static_cast<int>(std::lround(p.pulses * a_kind.pulses)), Settings::kPulsesMin, Settings::kPulsesMax * 2);
		p.viscosity = std::clamp(p.viscosity * a_kind.sticky, Settings::kViscosityMin, Settings::kViscosityMax);
		p.cohesion = std::clamp(p.cohesion * std::sqrt(std::max(a_kind.sticky, 0.01f)), Settings::kCohesionMin, Settings::kCohesionMax);
		p.stretch = std::clamp(p.stretch * a_kind.stretch, Settings::kStretchMin, Settings::kStretchMax);
		if (a_kind.hasColour) {
			p.colour = a_kind.colour;
		}
		return p;
	}

	float StrandRadius(const Kind& a_kind)
	{
		const auto& settings = Settings::Get();
		return a_kind.linked ? std::clamp(settings.StrandRadius(false) * a_kind.thick, Settings::kStrandRadiusMin * 0.5f, Settings::kStrandRadiusMax) :
		                       settings.StrandRadius(a_kind.colossal);
	}

	float CrotchRadius(const Kind& a_kind)
	{
		const auto& settings = Settings::Get();
		return a_kind.linked ? std::clamp(settings.CrotchRadius(false) * a_kind.thick, Settings::kStrandRadiusMin * 0.5f, Settings::kStrandRadiusMax) :
		                       settings.CrotchRadius(a_kind.colossal);
	}

	bool OwnMinRadius(const Kind& a_kind) { return !a_kind.linked && a_kind.colossal; }

	float Size(const Kind& a_kind)
	{
		if (a_kind.linked) {
			return std::clamp(a_kind.thick, 0.5f, 3.0f);
		}
		const auto& settings = Settings::Get();
		return a_kind.colossal && settings.creatureShape ? std::clamp(settings.creatureRadius / std::max(settings.radius, 0.01f), 0.5f, 3.0f) : 1.0f;
	}
}
