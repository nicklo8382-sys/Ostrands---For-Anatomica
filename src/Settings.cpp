#include "Settings.h"
#include "AtomicFile.h"

#include <SimpleIni.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>

namespace OStrands
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/OStrands.ini";
		constexpr const char* kUserPath = "Data/SKSE/Plugins/OStrands_User.ini";

		bool ReadIni(CSimpleIniA& ini, const char* a_path)
		{
			if (ini.LoadFile(a_path) >= 0) {
				return true;
			}
			if (std::filesystem::exists(a_path)) {
				throw std::runtime_error(std::format("Cannot read the existing {}", a_path));
			}
			return false;
		}

		bool Same(float a_value, float a_default)
		{
			return std::abs(a_value - a_default) <= 1e-5f * std::max(1.0f, std::abs(a_default));
		}

		template <class T>
		void Merge(std::vector<std::pair<std::string, T>>& a_list, std::string a_key, T a_value)
		{
			auto entry = std::ranges::find_if(a_list, [&](const auto& a_entry) { return a_entry.first == a_key; });
			if (entry != a_list.end()) {
				entry->second = std::move(a_value);
			} else {
				a_list.emplace_back(std::move(a_key), std::move(a_value));
			}
		}

		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.front()))) {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.back()))) {
				a_text.remove_suffix(1);
			}
			return a_text;
		}

		std::vector<std::string_view> Split(std::string_view a_text, char a_separator)
		{
			std::vector<std::string_view> parts;
			for (;;) {
				const auto at = a_text.find(a_separator);
				parts.push_back(Trim(a_text.substr(0, at)));
				if (at == std::string_view::npos) {
					return parts;
				}
				a_text.remove_prefix(at + 1);
			}
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		std::optional<std::array<float, 3>> ParseColour(std::string_view a_text)
		{
			const auto parts = Split(a_text, ',');
			if (parts.size() != 3) {
				return std::nullopt;
			}
			std::array<float, 3> colour{};
			for (std::size_t c = 0; c < 3; ++c) {
				const std::string text(parts[c]);
				char*             end = nullptr;
				const float       value = std::strtof(text.c_str(), &end);
				if (end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
					return std::nullopt;
				}
				colour[c] = std::clamp(value, 0.0f, 255.0f) / 255.0f;
			}
			return colour;
		}

		std::string FormatColour(const std::array<float, 3>& a_colour)
		{
			const auto byte = [](float a_value) { return static_cast<int>(std::lround(std::clamp(a_value, 0.0f, 1.0f) * 255.0f)); };
			return std::format("{}, {}, {}", byte(a_colour[0]), byte(a_colour[1]), byte(a_colour[2]));
		}

		std::optional<ShaftRig> ParseRig(std::string a_name, std::string_view a_value)
		{
			const auto parts = Split(a_value, '|');
			if (parts.empty() || parts[0].empty() || Lower(std::string(parts[0])) == "none") {
				return std::nullopt;
			}
			ShaftRig rig{ std::move(a_name) };
			for (const auto bone : Split(parts[0], ',')) {
				if (!bone.empty()) {
					rig.bones.emplace_back(bone);
				}
			}
			if (rig.bones.size() < 2) {
				logger::warn("[CreatureRigs] {} needs at least two bones from base to tip; ignoring it", rig.name);
				return std::nullopt;
			}
			if (parts.size() > 1 && !parts[1].empty()) {
				const std::string axis = Lower(std::string(parts[1]));
				if (axis == "x" || axis == "y" || axis == "z") {
					rig.axis = axis[0] - 'x';
				} else {
					logger::warn("[CreatureRigs] {}: forward axis should be x, y or z; using x", rig.name);
				}
			}
			const auto number = [&](std::size_t a_index, float& a_value, float a_min, float a_max) {
				if (parts.size() <= a_index || parts[a_index].empty()) {
					return;
				}
				const std::string text(parts[a_index]);
				char*             end = nullptr;
				const float       parsed = std::strtof(text.c_str(), &end);
				if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
					logger::warn("[CreatureRigs] {}: '{}' is not a number; using {}", rig.name, text, a_value);
					return;
				}
				a_value = std::clamp(parsed, a_min, a_max);
			};
			number(2, rig.tipExtension, -5.0f, 20.0f);
			number(3, rig.radius, 0.0f, 10.0f);
			return rig;
		}
	}

	std::vector<ShaftRig> Settings::DefaultRigs()
	{
		const auto chain = [](const char* a_format, int a_first, int a_last) {
			std::vector<std::string> bones;
			for (int i = a_first; i <= a_last; ++i) {
				bones.push_back(std::vformat(a_format, std::make_format_args(i)));
			}
			return bones;
		};
		const auto with = [](std::string a_first, std::vector<std::string> a_rest) {
			a_rest.insert(a_rest.begin(), std::move(a_first));
			return a_rest;
		};
		return {
			{ "bear", chain("BearD {}", 4, 9), 0, 0.1f, 1.1f },
			{ "boar", chain("BoarDick{:02}", 1, 6), 1, 0.1f, 1.1f },
			{ "canine", with("CDmiddle 3", chain("CDPenis {}", 1, 7)), 0, 0.1f, 1.3f },
			{ "chaurus", chain("CO {}", 2, 9), 0, 0.1f, 1.1f },
			{ "deer", chain("ElkD{:02}", 3, 6), 0, 0.1f, 1.1f },
			{ "draugr", chain("DD {}", 2, 6), 0, 0.1f, 0.9f },
			{ "falmer", chain("FD {}", 2, 7), 0, 0.1f, 1.1f },
			{ "gargoyle", chain("GD {}", 2, 7), 0, 0.1f, 1.1f },
			{ "giant", chain("GS {}", 2, 7), 0, 0.1f, 2.4f },
			{ "goat", chain("Goat_Penis{:02}", 2, 6), 0, 0.1f, 0.6f },
			{ "horker", chain("Horker_Penis{:02}", 2, 10), 0, 0.1f, 1.7f },
			{ "horse", chain("HS {}", 3, 7), 0, 0.1f, 1.7f },
			{ "mammoth", chain("Mammoth Gen{:02}", 5, 10), 0, 0.1f, 3.3f },
			{ "riekling", with("RD Base", chain("RD {}", 2, 5)), 0, 0.1f, 1.0f },
			{ "sabrecat", chain("SCD {}", 2, 7), 0, 0.1f, 1.1f },
			{ "skeever", chain("SkeeverD {:02}", 1, 7), 0, 0.1f, 1.3f },
			{ "troll", chain("TD {}", 2, 7), 0, 0.1f, 1.1f },
			{ "vampirelord", chain("VLDick{:02}", 2, 6), 0, 0.1f, 1.1f },
			{ "werewolf", chain("WWD {}", 2, 9), 0, 0.1f, 1.4f },
		};
	}

	SemenProfile Settings::Semen(bool a_creature) const
	{
		if (a_creature && creatureSemen) {
			return { creatureAmount, creatureStrength, creaturePulses, creatureCohesion, creatureViscosity, creatureStretch, creatureColour };
		}
		return { semenAmount, semenStrength, semenPulses, semenCohesion, semenViscosity, semenStretch, semenColour };
	}

	std::unique_lock<std::recursive_mutex> Settings::Lock()
	{
		static std::recursive_mutex mutex;
		return std::unique_lock(mutex);
	}

	Settings& Settings::Get()
	{
		static Settings settings;
		return settings;
	}

	const Settings& Settings::Defaults()
	{
		static const Settings defaults;
		return defaults;
	}

	bool Settings::Save() const
	{
		auto guard = Lock();
		try {
			CSimpleIniA ini;
			ini.SetUnicode();
			const bool existed = ReadIni(ini, kUserPath);
			const auto& d = Defaults();
			bool        changed = false;

			auto keep = [&](const char* section, const char* key, bool differs, const std::string& value) {
				if (!differs) {
					ini.Delete(section, key, true);
					return;
				}
				if (ini.SetValue(section, key, value.c_str()) < 0) {
					throw std::runtime_error("Cannot serialize settings");
				}
				changed = true;
			};
			auto b = [&](const char* section, const char* key, bool Settings::* field) {
				keep(section, key, this->*field != d.*field, this->*field ? "true" : "false");
			};
			auto f = [&](const char* section, const char* key, float Settings::* field) {
				keep(section, key, !Same(this->*field, d.*field), std::format("{}", this->*field));
			};
			auto i = [&](const char* section, const char* key, int Settings::* field) {
				keep(section, key, this->*field != d.*field, std::to_string(this->*field));
			};

			b("General", "Enabled", &Settings::enabled);
			b("General", "DebugLog", &Settings::debugLog);
			b("Creatures", "Enabled", &Settings::creatures);
			b("General", "NoVaginaUsesAnus", &Settings::noVaginaUsesAnus);
			auto colour = [&](const char* section, const char* key, std::array<float, 3> Settings::* field) {
				keep(section, key, FormatColour(this->*field) != FormatColour(d.*field), FormatColour(this->*field));
			};
			ini.Delete("CreatureSemen", nullptr, true);   // 0.8.0.4: renamed [ColossalLoad] (read once from the old names, saved under the new)
			ini.Delete("CreatureShape", nullptr, true);
			b("ColossalLoad", "Enabled", &Settings::creatureSemen);
			f("ColossalLoad", "Amount", &Settings::creatureAmount);
			f("ColossalLoad", "Strength", &Settings::creatureStrength);
			i("ColossalLoad", "Pulses", &Settings::creaturePulses);
			f("ColossalLoad", "Cohesion", &Settings::creatureCohesion);
			f("ColossalLoad", "Viscosity", &Settings::creatureViscosity);
			f("ColossalLoad", "Stretch", &Settings::creatureStretch);
			colour("ColossalLoad", "Colour", &Settings::creatureColour);
			b("ColossalLoad", "Shape", &Settings::creatureShape);
			f("ColossalLoad", "Radius", &Settings::creatureRadius);
			f("ColossalLoad", "MinRadius", &Settings::creatureMinRadius);
			f("ColossalLoad", "CrotchRadius", &Settings::creatureCrotchRadius);
			f("ColossalLoad", "CrotchMinRadius", &Settings::creatureCrotchMinRadius);
			colour("Semen", "Colour", &Settings::semenColour);
			b("Semen", "Glow", &Settings::semenGlow);
			b("Semen", "GlowCumColour", &Settings::glowCumColour);
			colour("Semen", "GlowColour", &Settings::glowColour);
			f("Semen", "GlowStrength", &Settings::glowStrength);
			i("Spawn", "PullStrings", &Settings::pullStrings);
			f("Shape", "Beads", &Settings::beads);
			f("Semen", "Splats", &Settings::splats);
			b("ColossalLoad", "Cords", &Settings::colossalCords);
			b("Anatomica", "Enabled", &Settings::anatomica);
			b("Anatomica", "Hole", &Settings::anatomicaHole);
			b("Anatomica", "HandOver", &Settings::anatomicaHandOver);
			b("Anatomica", "Colour", &Settings::anatomicaColour);
			b("Anatomica", "Load", &Settings::anatomicaLoad);
			b("Anatomica", "Hold", &Settings::anatomicaHold);
			f("Anatomica", "Overflow", &Settings::anatomicaOverflow);
			f("Anatomica", "OutStickiness", &Settings::anatomicaOutStickiness);
			f("Anatomica", "OutViscosity", &Settings::anatomicaOutViscosity);
			f("Shape", "Radius", &Settings::radius);
			f("Shape", "MinRadius", &Settings::minRadius);
			f("Crotch", "Radius", &Settings::crotchRadius);
			f("Crotch", "MinRadius", &Settings::crotchMinRadius);
			f("Spawn", "Rate", &Settings::bridgeRate);
			f("Shape", "Transparency", &Settings::transparency);
			f("Shape", "Shine", &Settings::shine);
			f("Shape", "Gloss", &Settings::gloss);
			b("Glints", "Enabled", &Settings::glints);
			f("Glints", "Rate", &Settings::glintRate);
			f("Glints", "Size", &Settings::glintSize);
			f("Glints", "Brightness", &Settings::glintBrightness);
			i("Spawn", "MaxTotal", &Settings::maxTotal);
			b("Crotch", "Enabled", &Settings::crotch);
			f("Crotch", "Rate", &Settings::crotchRate);
			b("Tribbing", "Enabled", &Settings::tribbing);
			b("Oral", "Enabled", &Settings::oral);
			f("Oral", "Swallow", &Settings::swallow);
			f("Oral", "Dribble", &Settings::dribble);
			f("Oral", "Cough", &Settings::cough);
			f("Oral", "Spit", &Settings::spit);
			b("Oral", "SpitShaft", &Settings::spitShaft);
			f("Oral", "DroolTime", &Settings::droolTime);
			b("Oral", "NoseDrip", &Settings::noseDrip);
			f("Oral", "FacialChance", &Settings::facialChance);
			b("Oral", "KissStrings", &Settings::kissStrings);
			b("Oral", "Snowball", &Settings::snowball);
			b("BeastForm", "People", &Settings::beastPeople);
			b("BeastForm", "BigLoads", &Settings::beastBigLoads);
			b("BeastForm", "KnotStrings", &Settings::knotStrings);
			f("BeastForm", "Mouth", &Settings::beastMouth);
			b("BeastForm", "FurCling", &Settings::furCling);
			b("BeastForm", "KnotHold", &Settings::knotHold);
			b("BeastForm", "TongueDrool", &Settings::tongueDrool);
			b("BeastForm", "VampireLordBigLoads", &Settings::lordBigLoads);
			b("BeastForm", "VampireLordDrool", &Settings::lordDrool);
			b("BeastForm", "VampireLordBlood", &Settings::lordBlood);
			colour("BeastForm", "VampireLordBloodColour", &Settings::lordBloodColour);
			f("BeastForm", "VampireLordBloodMix", &Settings::lordBloodMix);
			b("BeastForm", "VampireLordCold", &Settings::lordCold);
			b("BeastForm", "VampireLordHover", &Settings::lordHover);
			b("BeastForm", "VampireLordBats", &Settings::lordBats);
			b("BeastForm", "VampireLordBloodDrool", &Settings::lordBloodDrool);
			b("BeastForm", "VampireLordFangs", &Settings::lordFangs);
			f("BeastForm", "VampireLordSun", &Settings::lordSun);
			b("Stains", "Enabled", &Settings::stains);
			b("Stains", "Clothes", &Settings::stainClothes);
			b("Stains", "World", &Settings::stainWorld);
			f("Stains", "Size", &Settings::stainSize);
			i("Stains", "PerSecond", &Settings::stainsPerSecond);
			f("Stains", "Arc", &Settings::stainArc);
			b("Weather", "Enabled", &Settings::weather);
			f("Weather", "Rain", &Settings::rainWash);
			f("Weather", "Water", &Settings::waterWash);
			b("Weather", "Shelter", &Settings::shelter);
			f("Oral", "NoseChance", &Settings::noseChance);
			f("Oral", "NoseDeep", &Settings::noseDeep);
			f("Oral", "NoseAmount", &Settings::noseAmount);
			b("Precum", "Enabled", &Settings::precum);
			f("Precum", "From", &Settings::precumFrom);
			f("Precum", "Rate", &Settings::precumRate);
			b("Precum", "Strings", &Settings::precumStrings);
			b("Fingering", "Enabled", &Settings::fingering);
			b("Toys", "Enabled", &Settings::toys);
			b("Drips", "Enabled", &Settings::drips);
			b("Drips", "StrapWearer", &Settings::wearerDrips);
			f("Drips", "Interval", &Settings::dripInterval);
			f("Drips", "Linger", &Settings::dripLinger);
			f("Alignment", "VaginaOut", &Settings::vaginaOut);
			f("Alignment", "VaginaUp", &Settings::vaginaUp);
			f("Alignment", "AnusOut", &Settings::anusOut);
			f("Alignment", "AnusUp", &Settings::anusUp);
			f("Alignment", "MouthOut", &Settings::mouthOut);
			f("Alignment", "MouthUp", &Settings::mouthUp);
			b("Arousal", "Enabled", &Settings::arousal);
			b("Arousal", "Wetness", &Settings::arousalWetness);
			b("Arousal", "Precum", &Settings::arousalPrecum);
			f("Arousal", "Threshold", &Settings::arousalThreshold);
			f("Arousal", "Interval", &Settings::arousalInterval);
			f("Arousal", "Radius", &Settings::arousalRadius);
			keep("Semen", "Enabled", semen != d.semen, semen < 0 ? "auto" : semen > 0 ? "true" : "false");
			f("Semen", "Amount", &Settings::semenAmount);
			f("Semen", "Strength", &Settings::semenStrength);
			i("Semen", "Pulses", &Settings::semenPulses);
			f("Semen", "Opacity", &Settings::semenOpacity);
			f("Semen", "Cohesion", &Settings::semenCohesion);
			f("Semen", "Viscosity", &Settings::semenViscosity);
			f("Semen", "Stretch", &Settings::semenStretch);
			f("Semen", "Stickiness", &Settings::semenStickiness);
			f("Semen", "Overflow", &Settings::semenOverflow);
			f("Semen", "OCumWait", &Settings::ocumWait);
			b("Collision", "Enabled", &Settings::collision);
			f("Collision", "Thickness", &Settings::collisionThickness);
			b("Collision", "Show", &Settings::showShapes);
			f("Fluid", "Lifetime", &Settings::fluidLifetime);
			f("Fluid", "FadeTime", &Settings::fluidFade);
			i("Fluid", "MaxParticles", &Settings::fluidMaxParticles);
			i("Fluid", "TestKey", &Settings::fluidTestKey);
			i("Drips", "RevealingKey", &Settings::revealingKey);
			ini.Delete("RevealingArmor", nullptr, true);
			for (const auto& [id, name] : revealingArmor) {
				if (ini.SetValue("RevealingArmor", id.c_str(), name.c_str()) < 0) {
					throw std::runtime_error("Cannot serialize settings");
				}
				changed = true;
			}

			if (!existed && !changed) {
				return true;
			}
			std::string serialized;
			if (ini.Save(serialized, false) < 0) {
				throw std::runtime_error("Cannot serialize settings");
			}
			WriteFileAtomically(kUserPath, serialized);
			if (!existed) {
				logger::info("your changed settings are now kept in OStrands_User.ini");
			}
			return true;
		} catch (const std::exception& error) {
			logger::warn("could not save {}: {} (previous file preserved)", kUserPath, error.what());
			return false;
		}
	}

	bool Settings::Load()
	{
		auto guard = Lock();
		try {
			Settings replacement = Defaults();
			if (!replacement.ReadFile(kPath)) {
				logger::info("OStrands.ini not found; using built-in defaults");
			}
			const bool custom = replacement.ReadFile(kUserPath);
			replacement.Validate();
			*this = std::move(replacement);
			Report();
			if (custom) {
				logger::info("loaded your changes from OStrands_User.ini");
			} else {
				Save();
			}
			return true;
		} catch (const std::exception& error) {
			logger::warn("could not reload settings: {} (current settings preserved)", error.what());
			return false;
		}
	}

	void Settings::Report() const
	{
		for (const auto& [path, value] : shaftRadiusOverrides) {
			logger::info("shaft radius {} for meshes matching {}", value, path);
		}
		logger::info("settings: bridges {}/s; crotch {}; tribbing {}; oral {}; fingering {}; drips {} every ~{}s; max {} total",
			bridgeRate, crotch, tribbing, oral, fingering, drips, dripInterval, maxTotal);
		logger::info("semen {}, amount {}, {} pulses, lifetime {}s, test key {}", semen < 0 ? "auto" : semen ? "on" : "off", semenAmount,
			semenPulses, fluidLifetime, fluidTestKey);
		if (creatureSemen) {
			logger::info("Colossal Load (creatures, with More Nasty Critters): amount {}, {} pulses, strength {}, viscosity {}, colour {}", creatureAmount, creaturePulses, creatureStrength,
				creatureViscosity, FormatColour(creatureColour));
		}
		if (creatureShape) {
			logger::info("Colossal Load strands: radius {}, contact radius {}", creatureRadius, creatureCrotchRadius);
		}
		logger::info("semen colour {}; no vagina uses the anus: {}; Anatomica link {} (hole {}, hand over {}, colour {})", FormatColour(semenColour),
			noVaginaUsesAnus, anatomica, anatomicaHole, anatomicaHandOver, anatomicaColour);
		logger::info("arousal drips {}: wetness {}, precum {}, from arousal {}, every ~{}s at full arousal, within {} units", arousal ? "on" : "off",
			arousalWetness, arousalPrecum, arousalThreshold, arousalInterval, arousalRadius);
		std::string names;
		for (const auto& rig : rigs) {
			names += std::format("{}{}", names.empty() ? "" : ", ", rig.name);
		}
		logger::info("creatures in SexLab scenes and arousal drips {}; {} penis rigs: {}", creatures ? "on" : "off", rigs.size(), names);
	}

	void Settings::Validate()
	{
		const auto& defaults = Defaults();
		auto bounded = [](float value, float fallback, float min, float max) {
			return std::isfinite(value) ? std::clamp(value, min, max) : fallback;
		};
		bridgeRate = bounded(bridgeRate, defaults.bridgeRate, kRateMin, kRateMax);
		pullStrings = std::clamp(pullStrings, 0, 8);
		auto fin = [](float v, float lo, float hi, float d) { return std::isfinite(v) ? std::clamp(v, lo, hi) : d; };
		swallow = fin(swallow, 0.0f, 1.0f, 0.3f);
		facialChance = fin(facialChance, 0.0f, 1.0f, 0.2f);
		beastMouth = fin(beastMouth, 1.0f, 3.0f, 1.7f);
		lordBloodMix = fin(lordBloodMix, 0.0f, 1.0f, 0.5f);
		lordSun = fin(lordSun, 1.0f, 10.0f, 3.0f);
		stainSize = fin(stainSize, 0.2f, 4.0f, 1.0f);
		stainsPerSecond = std::clamp(stainsPerSecond, 1, 60);
		stainArc = fin(stainArc, 0.2f, 3.0f, 1.0f);
		rainWash = fin(rainWash, 1.0f, 20.0f, 3.0f);
		waterWash = fin(waterWash, 1.0f, 50.0f, 8.0f);
		noseChance = fin(noseChance, 0.0f, 1.0f, 0.1f);
		noseDeep = fin(noseDeep, 0.0f, 1.0f, 0.25f);
		noseAmount = fin(noseAmount, 0.1f, 4.0f, 1.0f);
		dribble = fin(dribble, 0.05f, 2.0f, 0.5f);
		cough = fin(cough, 0.0f, 1.0f, 0.08f);
		spit = fin(spit, 0.0f, 3.0f, 1.0f);
		droolTime = fin(droolTime, 0.0f, 120.0f, 20.0f);
		precumFrom = fin(precumFrom, 0.0f, 99.0f, 55.0f);
		precumRate = fin(precumRate, 0.2f, 3.0f, 1.0f);
		beads = std::isfinite(beads) ? std::clamp(beads, 0.0f, 1.0f) : 0.6f;
		splats = std::isfinite(splats) ? std::clamp(splats, 0.0f, 1.0f) : 0.5f;
		glowStrength = std::isfinite(glowStrength) ? std::clamp(glowStrength, 0.0f, 5.0f) : 1.0f;
		transparency = bounded(transparency, defaults.transparency, kTransparencyMin, kTransparencyMax);
		shine = bounded(shine, defaults.shine, kShineMin, kShineMax);
		gloss = bounded(gloss, defaults.gloss, kGlossMin, kGlossMax);
		glintRate = bounded(glintRate, defaults.glintRate, kGlintRateMin, kGlintRateMax);
		glintSize = bounded(glintSize, defaults.glintSize, kGlintSizeMin, kGlintSizeMax);
		glintBrightness = bounded(glintBrightness, defaults.glintBrightness, kGlintBrightnessMin, kGlintBrightnessMax);
		glintLife = bounded(glintLife, defaults.glintLife, 0.05f, 2.0f);
		glintMax = std::clamp(glintMax, 0, 64);
		crotchRate = bounded(crotchRate, defaults.crotchRate, kRateMin, kRateMax);
		dripInterval = bounded(dripInterval, defaults.dripInterval, kIntervalMin, kIntervalMax);
		dripLinger = bounded(dripLinger, defaults.dripLinger, kLingerMin, kLingerMax);
		arousalThreshold = bounded(arousalThreshold, defaults.arousalThreshold, kThresholdMin, kThresholdMax);
		for (auto field : { &Settings::vaginaOut, &Settings::vaginaUp, &Settings::anusOut, &Settings::anusUp, &Settings::mouthOut, &Settings::mouthUp }) {
			this->*field = bounded(this->*field, 0.0f, kAlignMin, kAlignMax);
		}
		arousalInterval = bounded(arousalInterval, defaults.arousalInterval, kArousalIntervalMin, kArousalIntervalMax);
		arousalRadius = bounded(arousalRadius, defaults.arousalRadius, kReachMin, kReachMax);
		maxTotal = std::clamp(maxTotal, kCountMin, kCountMax);
		semen = std::clamp(semen, -1, 1);
		semenAmount = bounded(semenAmount, defaults.semenAmount, kAmountMin, kAmountMax);
		semenStrength = bounded(semenStrength, defaults.semenStrength, kStrengthMin, kStrengthMax);
		semenPulses = std::clamp(semenPulses, kPulsesMin, kPulsesMax);
		semenOpacity = bounded(semenOpacity, defaults.semenOpacity, kOpacityMin, kOpacityMax);
		semenCohesion = bounded(semenCohesion, defaults.semenCohesion, kCohesionMin, kCohesionMax);
		semenViscosity = bounded(semenViscosity, defaults.semenViscosity, kViscosityMin, kViscosityMax);
		semenStretch = bounded(semenStretch, defaults.semenStretch, kStretchMin, kStretchMax);
		semenStickiness = bounded(semenStickiness, defaults.semenStickiness, kStickinessMin, kStickinessMax);
		semenOverflow = bounded(semenOverflow, defaults.semenOverflow, kOverflowMin, kOverflowMax);
		ocumWait = bounded(ocumWait, defaults.ocumWait, kWaitMin, kWaitMax);
		creatureAmount = bounded(creatureAmount, defaults.creatureAmount, kAmountMin, kAmountMax);
		creatureStrength = bounded(creatureStrength, defaults.creatureStrength, kStrengthMin, kStrengthMax);
		creaturePulses = std::clamp(creaturePulses, kPulsesMin, kPulsesMax);
		creatureCohesion = bounded(creatureCohesion, defaults.creatureCohesion, kCohesionMin, kCohesionMax);
		creatureViscosity = bounded(creatureViscosity, defaults.creatureViscosity, kViscosityMin, kViscosityMax);
		creatureStretch = bounded(creatureStretch, defaults.creatureStretch, kStretchMin, kStretchMax);
		for (auto field : { &Settings::radius, &Settings::minRadius, &Settings::crotchRadius, &Settings::crotchMinRadius, &Settings::creatureRadius,
				 &Settings::creatureMinRadius, &Settings::creatureCrotchRadius, &Settings::creatureCrotchMinRadius }) {
			this->*field = bounded(this->*field, defaults.*field, kStrandRadiusMin * 0.5f, kStrandRadiusMax);
		}
		for (auto field : { &Settings::semenColour, &Settings::creatureColour, &Settings::lordBloodColour }) {
			for (auto& channel : this->*field) {
				channel = std::isfinite(channel) ? std::clamp(channel, 0.0f, 1.0f) : 1.0f;
			}
		}
		collisionThickness = bounded(collisionThickness, defaults.collisionThickness, kThicknessMin, kThicknessMax);
		fluidLifetime = bounded(fluidLifetime, defaults.fluidLifetime, kLifetimeMin, kLifetimeMax);
		fluidFade = bounded(fluidFade, defaults.fluidFade, kFadeMin, kFadeMax);
		fluidMaxParticles = std::clamp(fluidMaxParticles, kParticlesMin, kParticlesMax);
		anatomicaOverflow = bounded(anatomicaOverflow, defaults.anatomicaOverflow, 0.0f, 1.0f);
		anatomicaOutStickiness = bounded(anatomicaOutStickiness, defaults.anatomicaOutStickiness, kStickinessMin, kStickinessMax);
		anatomicaOutViscosity = bounded(anatomicaOutViscosity, defaults.anatomicaOutViscosity, kViscosityMin, kViscosityMax);
		fluidTestKey = fluidTestKey > 0 && fluidTestKey <= kKeyMax ? fluidTestKey : 0;
		revealingKey = revealingKey > 0 && revealingKey <= kKeyMax ? revealingKey : 0;
	}

	bool Settings::ReadFile(const char* a_path)
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		if (!ReadIni(ini, a_path)) {
			return false;
		}

		auto b = [&](const char* section, const char* key, bool& value) { value = ini.GetBoolValue(section, key, value); };
		auto f = [&](const char* section, const char* key, float& value) {
			const double parsed = ini.GetDoubleValue(section, key, value);
			if (std::isfinite(parsed) && std::abs(parsed) <= std::numeric_limits<float>::max()) {
				value = static_cast<float>(parsed);
			} else {
				logger::warn("[{}] {} is not a finite float; using its default", section, key);
			}
		};
		auto i = [&](const char* section, const char* key, int& value) {
			value = static_cast<int>(ini.GetLongValue(section, key, value));
		};
		auto automatic = [&](const char* section, const char* key, int& value) {
			std::string text = ini.GetValue(section, key, "");
			std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (text == "auto") {
				value = -1;
			} else if (text == "true" || text == "1" || text == "yes" || text == "on") {
				value = 1;
			} else if (text == "false" || text == "0" || text == "no" || text == "off") {
				value = 0;
			} else if (!text.empty()) {
				logger::warn("[{}] {} should be auto, true or false; using its default", section, key);
			}
		};

		b("General", "Enabled", enabled);
		b("General", "DebugLog", debugLog);
		b("Creatures", "Enabled", creatures);
		b("General", "NoVaginaUsesAnus", noVaginaUsesAnus);
		auto colour = [&](const char* section, const char* key, std::array<float, 3>& value) {
			const char* text = ini.GetValue(section, key, nullptr);
			if (!text) {
				return;
			}
			if (auto parsed = ParseColour(text)) {
				value = *parsed;
			} else {
				logger::warn("[{}] {} should be three numbers 0-255 like 236, 233, 222; using its default", section, key);
			}
		};
		b("CreatureSemen", "Enabled", creatureSemen);
		f("CreatureSemen", "Amount", creatureAmount);
		f("CreatureSemen", "Strength", creatureStrength);
		i("CreatureSemen", "Pulses", creaturePulses);
		f("CreatureSemen", "Cohesion", creatureCohesion);
		f("CreatureSemen", "Viscosity", creatureViscosity);
		f("CreatureSemen", "Stretch", creatureStretch);
		colour("CreatureSemen", "Colour", creatureColour);
		b("CreatureShape", "Enabled", creatureShape);
		f("CreatureShape", "Radius", creatureRadius);
		f("CreatureShape", "MinRadius", creatureMinRadius);
		f("CreatureShape", "CrotchRadius", creatureCrotchRadius);
		f("CreatureShape", "CrotchMinRadius", creatureCrotchMinRadius);
		b("ColossalLoad", "Enabled", creatureSemen);   // 0.8.0.4: the new names win over the old ones above
		f("ColossalLoad", "Amount", creatureAmount);
		f("ColossalLoad", "Strength", creatureStrength);
		i("ColossalLoad", "Pulses", creaturePulses);
		f("ColossalLoad", "Cohesion", creatureCohesion);
		f("ColossalLoad", "Viscosity", creatureViscosity);
		f("ColossalLoad", "Stretch", creatureStretch);
		colour("ColossalLoad", "Colour", creatureColour);
		b("ColossalLoad", "Shape", creatureShape);
		f("ColossalLoad", "Radius", creatureRadius);
		f("ColossalLoad", "MinRadius", creatureMinRadius);
		f("ColossalLoad", "CrotchRadius", creatureCrotchRadius);
		f("ColossalLoad", "CrotchMinRadius", creatureCrotchMinRadius);
		colour("Semen", "Colour", semenColour);
		b("Semen", "Glow", semenGlow);
		b("Semen", "GlowCumColour", glowCumColour);
		colour("Semen", "GlowColour", glowColour);
		f("Semen", "GlowStrength", glowStrength);
		b("Anatomica", "Enabled", anatomica);
		b("Anatomica", "Hole", anatomicaHole);
		b("Anatomica", "HandOver", anatomicaHandOver);
		b("Anatomica", "Colour", anatomicaColour);
		b("Anatomica", "Load", anatomicaLoad);
		b("Anatomica", "Hold", anatomicaHold);
		f("Anatomica", "Overflow", anatomicaOverflow);
		f("Anatomica", "OutStickiness", anatomicaOutStickiness);
		f("Anatomica", "OutViscosity", anatomicaOutViscosity);

		CSimpleIniA::TNamesDepend rigKeys;
		ini.GetAllKeys("CreatureRigs", rigKeys);
		rigKeys.sort(CSimpleIniA::Entry::LoadOrder());
		for (const auto& key : rigKeys) {
			std::string name = Lower(std::string(Trim(key.pItem)));
			auto        existing = std::ranges::find(rigs, name, &ShaftRig::name);
			auto        rig = ParseRig(name, ini.GetValue("CreatureRigs", key.pItem, ""));
			if (!rig) {
				if (existing != rigs.end()) {
					rigs.erase(existing);
				}
			} else if (existing != rigs.end()) {
				*existing = std::move(*rig);
			} else {
				rigs.push_back(std::move(*rig));
			}
		}

		f("Spawn", "Rate", bridgeRate);
		i("Spawn", "MaxPerStroke", maxPerStroke);
		i("Spawn", "PullStrings", pullStrings);
		f("Shape", "Beads", beads);
		f("Semen", "Splats", splats);
		b("ColossalLoad", "Cords", colossalCords);
		i("Spawn", "MaxPerPair", maxPerPair);
		i("Spawn", "MaxTotal", maxTotal);
		f("Spawn", "Cooldown", cooldown);
		f("Shape", "Radius", radius);
		f("Shape", "MinRadius", minRadius);
		f("Shape", "ShaftRadius", shaftRadius);
		f("Shape", "TipExtension", tipExtension);
		f("Shape", "Transparency", transparency);
		f("Shape", "Shine", shine);
		f("Shape", "Gloss", gloss);
		b("Glints", "Enabled", glints);
		f("Glints", "Rate", glintRate);
		f("Glints", "Size", glintSize);
		f("Glints", "Brightness", glintBrightness);
		f("Glints", "Life", glintLife);
		i("Glints", "Max", glintMax);

		CSimpleIniA::TNamesDepend keys;
		ini.GetAllKeys("ShaftRadius", keys);
		for (const auto& key : keys) {
			std::string path = key.pItem;
			std::ranges::transform(path, path.begin(), [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
			float value = shaftRadius;
			f("ShaftRadius", key.pItem, value);
			Merge(shaftRadiusOverrides, std::move(path), value);
		}

		f("Physics", "Gravity", gravity);
		f("Physics", "Damping", damping);
		f("Physics", "Viscosity", viscosity);
		f("Physics", "MaxLength", maxLength);
		f("Physics", "MaxLife", maxLife);
		f("Physics", "FadeTime", fadeTime);
		b("Crotch", "Enabled", crotch);
		f("Crotch", "Rate", crotchRate);
		i("Crotch", "MaxPerContact", crotchMaxPerContact);
		i("Crotch", "MaxPerPair", crotchMaxPerPair);
		f("Crotch", "MaxGap", crotchMaxGap);
		f("Crotch", "Spread", crotchSpread);
		f("Crotch", "SkinOffset", crotchSkin);
		f("Crotch", "Radius", crotchRadius);
		f("Crotch", "MinRadius", crotchMinRadius);
		f("Crotch", "MaxLength", crotchMaxLength);
		f("Crotch", "MaxLife", crotchMaxLife);
		b("Tribbing", "Enabled", tribbing);
		f("Tribbing", "Rate", tribbingRate);
		i("Tribbing", "MaxPerContact", tribbingMaxPerContact);
		i("Tribbing", "MaxPerPair", tribbingMaxPerPair);
		f("Tribbing", "MaxGap", tribbingMaxGap);
		f("Tribbing", "Spread", tribbingSpread);
		b("Oral", "Enabled", oral);
		b("Oral", "Drips", oralDrips);
		f("Oral", "Swallow", swallow);
		f("Oral", "Dribble", dribble);
		f("Oral", "Cough", cough);
		f("Oral", "Spit", spit);
		b("Oral", "SpitShaft", spitShaft);
		f("Oral", "DroolTime", droolTime);
		b("Oral", "NoseDrip", noseDrip);
		f("Oral", "FacialChance", facialChance);
		b("Oral", "KissStrings", kissStrings);
		b("Oral", "Snowball", snowball);
		b("BeastForm", "People", beastPeople);
		b("BeastForm", "BigLoads", beastBigLoads);
		b("BeastForm", "KnotStrings", knotStrings);
		f("BeastForm", "Mouth", beastMouth);
		b("BeastForm", "FurCling", furCling);
		b("BeastForm", "KnotHold", knotHold);
		b("BeastForm", "TongueDrool", tongueDrool);
		b("BeastForm", "VampireLordBigLoads", lordBigLoads);
		b("BeastForm", "VampireLordDrool", lordDrool);
		b("BeastForm", "VampireLordBlood", lordBlood);
		colour("BeastForm", "VampireLordBloodColour", lordBloodColour);
		f("BeastForm", "VampireLordBloodMix", lordBloodMix);
		b("BeastForm", "VampireLordCold", lordCold);
		b("BeastForm", "VampireLordHover", lordHover);
		b("BeastForm", "VampireLordBats", lordBats);
		b("BeastForm", "VampireLordBloodDrool", lordBloodDrool);
		b("BeastForm", "VampireLordFangs", lordFangs);
		f("BeastForm", "VampireLordSun", lordSun);
		b("Stains", "Enabled", stains);
		b("Stains", "Clothes", stainClothes);
		b("Stains", "World", stainWorld);
		f("Stains", "Size", stainSize);
		i("Stains", "PerSecond", stainsPerSecond);
		f("Stains", "Arc", stainArc);
		b("Weather", "Enabled", weather);
		f("Weather", "Rain", rainWash);
		f("Weather", "Water", waterWash);
		b("Weather", "Shelter", shelter);
		f("Oral", "NoseChance", noseChance);
		f("Oral", "NoseDeep", noseDeep);
		f("Oral", "NoseAmount", noseAmount);
		b("Precum", "Enabled", precum);
		f("Precum", "From", precumFrom);
		f("Precum", "Rate", precumRate);
		b("Precum", "Strings", precumStrings);
		f("Oral", "MouthBlend", mouthBlend);
		f("Oral", "LipWidth", lipWidth);
		f("Oral", "LipHeight", lipHeight);
		f("Oral", "Rate", oralRate);
		i("Oral", "MaxPerContact", oralMaxPerContact);
		i("Oral", "MaxPerPair", oralMaxPerPair);
		f("Oral", "MaxGap", oralMaxGap);
		f("Oral", "Spread", oralSpread);
		b("Fingering", "Enabled", fingering);
		f("Fingering", "Radius", fingerRadius);
		f("Fingering", "TipExtension", fingerTipExtension);
		b("Fingering", "Drips", fingerDrips);
		f("Fingering", "Rate", fingerRate);
		i("Fingering", "MaxPerContact", fingerMaxPerContact);
		i("Fingering", "MaxPerPair", fingerMaxPerPair);
		f("Fingering", "MaxGap", fingerMaxGap);
		f("Fingering", "Spread", fingerSpread);
		b("Toys", "Enabled", toys);
		f("Toys", "Radius", toyRadius);
		f("Toys", "MaxRadius", toyMaxRadius);
		keys.clear();
		ini.GetAllKeys("Actions", keys);
		for (const auto& key : keys) {
			std::string id = key.pItem;
			std::string act = ini.GetValue("Actions", key.pItem, "");
			std::ranges::transform(id, id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::ranges::transform(act, act.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			Merge(actions, std::move(id), std::move(act));
		}

		b("Drips", "Enabled", drips);
		b("Drips", "StrapWearer", wearerDrips);
		f("Drips", "Interval", dripInterval);
		f("Drips", "Linger", dripLinger);
		i("Drips", "MaxPerSource", dripMaxPerSource);
		f("Drips", "Radius", dripRadius);
		f("Drips", "MinRadius", dripMinRadius);
		f("Drips", "StretchTime", dripStretchTime);
		f("Drips", "DropWeight", dripWeight);
		f("Drips", "MaxLength", dripMaxLength);
		f("Drips", "MaxLife", dripMaxLife);
		f("Drips", "FadeTime", dripFadeTime);
		i("Drips", "RevealingKey", revealingKey);
		if (const char* list = ini.GetValue("Drips", "RevealingKeywords", nullptr)) {
			revealingKeywords.clear();
			std::string_view rest = list;
			while (!rest.empty()) {
				const auto       comma = rest.find(',');
				std::string_view item = rest.substr(0, comma);
				rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
				while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) {
					item.remove_prefix(1);
				}
				while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) {
					item.remove_suffix(1);
				}
				if (!item.empty()) {
					std::string keyword(item);
					std::ranges::transform(keyword, keyword.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					revealingKeywords.push_back(std::move(keyword));
				}
			}
		}
		keys.clear();
		ini.GetAllKeys("RevealingArmor", keys);
		for (const auto& key : keys) {
			std::string id = key.pItem;
			std::ranges::transform(id, id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			Merge(revealingArmor, std::move(id), std::string(ini.GetValue("RevealingArmor", key.pItem, "")));
		}

		f("Alignment", "VaginaOut", vaginaOut);
		f("Alignment", "VaginaUp", vaginaUp);
		f("Alignment", "AnusOut", anusOut);
		f("Alignment", "AnusUp", anusUp);
		f("Alignment", "MouthOut", mouthOut);
		f("Alignment", "MouthUp", mouthUp);

		b("Arousal", "Enabled", arousal);
		b("Arousal", "Wetness", arousalWetness);
		b("Arousal", "Precum", arousalPrecum);
		f("Arousal", "Threshold", arousalThreshold);
		f("Arousal", "Interval", arousalInterval);
		f("Arousal", "Radius", arousalRadius);

		automatic("Semen", "Enabled", semen);
		f("Semen", "Amount", semenAmount);
		f("Semen", "Strength", semenStrength);
		i("Semen", "Pulses", semenPulses);
		f("Semen", "Opacity", semenOpacity);
		f("Semen", "Cohesion", semenCohesion);
		f("Semen", "Viscosity", semenViscosity);
		f("Semen", "Stretch", semenStretch);
		f("Semen", "Stickiness", semenStickiness);
		f("Semen", "Overflow", semenOverflow);
		f("Semen", "OCumWait", ocumWait);
		b("Collision", "Enabled", collision);
		f("Collision", "Thickness", collisionThickness);
		b("Collision", "Show", showShapes);
		f("Fluid", "Lifetime", fluidLifetime);
		f("Fluid", "FadeTime", fluidFade);
		i("Fluid", "MaxParticles", fluidMaxParticles);
		i("Fluid", "TestKey", fluidTestKey);
		return true;
	}
}
