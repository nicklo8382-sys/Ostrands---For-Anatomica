#include "SettingsMenu.h"

#include "AnatomicaBridge.h"
#include "Arousal.h"
#include "Body.h"
#include "FluidManager.h"
#include "OStimBridge.h"
#include "Revealing.h"
#include "Settings.h"

#include "SKSEMenuFramework.h"

#include <cmath>

namespace OStrands::SettingsMenu
{
	namespace
	{
		namespace UI = ImGuiMCP;

		using Condition = std::function<bool()>;

		void Hint(const char* a_text)
		{
			if (a_text && *a_text && UI::IsItemHovered(UI::ImGuiHoveredFlags_AllowWhenDisabled)) {
				UI::SetTooltip("%s", a_text);
			}
		}

		void Persist()
		{
			auto guard = Settings::Lock();
			Settings::Get().Save();
		}

		struct Option
		{
			Option(const char* a_name, const char* a_tooltip, Condition a_condition) :
				name(a_name), tooltip(a_tooltip), condition(std::move(a_condition))
			{}

			virtual ~Option() = default;

			void Draw()
			{
				bool enabled = true;
				if (condition) {
					auto guard = Settings::Lock();
					enabled = condition();
				}
				UI::BeginDisabled(!enabled);
				Body();
				UI::EndDisabled();
			}

			virtual void Body() = 0;

			const char* name;
			const char* tooltip;
			Condition   condition;
		};

		struct Heading : Option
		{
			explicit Heading(const char* a_name) :
				Option(a_name, nullptr, {})
			{}

			void Body() override { UI::SeparatorText(name); }
		};

		struct Note : Option
		{
			explicit Note(std::function<std::string()> a_text) :
				Option("", nullptr, {}), text(std::move(a_text))
			{}

			void Body() override { UI::TextWrapped("%s", text().c_str()); }

			std::function<std::string()> text;
		};

		struct Toggle : Option
		{
			Toggle(const char* a_name, const char* a_tooltip, bool Settings::* a_field, Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), field(a_field)
			{}

			void Body() override
			{
				bool value = false;
				{
					auto guard = Settings::Lock();
					value = Settings::Get().*field;
				}
				if (UI::Checkbox(name, &value)) {
					auto       guard = Settings::Lock();
					auto&      settings = Settings::Get();
					const bool previous = settings.*field;
					settings.*field = value;
					if (!settings.Save()) {
						settings.*field = previous;
					}
				}
				Hint(tooltip);
			}

			bool Settings::* field;
		};

		template <class T>
		struct Slider : Option
		{
			Slider(const char* a_name, const char* a_tooltip, T Settings::* a_field, float a_min, float a_max, float a_step,
				Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), field(a_field), min(a_min), max(a_max), step(a_step)
			{
				const int digits = a_step >= 1.0f ? 0 : a_step >= 0.1f ? 1 : a_step >= 0.01f ? 2 : 3;
				format = std::format("%.{}f", digits);
			}

			void Body() override
			{
				float value = 0.0f;
				{
					auto guard = Settings::Lock();
					value = static_cast<float>(Settings::Get().*field);
				}
				if (UI::SliderFloat(name, &value, min, max, format.c_str()) && std::isfinite(value)) {
					value = std::clamp(std::round(value / step) * step, min, max);
					auto  guard = Settings::Lock();
					auto& settings = Settings::Get();
					if constexpr (std::is_integral_v<T>) {
						settings.*field = static_cast<T>(std::lround(value));
					} else {
						settings.*field = static_cast<T>(value);
					}
					settings.Validate();
				}
				if (UI::IsItemDeactivatedAfterEdit()) {
					Persist();
				}
				Hint(tooltip);
			}

			T Settings::* field;
			float         min;
			float         max;
			float         step;
			std::string   format;
		};

		struct ColourPick : Option
		{
			ColourPick(const char* a_name, const char* a_tooltip, std::array<float, 3> Settings::* a_field, Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), field(a_field)
			{}

			void Body() override
			{
				std::array<float, 3> value{};
				{
					auto guard = Settings::Lock();
					value = Settings::Get().*field;
				}
				if (UI::ColorEdit3(name, value.data())) {
					auto  guard = Settings::Lock();
					auto& settings = Settings::Get();
					settings.*field = value;
					settings.Validate();
				}
				if (UI::IsItemDeactivatedAfterEdit()) {
					Persist();
				}
				Hint(tooltip);
			}

			std::array<float, 3> Settings::* field;
		};

		struct Choice : Option
		{
			Choice(const char* a_name, const char* a_tooltip, int Settings::* a_field, std::vector<std::pair<const char*, int>> a_items,
				Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), field(a_field), items(std::move(a_items))
			{
				for (const auto& item : items) {
					labels.push_back(item.first);
				}
			}

			void Body() override
			{
				int value = 0;
				{
					auto guard = Settings::Lock();
					value = Settings::Get().*field;
				}
				int index = 0;
				for (std::size_t i = 0; i < items.size(); ++i) {
					if (items[i].second == value) {
						index = static_cast<int>(i);
					}
				}
				if (UI::Combo(name, &index, labels.data(), static_cast<int>(labels.size())) && index >= 0 &&
					static_cast<std::size_t>(index) < items.size()) {
					auto      guard = Settings::Lock();
					auto&     settings = Settings::Get();
					const int previous = settings.*field;
					settings.*field = items[index].second;
					settings.Validate();
					if (!settings.Save()) {
						settings.*field = previous;
					}
				}
				Hint(tooltip);
			}

			int Settings::*                            field;
			std::vector<std::pair<const char*, int>>   items;
			std::vector<const char*>                   labels;
		};

		struct Key : Option
		{
			static constexpr int kEscape = 0x01;
			static constexpr int kBackspace = 0x0E;
			static constexpr int kDelete = 0xD3;

			Key(const char* a_name, const char* a_tooltip, int Settings::* a_field, Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), field(a_field)
			{}

			static const std::uint8_t* State()
			{
				auto* input = RE::BSInputDeviceManager::GetSingleton();
				auto* keyboard = input ? input->GetKeyboard() : nullptr;
				return keyboard ? keyboard->GetRuntimeData().curState : nullptr;
			}

			const std::string& NameOf(int a_code)
			{
				if (a_code != named) {
					named = a_code;
					RE::BSFixedString text;
					auto*             input = RE::BSInputDeviceManager::GetSingleton();
					if (a_code <= 0) {
						label = "Unbound";
					} else if (input && input->GetButtonNameFromID(RE::INPUT_DEVICE::kKeyboard, a_code, text) && !text.empty()) {
						label = text.c_str();
					} else {
						label = std::format("Key {}", a_code);
					}
				}
				return label;
			}

			int Pressed()
			{
				const auto* state = State();
				if (!state) {
					return -1;
				}
				for (std::size_t code = 1; code < held.size(); ++code) {
					const bool down = (state[code] & 0x80) != 0;
					if (down && !held[code]) {
						return static_cast<int>(code);
					}
					held[code] = held[code] && down;
				}
				return -1;
			}

			void Listen()
			{
				const auto* state = State();
				for (std::size_t code = 0; code < held.size(); ++code) {
					held[code] = state && (state[code] & 0x80) != 0;
				}
				listening = true;
			}

			void Body() override
			{
				int value = 0;
				{
					auto guard = Settings::Lock();
					value = Settings::Get().*field;
				}
				if (listening) {
					if (const int code = Pressed(); code > 0) {
						listening = false;
						if (code != kEscape) {
							auto  guard = Settings::Lock();
							auto& settings = Settings::Get();
							settings.*field = code == kBackspace || code == kDelete ? 0 : code;
							settings.Validate();
							settings.Save();
							value = settings.*field;
						}
					}
				}
				const std::string text = listening ? std::format("Press a key... (Esc cancels, Delete unbinds)###{}", name) :
				                                     std::format("{}: {}###{}", name, NameOf(value), name);
				if (UI::Button(text.c_str())) {
					if (listening) {
						listening = false;
					} else {
						Listen();
					}
				}
				Hint(tooltip);
			}

			int Settings::*        field;
			bool                   listening{ false };
			std::array<bool, 256>  held{};
			int                    named{ -1 };
			std::string            label;
		};

		struct Button : Option
		{
			Button(const char* a_name, const char* a_tooltip, std::function<void()> a_action, Condition a_condition = {}) :
				Option(a_name, a_tooltip, std::move(a_condition)), action(std::move(a_action))
			{}

			void Body() override
			{
				if (UI::Button(name)) {
					action();
				}
				Hint(tooltip);
			}

			std::function<void()> action;
		};

		using Page = std::vector<std::unique_ptr<Option>>;

		Page strands;
		Page semen;
		Page arousal;
		Page alignment;

		void Draw(Page& a_page)
		{
			for (auto& option : a_page) {
				option->Draw();
			}
		}

		void __stdcall DrawStrands() { Draw(strands); }
		void __stdcall DrawSemen() { Draw(semen); }
		void __stdcall DrawArousal() { Draw(arousal); }
		void __stdcall DrawAlignment() { Draw(alignment); }

		void BuildStrands()
		{
			auto&      page = strands;
			const auto on = [] { return Settings::Get().enabled; };
			const auto drips = [] { return Settings::Get().enabled && Settings::Get().drips; };
			const auto glints = [] { return Settings::Get().enabled && Settings::Get().glints; };
			const auto collision = [] { return Settings::Get().enabled && Settings::Get().collision; };

			page.push_back(std::make_unique<Heading>("General"));
			page.push_back(std::make_unique<Toggle>("Enabled", "Draw strands, drips and semen in OStim and SexLab scenes.", &Settings::enabled));
			page.push_back(std::make_unique<Toggle>("Debug log", "Write scene and strand details to OStrands.log.", &Settings::debugLog));
			page.push_back(std::make_unique<Toggle>("Creatures",
				"Include creatures in SexLab and OStim scenes and arousal precum, using the penis bones listed under [CreatureRigs] in OStrands.ini. Applies from the next stage.",
				&Settings::creatures, on));

			page.push_back(std::make_unique<Heading>("Strand types"));
			page.push_back(std::make_unique<Toggle>("Crotch strands", "Strands between the bodies as they part after a thrust.", &Settings::crotch, on));
			page.push_back(std::make_unique<Toggle>("Scissoring strands", "Strands between two vulvas. Applies from the next scene.", &Settings::tribbing, on));
			page.push_back(std::make_unique<Toggle>("Oral strands", "Strands for blowjobs, cunnilingus and rimming. Applies from the next scene.",
				&Settings::oral, on));
			page.push_back(std::make_unique<Toggle>("Fingering strands", "Strands for fingering and rubbing. Applies from the next scene.",
				&Settings::fingering, on));
			page.push_back(std::make_unique<Toggle>("Toy strands", "Strands for dildos and other toys. Applies from the next scene.", &Settings::toys, on));

			page.push_back(std::make_unique<Heading>("Spawning"));
			page.push_back(std::make_unique<Slider<float>>("Bridge rate", "Most strands per second between the opening and the shaft.",
				&Settings::bridgeRate, Settings::kRateMin, Settings::kRateMax, 0.1f, on));
			page.push_back(std::make_unique<Slider<float>>("Crotch rate", "Most strands per second between the bodies.", &Settings::crotchRate,
				Settings::kRateMin, Settings::kRateMax, 0.1f, on));
			page.push_back(std::make_unique<Slider<int>>("Max strands", "Most strands on screen at once.", &Settings::maxTotal,
				static_cast<float>(Settings::kCountMin), static_cast<float>(Settings::kCountMax), 1.0f, on));

			page.push_back(std::make_unique<Heading>("Drips"));
			page.push_back(std::make_unique<Toggle>("Drips", "Passive drips from the vagina during and after scenes.", &Settings::drips, on));
			page.push_back(std::make_unique<Toggle>("Strap-on wearer drips", "A woman wearing a strap-on drips too.", &Settings::wearerDrips, drips));
			page.push_back(std::make_unique<Slider<float>>("Drip interval", "Average seconds between drips.", &Settings::dripInterval,
				Settings::kIntervalMin, Settings::kIntervalMax, 0.1f, drips));
			page.push_back(std::make_unique<Slider<float>>("Drip linger", "Seconds drips keep falling after a scene ends, getting sparser. 0 turns it off.",
				&Settings::dripLinger, Settings::kLingerMin, Settings::kLingerMax, 1.0f, drips));

			page.push_back(std::make_unique<Heading>("Look"));
			page.push_back(std::make_unique<Slider<float>>("Transparency", "How see-through the strands are. 0 is solid.", &Settings::transparency,
				Settings::kTransparencyMin, Settings::kTransparencyMax, 0.05f, on));
			page.push_back(std::make_unique<Slider<float>>("Shine", "Brightness of the wet highlights on the strands.", &Settings::shine,
				Settings::kShineMin, Settings::kShineMax, 0.5f, on));
			page.push_back(std::make_unique<Slider<float>>("Shine sharpness", "Higher makes the highlights smaller and sharper, like liquid.",
				&Settings::gloss, Settings::kGlossMin, Settings::kGlossMax, 10.0f, on));
			page.push_back(std::make_unique<Slider<float>>("Strand thickness", "Starting radius of the strands between the bodies and the shaft.",
				&Settings::radius, Settings::kStrandRadiusMin, Settings::kStrandRadiusMax, 0.005f, on));
			page.push_back(std::make_unique<Slider<float>>("Crotch strand thickness", "Starting radius of the strands between the bodies.",
				&Settings::crotchRadius, Settings::kStrandRadiusMin, Settings::kStrandRadiusMax, 0.005f, on));
			const auto creatureShape = [] { return Settings::Get().enabled && Settings::Get().creatureShape; };
			page.push_back(std::make_unique<Toggle>("Own strand thickness for a Colossal Load",
				"A Colossal Load giver uses the values below. With the Anatomica link, Anatomica's thickness for him is used instead.",
				&Settings::creatureShape, on));
			page.push_back(std::make_unique<Slider<float>>("Colossal strand thickness", "Starting radius of Colossal Load strands.", &Settings::creatureRadius,
				Settings::kStrandRadiusMin, Settings::kStrandRadiusMax, 0.005f, creatureShape));
			page.push_back(std::make_unique<Slider<float>>("Colossal crotch strand thickness", "Starting radius of Colossal Load crotch strands.",
				&Settings::creatureCrotchRadius, Settings::kStrandRadiusMin, Settings::kStrandRadiusMax, 0.005f, creatureShape));
			page.push_back(std::make_unique<Toggle>("Glints", "Little star glints that flash along the strands, like sunlight on water.",
				&Settings::glints, on));
			page.push_back(std::make_unique<Slider<float>>("Glint rate", "Glints per second on each strand.", &Settings::glintRate,
				Settings::kGlintRateMin, Settings::kGlintRateMax, 0.1f, glints));
			page.push_back(std::make_unique<Slider<float>>("Glint size", "How big each glint is.", &Settings::glintSize, Settings::kGlintSizeMin,
				Settings::kGlintSizeMax, 0.05f, glints));
			page.push_back(std::make_unique<Slider<float>>("Glint brightness", "How bright each glint flashes.", &Settings::glintBrightness,
				Settings::kGlintBrightnessMin, Settings::kGlintBrightnessMax, 0.1f, glints));

			page.push_back(std::make_unique<Heading>("Collision"));
			page.push_back(std::make_unique<Toggle>("Body collision", "Strands, drips and semen rest on bodies instead of passing through them.",
				&Settings::collision, on));
			page.push_back(std::make_unique<Slider<float>>("Body thickness",
				"Scales the collision shapes. Lower it if fluid floats above the skin, raise it if it sinks in.", &Settings::collisionThickness,
				Settings::kThicknessMin, Settings::kThicknessMax, 0.05f, collision));
			page.push_back(std::make_unique<Toggle>("Show collision shapes",
				"Shows collision dots on the console-selected actor, scene actors or the player. For debugging after character creation. Automatically turns off when starting a new game. Turn it off to play.",
				&Settings::showShapes, on));
			page.push_back(std::make_unique<Button>("Refit collision shapes",
				"Remeasure loaded geometry after changing a body or morph. With Debug log enabled, records the selected mesh, sample counts and fallback radii.",
				[] { Body::RequestRefit(); }));

			page.push_back(std::make_unique<Heading>("Settings files"));
			page.push_back(std::make_unique<Note>([] {
				return std::string("Your changes are saved to Data\\SKSE\\Plugins\\OStrands_User.ini. Updates never overwrite that file. OStrands.ini holds the defaults and is replaced by every update.");
			}));
			page.push_back(std::make_unique<Button>("Reload settings files", "Read OStrands.ini and OStrands_User.ini again after editing them by hand.", [] {
				if (Settings::Get().Load()) {
					OStimBridge::ReloadActions();
				}
			}));
		}

		void BuildSemen()
		{
			auto&      page = semen;
			const auto on = [] { return Settings::Get().enabled; };
			const auto active = [] { return Settings::Get().enabled && FluidManager::Get().SemenEnabled(); };

			page.push_back(std::make_unique<Choice>("Semen", "Auto turns semen on in SexLab scenes, and in OStim scenes when OCum is installed.", &Settings::semen,
				std::vector<std::pair<const char*, int>>{ { "Auto", -1 }, { "On", 1 }, { "Off", 0 } }, on));
			page.push_back(std::make_unique<Note>([] {
				auto       guard = Settings::Lock();
				const bool ocum = FluidManager::Get().OCumInstalled();
				const bool sexlab = FluidManager::Get().SexLabInstalled();
				const int  mode = Settings::Get().semen;
				if (mode > 0) {
					return std::string("Semen is on.");
				}
				if (mode == 0) {
					return std::string("Semen is off.");
				}
				if (ocum) {
					return std::string("Auto: on, OCum found.");
				}
				return std::string(sexlab ? "Auto: on in SexLab scenes; off in OStim scenes, OCum not found." : "Auto: off, OCum not found.");
			}));

			page.push_back(std::make_unique<Heading>("Load"));
			page.push_back(std::make_unique<Slider<float>>("Load size", "Size of each load. OCum scales it further in your own scenes.", &Settings::semenAmount,
				Settings::kAmountMin, Settings::kAmountMax, 0.05f, active));
			page.push_back(std::make_unique<Slider<float>>("Spurt strength", "How hard each spurt shoots.", &Settings::semenStrength, Settings::kStrengthMin,
				Settings::kStrengthMax, 0.05f, active));
			page.push_back(std::make_unique<Slider<int>>("Spurts", "Spurts per climax, before the final dribble.", &Settings::semenPulses,
				static_cast<float>(Settings::kPulsesMin), static_cast<float>(Settings::kPulsesMax), 1.0f, active));
			page.push_back(std::make_unique<Slider<float>>("Spill while inside",
				"Share of each spurt that seeps out around the shaft while it is still inside. The rest comes out after pulling out.", &Settings::semenOverflow,
				Settings::kOverflowMin, Settings::kOverflowMax, 0.05f, active));

			page.push_back(std::make_unique<Heading>("Fluid"));
			page.push_back(std::make_unique<Slider<float>>("Thickness", "How thick and slow the semen flows. Higher holds ropes together.",
				&Settings::semenViscosity, Settings::kViscosityMin, Settings::kViscosityMax, 1.0f, active));
			page.push_back(std::make_unique<Slider<float>>("Clumping", "How strongly drops pull together into globs.", &Settings::semenCohesion,
				Settings::kCohesionMin, Settings::kCohesionMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<float>>("Stretch", "How easily strings stretch before they snap. Higher gives longer, thinner strings.",
				&Settings::semenStretch, Settings::kStretchMin, Settings::kStretchMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<float>>("Skin stickiness", "How firmly semen holds on to skin. Lower lets it run down.",
				&Settings::semenStickiness, Settings::kStickinessMin, Settings::kStickinessMax, 0.1f, active));
			page.push_back(std::make_unique<Slider<float>>("Opacity", "1 is solid white.", &Settings::semenOpacity, Settings::kOpacityMin,
				Settings::kOpacityMax, 0.05f, active));

			page.push_back(std::make_unique<ColourPick>("Colour", "Colour of the semen. Anatomica's per-race colours win when the Anatomica link is on.",
				&Settings::semenColour, active));

			const auto creature = [] { return Settings::Get().enabled && FluidManager::Get().SemenEnabled() && Settings::Get().creatureSemen; };
			page.push_back(std::make_unique<Heading>("Colossal Load"));
			page.push_back(std::make_unique<Toggle>("Colossal Load",
				"A bigger, heavier load. Without Anatomica it is for creatures, and only with More Nasty Critters installed. With the "
				"Anatomica link on, Anatomica decides who gets one (men by schlong size or by chance; creatures by their type) and how big, "
				"and these values are not used.",
				&Settings::creatureSemen, active));
			page.push_back(std::make_unique<Slider<float>>("Colossal load size", "Size of each Colossal Load.", &Settings::creatureAmount,
				Settings::kAmountMin, Settings::kAmountMax, 0.05f, creature));
			page.push_back(std::make_unique<Slider<float>>("Colossal spurt strength", "How hard each Colossal Load spurt shoots.", &Settings::creatureStrength,
				Settings::kStrengthMin, Settings::kStrengthMax, 0.05f, creature));
			page.push_back(std::make_unique<Slider<int>>("Colossal spurts", "Spurts per Colossal Load.", &Settings::creaturePulses,
				static_cast<float>(Settings::kPulsesMin), static_cast<float>(Settings::kPulsesMax), 1.0f, creature));
			page.push_back(std::make_unique<Slider<float>>("Colossal thickness", "How thick and slow a Colossal Load flows.", &Settings::creatureViscosity,
				Settings::kViscosityMin, Settings::kViscosityMax, 1.0f, creature));
			page.push_back(std::make_unique<Slider<float>>("Colossal clumping", "How strongly a Colossal Load pulls together into globs.",
				&Settings::creatureCohesion, Settings::kCohesionMin, Settings::kCohesionMax, 0.5f, creature));
			page.push_back(std::make_unique<Slider<float>>("Colossal stretch", "How far Colossal Load strings stretch before they snap.", &Settings::creatureStretch,
				Settings::kStretchMin, Settings::kStretchMax, 0.5f, creature));
			page.push_back(std::make_unique<ColourPick>("Colossal colour", "Colour of a Colossal Load.", &Settings::creatureColour, creature));

			page.push_back(std::make_unique<Heading>("Anatomica"));
			page.push_back(std::make_unique<Note>([] { return AnatomicaBridge::Describe(); }));
			const auto linked = [] { return Settings::Get().enabled && AnatomicaBridge::Connected(); };
			const auto linkOn = [] { return Settings::Get().enabled && AnatomicaBridge::Connected() && Settings::Get().anatomica; };
			page.push_back(std::make_unique<Toggle>("Link with Anatomica", "Use Anatomica for the anus and its cum when Anatomica is installed.",
				&Settings::anatomica, linked));
			page.push_back(std::make_unique<Toggle>("Use Anatomica's hole",
				"Strands and semen start at his real opening as Anatomica measures it (gape and PPA pull included), not at the anus bones plus the Alignment offsets.",
				&Settings::anatomicaHole, linkOn));
			page.push_back(std::make_unique<Toggle>("Hand loads to Anatomica",
				"What goes into an Anatomica anus is given to Anatomica: it leaks it out later (drips, gushes, squirts) instead of OStrands leaking its own copy.",
				&Settings::anatomicaHandOver, linkOn));
			page.push_back(std::make_unique<Toggle>("Anatomica's colours", "Use Anatomica's cum colour for each giver (its per-race colours).",
				&Settings::anatomicaColour, linkOn));
			page.push_back(std::make_unique<Toggle>("Anatomica's Colossal Load",
				"Anatomica (2.6c or newer) decides who gets a Colossal Load and how big - set on its Integrations page.",
				&Settings::anatomicaLoad, linkOn));
			page.push_back(std::make_unique<Toggle>("Hold the load in",
				"A finish that starts inside an Anatomica anus stays in him while the tip is near, even if the animation pulls back.",
				&Settings::anatomicaHold, linkOn));
			page.push_back(std::make_unique<Slider<float>>("Spills out round the shaft", "Share of a load inside an Anatomica anus that spills out at once (the rest Anatomica lets out later).",
				&Settings::anatomicaOverflow, 0.0f, 1.0f, 0.05f, linkOn));
			page.push_back(std::make_unique<Slider<float>>("What comes out of him: stickiness",
				"How much what Anatomica lets out of him (ooze, gush, flood, squirt) clings to his skin and runs down it. The schlong's own spurts use Stickiness above.",
				&Settings::anatomicaOutStickiness, Settings::kStickinessMin, Settings::kStickinessMax, 0.05f, linkOn));
			page.push_back(std::make_unique<Slider<float>>("What comes out of him: thickness", "How thick and slow it flows.",
				&Settings::anatomicaOutViscosity, Settings::kViscosityMin, Settings::kViscosityMax, 1.0f, linkOn));

			page.push_back(std::make_unique<Heading>("Lifetime"));
			page.push_back(std::make_unique<Slider<float>>("Lifetime", "Seconds semen stays before it fades away.", &Settings::fluidLifetime,
				Settings::kLifetimeMin, Settings::kLifetimeMax, 5.0f, active));
			page.push_back(std::make_unique<Slider<float>>("Fade time", "Seconds semen takes to shrink away at the end of its lifetime.", &Settings::fluidFade,
				Settings::kFadeMin, Settings::kFadeMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<int>>("Max particles", "Most semen particles in one spot. Lower is faster.", &Settings::fluidMaxParticles,
				static_cast<float>(Settings::kParticlesMin), static_cast<float>(Settings::kParticlesMax), 32.0f, active));

			page.push_back(std::make_unique<Heading>("OCum and testing"));
			page.push_back(std::make_unique<Slider<float>>("OCum wait",
				"Seconds to wait for OCum's load size in your scene before spurting with the default amount.", &Settings::ocumWait, Settings::kWaitMin,
				Settings::kWaitMax, 0.1f, active));
			page.push_back(std::make_unique<Key>("Test key",
				"Click, then press a key. During your OStim scene, that key makes everyone with a schlong climax. Leave it unbound in normal play.",
				&Settings::fluidTestKey, active));
		}

		void BuildArousal()
		{
			auto&      page = arousal;
			const auto found = [] { return Settings::Get().enabled && Arousal::Installed(); };
			const auto active = [] { return Settings::Get().enabled && Arousal::Installed() && Settings::Get().arousal; };

			page.push_back(std::make_unique<Note>([] {
				return std::string(Arousal::Live()      ? "SLO Aroused NG found; arousal is read live from its plugin." :
				                   Arousal::Installed() ? "Arousal mod found." :
				                                          "No arousal mod found. Install SLO Aroused NG, OSL Aroused, or another mod that keeps SexLab Aroused's arousal faction, to use these settings.");
			}));
			page.push_back(std::make_unique<Toggle>("Arousal drips", "Aroused, naked actors near you drip outside of scenes.", &Settings::arousal, found));
			page.push_back(std::make_unique<Toggle>("Wetness", "Aroused actors with a vagina drip from it.", &Settings::arousalWetness, active));
			page.push_back(std::make_unique<Toggle>("Precum", "Aroused actors with a schlong drip precum from the tip.", &Settings::arousalPrecum, active));
			page.push_back(std::make_unique<Slider<float>>("Arousal threshold", "Arousal, from 0 to 100, where dripping starts.", &Settings::arousalThreshold,
				Settings::kThresholdMin, Settings::kThresholdMax, 1.0f, active));
			page.push_back(std::make_unique<Slider<float>>("Arousal drip interval",
				"Average seconds between drips at full arousal. At the threshold they come four times slower.", &Settings::arousalInterval,
				Settings::kArousalIntervalMin, Settings::kArousalIntervalMax, 0.5f, active));
			page.push_back(std::make_unique<Slider<float>>("Arousal range", "How far from you actors drip, in game units.", &Settings::arousalRadius,
				Settings::kReachMin, Settings::kReachMax, 128.0f, active));

			page.push_back(std::make_unique<Heading>("Revealing armor"));
			page.push_back(std::make_unique<Note>([] {
				return std::string("Drips stop when something is in the body slot, unless that armor is marked below or has the OStrands_Revealing keyword.");
			}));
			page.push_back(std::make_unique<Button>("Mark body armor as revealing",
				"Uses the console-selected actor, or you. Everyone wearing that armor drips through it from now on.",
				[] { SKSE::GetTaskInterface()->AddTask([] { Revealing::Mark(true); }); }));
			page.push_back(std::make_unique<Button>("Unmark body armor",
				"Uses the console-selected actor, or you. That armor stops drips again.",
				[] { SKSE::GetTaskInterface()->AddTask([] { Revealing::Mark(false); }); }));
			page.push_back(std::make_unique<Key>("Revealing toggle key",
				"Click, then press a key. In game, that key marks or unmarks the body armor of the console-selected actor, or yours.",
				&Settings::revealingKey));
			page.push_back(std::make_unique<Note>([] {
				std::size_t marked = 0;
				{
					auto guard = Settings::Lock();
					marked = Settings::Get().revealingArmor.size();
				}
				const auto status = Revealing::Status();
				return std::format("{} armor(s) marked.{}{}", marked, status.empty() ? "" : " ", status);
			}));
		}
	}

	namespace
	{
		void BuildAlignment()
		{
			auto&      page = alignment;
			const auto on = [] { return Settings::Get().enabled; };
			const auto slider = [&](const char* a_name, const char* a_tooltip, float Settings::* a_field) {
				page.push_back(std::make_unique<Slider<float>>(a_name, a_tooltip, a_field, Settings::kAlignMin, Settings::kAlignMax, 0.1f, on));
			};

			page.push_back(std::make_unique<Note>([] {
				return std::string("Moves where strands, drips and semen start, for bodies whose openings sit off the skeleton. Turn on Show collision shapes to see the opening markers move while you tune. Values are in game units.");
			}));
			page.push_back(std::make_unique<Toggle>("Show collision shapes",
				"Shows collision dots and opening markers on the console-selected actor, scene actors or the player. Turn it off to play.", &Settings::showShapes, on));

			page.push_back(std::make_unique<Toggle>("No vagina: use the anus",
				"When a scene calls for vaginal but the receiver has no vagina (men), strands and semen use the anus.", &Settings::noVaginaUsesAnus, on));

			page.push_back(std::make_unique<Heading>("Vagina"));
			slider("Vagina out", "Positive moves the origin out of the body, negative pushes it in.", &Settings::vaginaOut);
			slider("Vagina up", "Positive moves the origin toward the head, negative toward the feet.", &Settings::vaginaUp);

			page.push_back(std::make_unique<Heading>("Anus"));
			slider("Anus out", "Positive moves the origin out of the body, negative pushes it in.", &Settings::anusOut);
			slider("Anus up", "Positive moves the origin toward the head, negative toward the feet.", &Settings::anusUp);

			page.push_back(std::make_unique<Heading>("Mouth"));
			slider("Mouth out", "Positive moves the origin out in front of the face, negative pushes it into the mouth.", &Settings::mouthOut);
			slider("Mouth up", "Positive moves the origin up toward the nose, negative down toward the chin.", &Settings::mouthUp);

			page.push_back(std::make_unique<Button>("Reset alignment", "Sets every offset on this page back to 0.", [] {
				auto  guard = Settings::Lock();
				auto& settings = Settings::Get();
				for (auto field : { &Settings::vaginaOut, &Settings::vaginaUp, &Settings::anusOut, &Settings::anusUp, &Settings::mouthOut, &Settings::mouthUp }) {
					settings.*field = 0.0f;
				}
				settings.Save();
			}));
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule()) {
			logger::info("SKSE Menu Framework not found; change settings in OStrands_User.ini");
			return;
		}
		if (strands.empty()) {
			BuildStrands();
			BuildSemen();
			BuildArousal();
			BuildAlignment();
		}
		SKSEMenuFramework::SetSection("OStrands");
		SKSEMenuFramework::AddSectionItem("Strands", DrawStrands);
		SKSEMenuFramework::AddSectionItem("Semen", DrawSemen);
		SKSEMenuFramework::AddSectionItem("Arousal", DrawArousal);
		SKSEMenuFramework::AddSectionItem("Alignment", DrawAlignment);
		logger::info("settings added to SKSE Menu Framework (API {})", SKSEMenuFramework::GetMenuFrameworkAPIVersion());
	}
}
