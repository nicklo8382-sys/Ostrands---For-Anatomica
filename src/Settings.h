#pragma once

#include <array>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace OStrands
{
	struct ShaftRig
	{
		std::string              name;
		std::vector<std::string> bones;
		int                      axis{ 0 };
		float                    tipExtension{ 0.1f };
		float                    radius{ 0.0f };
	};

	struct SemenProfile
	{
		float                amount{ 1.5f };
		float                strength{ 1.0f };
		int                  pulses{ 5 };
		float                cohesion{ 15.0f };
		float                viscosity{ 50.0f };
		float                stretch{ 5.0f };
		std::array<float, 3> colour{ 236.0f / 255.0f, 233.0f / 255.0f, 222.0f / 255.0f };
	};

	struct Settings
	{
		bool  enabled{ true };
		bool  debugLog{ false };

		bool                  creatures{ true };
		std::vector<ShaftRig> rigs = DefaultRigs();
		bool                  noVaginaUsesAnus{ true };

		bool  creatureSemen{ true };   // 0.8.0.4 Colossal Load (creatures only with MoreNastyCritters.esp)
		float creatureAmount{ 2.5f };
		float creatureStrength{ 1.3f };
		int   creaturePulses{ 8 };
		float creatureCohesion{ 15.0f };
		float creatureViscosity{ 65.0f };
		float creatureStretch{ 8.0f };
		std::array<float, 3> creatureColour{ 236.0f / 255.0f, 233.0f / 255.0f, 222.0f / 255.0f };
		bool  creatureShape{ true };
		float creatureRadius{ 0.14f };
		float creatureMinRadius{ 0.035f };
		float creatureCrotchRadius{ 0.12f };
		float creatureCrotchMinRadius{ 0.03f };

		bool anatomica{ true };
		bool anatomicaHole{ true };
		bool anatomicaHandOver{ true };
		bool anatomicaColour{ true };
		bool anatomicaLoad{ true };   // 0.8.0.4: Anatomica decides who gets a Colossal Load
		bool  anatomicaHold{ true };       // 0.8.0.6: a finish that starts inside an Anatomica anus stays in him while the tip is near
		float anatomicaOverflow{ 0.1f };   // 0.8.0.6: share that spills out round the shaft there (Anatomica lets the rest out later)
		float anatomicaOutStickiness{ 0.9f };   // 0.8.0.6: what Anatomica lets out of him clings to his skin and runs down it
		float anatomicaOutViscosity{ 70.0f };   // 0.8.0.6: and how thick and slow it flows

		float bridgeRate{ 2.1f };
		int   maxPerStroke{ 2 };
		int   pullStrings{ 3 };
		float beads{ 0.6f };       // 0.8.0.7: strings bead up as they thin (0 = only just before they snap, as before)
		float splats{ 0.5f };      // 0.8.0.7: drops that land on skin flatten into a splash (0 = as before)
		bool  colossalCords{ true };   // 0.8.0.7: a Colossal Load comes out in thick unbroken cords   // 0.8.0.7: strings between the rim and the tip as it slides out (0 = off)
		int   maxPerPair{ 5 };
		int   maxTotal{ 20 };
		float cooldown{ 0.25f };

		float radius{ 0.10f };
		float minRadius{ 0.035f };
		float shaftRadius{ 1.0f };
		std::vector<std::pair<std::string, float>> shaftRadiusOverrides;
		float tipExtension{ 2.9f };
		float transparency{ 0.8f };
		float shine{ 20.0f };
		float gloss{ 100.0f };

		bool  glints{ true };
		float glintRate{ 1.5f };
		float glintSize{ 1.3f };
		float glintBrightness{ 5.0f };
		float glintLife{ 0.25f };
		int   glintMax{ 24 };

		float gravity{ 350.0f };
		float damping{ 0.9f };
		float viscosity{ 4.0f };
		float maxLength{ 14.0f };
		float maxLife{ 4.0f };
		float fadeTime{ 0.4f };

		bool  crotch{ true };
		float crotchRate{ 2.1f };
		int   crotchMaxPerContact{ 3 };
		int   crotchMaxPerPair{ 5 };
		float crotchMaxGap{ 10.0f };
		float crotchSpread{ 1.5f };
		float crotchSkin{ 1.0f };
		float crotchRadius{ 0.09f };
		float crotchMinRadius{ 0.03f };
		float crotchMaxLength{ 12.0f };
		float crotchMaxLife{ 4.0f };

		bool  tribbing{ true };
		float tribbingRate{ 1.0f };
		int   tribbingMaxPerContact{ 3 };
		int   tribbingMaxPerPair{ 5 };
		float tribbingMaxGap{ 6.0f };
		float tribbingSpread{ 1.5f };

		bool  oral{ true };
		bool  oralDrips{ true };
		float mouthBlend{ 0.7f };
		float lipWidth{ 0.9f };
		float lipHeight{ 0.45f };
		float oralRate{ 0.8f };
		int   oralMaxPerContact{ 3 };
		int   oralMaxPerPair{ 5 };
		float oralMaxGap{ 6.0f };
		float oralSpread{ 1.0f };
		// 0.8.0.8: the mouth after an oral finish, and spit
		float swallow{ 0.3f };       // share of an oral finish that is swallowed (the rest is held and dribbles out)
		float dribble{ 0.5f };       // how fast the mouth lets it out of the corners (1 = as any opening; lower holds it longer)
		float cough{ 0.08f };        // chance a second, while the mouth is very full, of a cough that pushes some out
		float spit{ 1.0f };          // spit: how often drool strings and drips come from the mouth during oral (0 = none, 3 = sloppy)
		bool  spitShaft{ true };     // the shaft drips spit during a blowjob
		float droolTime{ 20.0f };    // seconds of drool strings from the lip corners after oral
		bool  noseDrip{ false };     // a very full mouth can drip from the nose too
		float facialChance{ 0.2f };  // 0.8.0.8: chance a finish in the mouth pulls out onto the face instead
		bool  kissStrings{ true };   // 0.8.0.8: spit strings between the lips as a kiss parts
		bool  snowball{ true };      // 0.8.0.8: a kiss passes cum held in one mouth to the other
		bool  beastPeople{ true };   // 0.8.0.8: OStim Beast Form - werewolf / werebear / Vampire Lord forms count as people
		bool  beastBigLoads{ true }; // 0.8.0.8: follow Beast Form's "Big loads" (a Colossal Load for werewolves and werebears)
		bool  knotStrings{ true };   // 0.8.0.8: a knot pulling out - more, thicker strings and a gush
		float beastMouth{ 1.7f };    // 0.8.0.8: beast forms' lip points are this much further apart (a muzzle)
		bool  furCling{ true };      // 0.8.0.8: on beast forms semen clings in the fur (stickier, runs slower)
		bool  knotHold{ true };      // 0.8.0.8: a knot holds it all in; pulling the knot out lets out a bigger gush
		bool  tongueDrool{ true };   // 0.8.0.8: beast forms drip spit from the tongue tip when excited
		bool  lordBigLoads{ true };  // 0.8.0.8: Vampire Lords get a Colossal Load of their own (Beast Form leaves them normal)
		bool  lordDrool{ false };    // 0.8.0.8: Vampire Lords drool too (from the mouth - they have no tongue bones)
		bool  lordBlood{ false };    // 0.8.0.8: a Vampire Lord's cum is blood-dark
		std::array<float, 3> lordBloodColour{ 0.45f, 0.06f, 0.06f };
		float lordBloodMix{ 0.5f };  //   ... how much of that colour (0 = his normal colour, 1 = all of it)
		bool  lordCold{ true };      // 0.8.0.8: a Vampire Lord's cum is cold and thin: runs freer and stays wet longer
		bool  lordHover{ true };     // 0.8.0.8: while he levitates, what runs down him drips straight off below the hips
		bool  lordBats{ true };      // 0.8.0.8: turning into bats (his teleport) snaps the strands on him
		bool  lordBloodDrool{ true };// 0.8.0.8: very excited, or casting Vampiric Drain, his drool comes out red
		bool  lordFangs{ true };     // 0.8.0.8: spit strings hang from his fangs too
		float lordSun{ 3.0f };       // 0.8.0.8: out in the sun it dries off a Vampire Lord this much faster (1 = off)
		bool  stains{ true };        // 0.8.0.8: where a spurt lands outside the body it leaves a decal
		bool  stainClothes{ true };  //   ... on someone's clothes / armour (dressed there)
		bool  stainWorld{ true };    //   ... on walls, furniture and the floor
		float stainSize{ 1.0f };     //   ... x its size
		int   stainsPerSecond{ 6 };  //   ... at most this many a second
		float stainArc{ 1.0f };      // 0.8.0.8: x how far a spurt flies for the stain trace (aim)
		bool  weather{ true };       // 0.8.0.8: rain and water wash it off
		float rainWash{ 3.0f };      //   ... x as fast in the rain
		float waterWash{ 8.0f };     //   ... x as fast in water
		bool  shelter{ true };       //   ... under a roof the rain does not reach it
		float noseChance{ 0.1f };    // 0.8.0.8: chance a mouth finish comes out of the nose too
		float noseDeep{ 0.25f };     //   ... added when the tip is deep in the throat
		float noseAmount{ 1.0f };    //   ... x how much
		// 0.8.0.8: precum in scenes, by OStim excitement
		bool  precum{ true };        // the tip beads and drips more as he gets close
		float precumFrom{ 55.0f };   // OStim excitement it starts at (0-100)
		float precumRate{ 1.0f };    // how often (0.2-3)
		bool  precumStrings{ true }; // clear strings when the tip touches and pulls away

		bool  fingering{ true };
		float fingerRadius{ 0.35f };
		float fingerTipExtension{ 0.9f };
		bool  fingerDrips{ true };
		float fingerRate{ 0.8f };
		int   fingerMaxPerContact{ 2 };
		int   fingerMaxPerPair{ 4 };
		float fingerMaxGap{ 4.0f };
		float fingerSpread{ 0.8f };

		bool  toys{ true };
		float toyRadius{ 0.8f };
		float toyMaxRadius{ 20.0f };

		std::vector<std::pair<std::string, std::string>> actions;

		bool  drips{ true };
		bool  wearerDrips{ true };
		float dripInterval{ 4.0f };
		float dripLinger{ 15.0f };
		int   dripMaxPerSource{ 3 };
		float dripRadius{ 0.09f };
		float dripMinRadius{ 0.025f };
		float dripStretchTime{ 1.0f };
		float dripWeight{ 2.5f };
		float dripMaxLength{ 18.0f };
		float dripMaxLife{ 7.0f };
		float dripFadeTime{ 0.9f };

		float vaginaOut{ 0.0f };
		float vaginaUp{ 0.0f };
		float anusOut{ 0.0f };
		float anusUp{ 0.0f };
		float mouthOut{ 0.0f };
		float mouthUp{ 0.0f };

		std::vector<std::string>                         revealingKeywords{ "ostrands_revealing" };
		std::vector<std::pair<std::string, std::string>> revealingArmor;

		bool  arousal{ true };
		bool  arousalWetness{ true };
		bool  arousalPrecum{ true };
		float arousalThreshold{ 50.0f };
		float arousalInterval{ 3.0f };
		float arousalRadius{ 2048.0f };

		int   semen{ -1 };
		float semenAmount{ 1.5f };
		float semenStrength{ 1.0f };
		int   semenPulses{ 5 };
		float semenOpacity{ 0.9f };
		float semenCohesion{ 15.0f };
		float semenViscosity{ 50.0f };
		float semenStretch{ 5.0f };
		float semenStickiness{ 0.4f };
		float semenOverflow{ 0.5f };
		float ocumWait{ 0.5f };
		std::array<float, 3> semenColour{ 236.0f / 255.0f, 233.0f / 255.0f, 222.0f / 255.0f };
		bool                 semenGlow{ false };                         // 0.8.0.7: semen glows (emissive)
		bool                 glowCumColour{ true };                      // glow in the semen's own colour
		std::array<float, 3> glowColour{ 0.55f, 0.75f, 1.0f };
		float                glowStrength{ 1.0f };

		bool  collision{ true };
		float collisionThickness{ 1.0f };
		bool  showShapes{ false };

		float fluidLifetime{ 30.0f };
		float fluidFade{ 6.0f };
		int   fluidMaxParticles{ 1024 };
		int   fluidTestKey{ 0 };
		int   revealingKey{ 0 };

		SemenProfile Semen(bool a_creature) const;
		float        StrandRadius(bool a_creature) const { return a_creature && creatureShape ? creatureRadius : radius; }
		float        StrandMinRadius(bool a_creature) const { return a_creature && creatureShape ? creatureMinRadius : minRadius; }
		float        CrotchRadius(bool a_creature) const { return a_creature && creatureShape ? creatureCrotchRadius : crotchRadius; }
		float        CrotchMinRadius(bool a_creature) const { return a_creature && creatureShape ? creatureCrotchMinRadius : crotchMinRadius; }

		static Settings&             Get();
		static const Settings&       Defaults();
		static std::vector<ShaftRig> DefaultRigs();
		static std::unique_lock<std::recursive_mutex> Lock();
		static constexpr float kRateMin = 0.0f, kRateMax = 3.0f;
		static constexpr float kIntervalMin = 0.2f, kIntervalMax = 6.0f;
		static constexpr int kCountMin = 0, kCountMax = 32;
		static constexpr float kTransparencyMin = 0.0f, kTransparencyMax = 0.95f;
		static constexpr float kShineMin = 0.0f, kShineMax = 20.0f;
		static constexpr float kGlossMin = 10.0f, kGlossMax = 1000.0f;
		static constexpr float kGlintRateMin = 0.0f, kGlintRateMax = 6.0f;
		static constexpr float kGlintSizeMin = 0.1f, kGlintSizeMax = 3.0f;
		static constexpr float kGlintBrightnessMin = 0.0f, kGlintBrightnessMax = 5.0f;
		static constexpr float kLingerMin = 0.0f, kLingerMax = 30.0f;
		static constexpr float kThresholdMin = 0.0f, kThresholdMax = 100.0f;
		static constexpr float kAlignMin = -5.0f, kAlignMax = 5.0f;
		static constexpr float kArousalIntervalMin = 0.5f, kArousalIntervalMax = 20.0f;
		static constexpr float kReachMin = 256.0f, kReachMax = 8192.0f;
		static constexpr float kAmountMin = 0.0f, kAmountMax = 3.0f;
		static constexpr float kStrengthMin = 0.2f, kStrengthMax = 2.0f;
		static constexpr int kPulsesMin = 1, kPulsesMax = 12;
		static constexpr float kOverflowMin = 0.0f, kOverflowMax = 1.0f;
		static constexpr float kViscosityMin = 0.0f, kViscosityMax = 100.0f;
		static constexpr float kCohesionMin = 0.0f, kCohesionMax = 20.0f;
		static constexpr float kStretchMin = 0.0f, kStretchMax = 30.0f;
		static constexpr float kStickinessMin = 0.0f, kStickinessMax = 5.0f;
		static constexpr float kOpacityMin = 0.05f, kOpacityMax = 1.0f;
		static constexpr float kLifetimeMin = 5.0f, kLifetimeMax = 300.0f;
		static constexpr float kFadeMin = 0.5f, kFadeMax = 30.0f;
		static constexpr int kParticlesMin = 64, kParticlesMax = 2000;
		static constexpr float kWaitMin = 0.0f, kWaitMax = 2.0f;
		static constexpr float kThicknessMin = 0.5f, kThicknessMax = 2.0f;
		static constexpr float kStrandRadiusMin = 0.02f, kStrandRadiusMax = 0.4f;
		static constexpr int kKeyMax = 255;
		bool Load();
		bool Save() const;
		void Validate();

	private:
		bool ReadFile(const char* a_path);
		void Report() const;
	};
}
