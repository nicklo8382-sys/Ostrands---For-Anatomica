#pragma once

#include <array>

#include "Colossal.h"

namespace OStrands::AnatomicaBridge
{
	void Connect();
	bool Connected();

	bool HoleFrame(RE::Actor* a_actor, RE::NiPoint3& a_center, RE::NiPoint3& a_deep);
	bool CumColour(RE::Actor* a_giver, std::array<float, 3>& a_colour);
	bool CumGlow(RE::Actor* a_giver, std::array<float, 4>& a_glow);   // 0.8.0.7: Anatomica says his cum glows (rgb + strength)
	bool TakesOver(RE::Actor* a_receiver);
	void SemenIn(RE::Actor* a_receiver, RE::Actor* a_giver, float a_loads, const std::array<float, 3>& a_colour);
	void Swallow(RE::Actor* a_receiver, RE::Actor* a_giver, float a_loads);
	void Facial(RE::Actor* a_receiver, RE::Actor* a_giver, float a_loads);   // 0.8.0.8: a finish onto the face
	bool Precum(RE::Actor* a_receiver, RE::Actor* a_giver, float a_loads);   // 0.8.0.8: precum into an Anatomica anus (false = not his)   // 0.8.0.8: tell Anatomica (2.6j+) it was swallowed - a gulp
	bool LoadKind(RE::Actor* a_giver, RE::Actor* a_receiver, Colossal::Kind& a_kind);   // 0.8.0.4 Colossal Load (Anatomica 2.6c+)
	std::string Describe();
}
