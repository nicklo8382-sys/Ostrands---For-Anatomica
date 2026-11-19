#pragma once

#include <array>

#include "Settings.h"

namespace OStrands::Colossal
{
	// 0.8.0.4 Colossal Load: whose climax is bigger than normal, and by how much.
	// With Anatomica (2.6c+) Anatomica decides (men by schlong size or a chance per scene; with More Nasty Critters,
	// creatures by their type / group) and gives multipliers on the normal [Semen] values.
	// Without it, OStrands' own [ColossalLoad] values apply to creatures, and only when MoreNastyCritters.esp is loaded.
	struct Kind
	{
		bool                 colossal{ false };
		bool                 linked{ false };
		float                weight{ 0.0f };
		float                amount{ 1.0f };
		float                strength{ 1.0f };
		float                pulses{ 1.0f };
		float                thick{ 1.0f };
		float                sticky{ 1.0f };
		float                stretch{ 1.0f };
		bool                 hasColour{ false };
		std::array<float, 3> colour{};
	};

	bool         MncLoaded();
	Kind         Of(RE::Actor* a_giver, RE::Actor* a_receiver);
	SemenProfile Profile(const Kind& a_kind);
	float        StrandRadius(const Kind& a_kind);
	float        CrotchRadius(const Kind& a_kind);
	bool         OwnMinRadius(const Kind& a_kind);   // the [ColossalLoad] minimum radius applies (not linked)
	float        Size(const Kind& a_kind);           // 0.8.0.6: how much bigger its globs and ropes are drawn (1 = normal)
}
