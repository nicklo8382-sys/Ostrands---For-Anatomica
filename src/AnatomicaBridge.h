#pragma once

#include <array>

#include "Colossal.h"

namespace OStrands::AnatomicaBridge
{
	void Connect();
	bool Connected();

	bool HoleFrame(RE::Actor* a_actor, RE::NiPoint3& a_center, RE::NiPoint3& a_deep);
	bool CumColour(RE::Actor* a_giver, std::array<float, 3>& a_colour);
	bool TakesOver(RE::Actor* a_receiver);
	void SemenIn(RE::Actor* a_receiver, RE::Actor* a_giver, float a_loads, const std::array<float, 3>& a_colour);
	bool LoadKind(RE::Actor* a_giver, RE::Actor* a_receiver, Colossal::Kind& a_kind);   // 0.8.0.4 Colossal Load (Anatomica 2.6c+)
	std::string Describe();
}
