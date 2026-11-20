#pragma once

#include <array>

// 0.8.0.8: where a spurt lands outside the body. Each spurt's flight is traced as an arc (its speed, gravity); where it
// first hits something it leaves a decal through the game's own decal system (the one blood uses):
//  * on someone's clothes or armour (when they are dressed there) - a stain on the armour;
//  * on walls, furniture and the floor - a splash.
// And the weather: rain slowly washes it off people and thins it, water washes it off faster.
namespace OStrands::Stains
{
	void Spurt(RE::Actor* a_giver, const RE::NiPoint3& a_from, const RE::NiPoint3& a_direction, float a_speed, const std::array<float, 3>& a_colour, float a_amount);
	float LordRate(const RE::NiPoint3& a_point, RE::TESObjectCELL* a_cell, const std::vector<RE::ActorHandle>& a_hosts);   // 0.8.0.8: a Vampire Lord: cold keeps it, the sun dries it
	float WashRate(const RE::NiPoint3& a_point, RE::TESObjectCELL* a_cell, const std::vector<RE::ActorHandle>& a_hosts);   // x how fast it goes (1 = dry)
	std::string Status();
}
