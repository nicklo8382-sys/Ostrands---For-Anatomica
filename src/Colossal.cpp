#include "Colossal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>

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

	int BeastForm(RE::Actor* a_actor)
	{
		auto* race = a_actor ? a_actor->GetRace() : nullptr;
		if (!race) {
			return 0;
		}
		static RE::TESRace* werewolf = nullptr;
		static RE::TESRace* werebear = nullptr;
		static RE::TESRace* lord = nullptr;
		static bool         looked = false;
		if (!looked) {
			if (auto* data = RE::TESDataHandler::GetSingleton()) {
				looked = true;
				werewolf = data->LookupForm<RE::TESRace>(0x0CDD84, "Skyrim.esm");
				werebear = data->LookupForm<RE::TESRace>(0x01E17B, "Dragonborn.esm");
				lord = data->LookupForm<RE::TESRace>(0x00283A, "Dawnguard.esm");
			}
		}
		if (race == werewolf) {
			return 1;
		}
		if (werebear && race == werebear) {
			return 2;
		}
		if (lord && race == lord) {
			return 3;
		}
		std::string name = race->GetFullName() ? race->GetFullName() : "";
		std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (name.find("werewolf") != std::string::npos) {
			return 1;
		}
		if (name.find("werebear") != std::string::npos) {
			return 2;
		}
		if (name.find("vampire lord") != std::string::npos) {
			return 3;
		}
		return 0;
	}

	bool Levitating(RE::Actor* a_actor)
	{
		if (!a_actor || BeastForm(a_actor) != 3) {
			return false;
		}
		bool hover = false;
		if ((a_actor->GetGraphVariableBool("IsLevitating", hover) && hover) || (a_actor->GetGraphVariableBool("bIsLevitating", hover) && hover)) {
			return true;
		}
		// a Vampire Lord levitates with his spells drawn and walks with his claws: drawn + a spell in the right hand
		auto* state = a_actor->AsActorState();
		auto* right = a_actor->GetEquippedObject(false);
		return state && state->IsWeaponDrawn() && right && right->Is(RE::FormType::Spell);
	}

	bool BeastPerson(RE::Actor* a_actor)
	{
		return Settings::Get().beastPeople && BeastForm(a_actor) != 0;
	}

	namespace
	{
		std::atomic<int>    g_bigLoads{ -1 };   // -1 not read yet / no Beast Form, 0 off, 1 on
		std::atomic<double> g_bigAt{ -100.0 };
		std::atomic<bool>   g_bigAsked{ false };

		double Clock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

		void ReadBigLoads()   // main thread (an SKSE task)
		{
			int result = -1;
			auto* data = RE::TESDataHandler::GetSingleton();
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			RE::TESQuest* quest = nullptr;
			if (data && vm) {
				for (const char* plugin : { "OStim_Beast_Form_Merged.esp", "OStim_Beast_Form.esp" }) {
					if (data->LookupModByName(plugin)) {
						quest = data->LookupForm<RE::TESQuest>(0x822, plugin);
						if (quest) {
							break;
						}
					}
				}
			}
			if (quest) {
				auto* policy = vm->GetObjectHandlePolicy();
				const auto handle = policy ? policy->GetHandleForObject(RE::TESQuest::FORMTYPE, quest) : 0;
				RE::BSTSmartPointer<RE::BSScript::Object> object;
				if (handle && vm->FindBoundObject(handle, "OCumBeastPie", object) && object) {
					const auto* huge = object->GetProperty("HugeLoad");
					const auto* load = object->GetProperty("BeastLoadEnabled");
					result = (huge && huge->GetBool() && (!load || load->GetBool())) ? 1 : 0;
				}
			}
			const int before = g_bigLoads.exchange(result);
			if (before != result && result >= 0) {
				logger::info("OStim Beast Form: Big loads {}", result == 1 ? "on - werewolves and werebears get a Colossal Load" : "off");
			}
			g_bigAt = Clock();
			g_bigAsked = false;
		}
	}

	bool BeastBigLoads()
	{
		if (Clock() - g_bigAt > 10.0 && !g_bigAsked.exchange(true)) {
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([]() { ReadBigLoads(); });
			} else {
				g_bigAsked = false;
			}
		}
		return g_bigLoads == 1;
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
		// 0.8.0.8: OStim Beast Form's "Big loads" - werewolves and werebears (Vampire Lords stay normal, as in Beast Form)
		if (!kind.colossal && settings.creatureSemen && settings.beastBigLoads) {
			const int form = BeastForm(a_giver);
			if ((form == 1 || form == 2) && BeastBigLoads()) {
				kind.colossal = true;
			}
		}
		// 0.8.0.8: Vampire Lords get their own big load (on its own toggle, Beast Form's setting or not)
		if (!kind.colossal && settings.creatureSemen && settings.lordBigLoads && BeastForm(a_giver) == 3) {
			kind.colossal = true;
		}
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
