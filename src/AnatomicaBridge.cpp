#include "AnatomicaBridge.h"

#include <cmath>
#include <format>
#include <optional>

#include "FluidManager.h"
#include "Settings.h"

namespace OStrands::AnatomicaBridge
{
	namespace
	{
		using ApiVersionFn = int (*)();
		using HoleFrameFn = bool (*)(RE::Actor*, float*, float*);
		using CumColourFn = bool (*)(RE::Actor*, float*);
		using CumGlowFn = bool (*)(RE::Actor*, float*);   // 0.8.0.7: rgb + strength
		using TakesOverFn = bool (*)(RE::Actor*);
		using SemenInFn = void (*)(RE::Actor*, RE::Actor*, float, const float*);
		using LoadKindFn = bool (*)(RE::Actor*, RE::Actor*, float*);

		struct Api
		{
			int          version{ 0 };
			HoleFrameFn  holeFrame{ nullptr };
			CumColourFn  cumColour{ nullptr };
			CumGlowFn    cumGlow{ nullptr };
			TakesOverFn  takesOver{ nullptr };
			SemenInFn    semenIn{ nullptr };
			LoadKindFn   loadKind{ nullptr };
		};

		Api g_api;

		template <class T>
		T Find(REX::W32::HMODULE a_module, const char* a_name)
		{
			return reinterpret_cast<T>(REX::W32::GetProcAddress(a_module, a_name));
		}

		bool Enabled()
		{
			return g_api.version > 0 && Settings::Get().anatomica;
		}
	}

	void Connect()
	{
		auto* module = REX::W32::GetModuleHandleW(L"AnatomicaMenu.dll");
		if (!module) {
			logger::info("Anatomica not found; holes come from the skeleton");
			return;
		}
		const auto version = Find<ApiVersionFn>(module, "Anatomica_ApiVersion");
		if (!version) {
			logger::info("Anatomica found, but this version has no OStrands link (needs Anatomica 2.5w or newer)");
			return;
		}
		g_api.holeFrame = Find<HoleFrameFn>(module, "Anatomica_HoleFrame");
		g_api.cumColour = Find<CumColourFn>(module, "Anatomica_CumColour");
		g_api.cumGlow = Find<CumGlowFn>(module, "Anatomica_CumGlow");
		g_api.takesOver = Find<TakesOverFn>(module, "Anatomica_TakesOver");
		g_api.semenIn = Find<SemenInFn>(module, "Anatomica_SemenIn");
		g_api.loadKind = Find<LoadKindFn>(module, "Anatomica_LoadKind");
		g_api.version = version();
		logger::info("Anatomica link v{}: hole {}, colour {}, hand over {}, Colossal Load {}", g_api.version, g_api.holeFrame != nullptr, g_api.cumColour != nullptr,
			g_api.takesOver && g_api.semenIn, g_api.loadKind != nullptr);
	}

	bool Connected() { return g_api.version > 0; }

	bool HoleFrame(RE::Actor* a_actor, RE::NiPoint3& a_center, RE::NiPoint3& a_deep)
	{
		if (!a_actor || !Enabled() || !Settings::Get().anatomicaHole || !g_api.holeFrame) {
			return false;
		}
		float center[3]{};
		float deep[3]{};
		if (!g_api.holeFrame(a_actor, center, deep)) {
			return false;
		}
		a_center = { center[0], center[1], center[2] };
		a_deep = { deep[0], deep[1], deep[2] };
		return std::isfinite(a_center.x) && std::isfinite(a_deep.x) && a_center.GetDistance(a_deep) > 0.01f;
	}

	bool CumGlow(RE::Actor* a_giver, std::array<float, 4>& a_glow)
	{
		if (!a_giver || !Enabled() || !Settings::Get().anatomicaColour || !g_api.cumGlow) {
			return false;
		}
		float v[4]{};
		if (!g_api.cumGlow(a_giver, v)) {
			return false;
		}
		for (std::size_t c = 0; c < 3; ++c) {
			a_glow[c] = std::isfinite(v[c]) ? std::clamp(v[c], 0.0f, 1.0f) : 1.0f;
		}
		a_glow[3] = std::isfinite(v[3]) ? std::clamp(v[3], 0.0f, 5.0f) : 0.0f;
		return a_glow[3] > 0.0f;
	}

	bool CumColour(RE::Actor* a_giver, std::array<float, 3>& a_colour)
	{
		if (!a_giver || !Enabled() || !Settings::Get().anatomicaColour || !g_api.cumColour) {
			return false;
		}
		float rgb[3]{};
		if (!g_api.cumColour(a_giver, rgb)) {
			return false;
		}
		for (std::size_t c = 0; c < 3; ++c) {
			a_colour[c] = std::isfinite(rgb[c]) ? std::clamp(rgb[c], 0.0f, 1.0f) : 1.0f;
		}
		return true;
	}

	bool TakesOver(RE::Actor* a_receiver)
	{
		return a_receiver && Enabled() && Settings::Get().anatomicaHandOver && g_api.takesOver && g_api.semenIn && g_api.takesOver(a_receiver);
	}

	void SemenIn(RE::Actor* a_receiver, RE::Actor* a_giver, float a_loads, const std::array<float, 3>& a_colour)
	{
		if (a_receiver && a_loads > 0.0f && Enabled() && g_api.semenIn) {
			g_api.semenIn(a_receiver, a_giver, a_loads, a_colour.data());
		}
	}

	bool LoadKind(RE::Actor* a_giver, RE::Actor* a_receiver, Colossal::Kind& a_kind)
	{
		if (!a_giver || !Enabled() || !Settings::Get().anatomicaLoad || !g_api.loadKind) {
			return false;
		}
		float out[12]{};
		if (!g_api.loadKind(a_giver, a_receiver, out)) {
			return false;
		}
		const auto num = [&](int a_i, float a_lo, float a_hi) { return std::isfinite(out[a_i]) ? std::clamp(out[a_i], a_lo, a_hi) : 1.0f; };
		a_kind = {};
		a_kind.linked = true;
		a_kind.weight = std::isfinite(out[0]) ? std::clamp(out[0], 0.0f, 1.0f) : 0.0f;
		a_kind.colossal = a_kind.weight > 0.0f;
		a_kind.amount = num(1, 0.1f, 10.0f);
		a_kind.strength = num(2, 0.1f, 5.0f);
		a_kind.pulses = num(3, 0.2f, 5.0f);
		a_kind.thick = num(4, 0.2f, 5.0f);
		a_kind.sticky = num(5, 0.1f, 5.0f);
		a_kind.stretch = num(6, 0.1f, 5.0f);
		a_kind.hasColour = std::isfinite(out[7]) && out[7] > 0.5f;
		for (std::size_t c = 0; c < 3; ++c) {
			a_kind.colour[c] = std::isfinite(out[8 + c]) ? std::clamp(out[8 + c], 0.0f, 1.0f) : 0.9f;
		}
		return true;
	}

	std::string Describe()
	{
		if (!Connected()) {
			return REX::W32::GetModuleHandleW(L"AnatomicaMenu.dll") ? "Anatomica is installed, but its version has no OStrands link (2.5w or newer has it)." :
			                                                          "Anatomica is not installed.";
		}
		return std::format("Anatomica link v{} connected.", g_api.version);
	}
}

extern "C"
{
	__declspec(dllexport) int OStrands_ApiVersion()
	{
		return 2;   // 0.8.0.7: OStrands_EmitGlow
	}

	// Adds semen particles at a world point (any thread). host: the actor it belongs to (it collides with and is drawn near them).
	// stream: the same non-zero number on consecutive calls links the particles into one rope. velocity: game units per second.
	// count: particles (a full OStrands load is roughly 150-250). colour: 0-1 RGB, or null for the actor's / default colour.
	__declspec(dllexport) bool OStrands_Emit(RE::Actor* a_host, std::uint32_t a_stream, const float* a_point, const float* a_velocity, int a_count,
		const float* a_colour)
	{
		if (!a_host || !a_point || !a_velocity || a_count <= 0) {
			return false;
		}
		std::optional<std::array<float, 3>> colour;
		if (a_colour) {
			colour = std::array<float, 3>{ a_colour[0], a_colour[1], a_colour[2] };
		}
		return OStrands::FluidManager::Get().Inject(a_host->GetHandle(), a_stream, { a_point[0], a_point[1], a_point[2] },
			{ a_velocity[0], a_velocity[1], a_velocity[2] }, std::min(a_count, 64), colour);
	}

	// 0.8.0.7: as OStrands_Emit, plus a glow (rgb 0-1 and strength; null or strength 0 = no glow)
	__declspec(dllexport) bool OStrands_EmitGlow(RE::Actor* a_host, std::uint32_t a_stream, const float* a_point, const float* a_velocity, int a_count,
		const float* a_colour, const float* a_glow)
	{
		if (!a_host || !a_point || !a_velocity || a_count <= 0) {
			return false;
		}
		std::optional<std::array<float, 3>> colour;
		if (a_colour) {
			colour = std::array<float, 3>{ a_colour[0], a_colour[1], a_colour[2] };
		}
		std::optional<std::array<float, 4>> glow;
		if (a_glow) {
			glow = std::array<float, 4>{ a_glow[0], a_glow[1], a_glow[2], a_glow[3] };
		}
		return OStrands::FluidManager::Get().Inject(a_host->GetHandle(), a_stream, { a_point[0], a_point[1], a_point[2] },
			{ a_velocity[0], a_velocity[1], a_velocity[2] }, std::min(a_count, 64), colour, glow);
	}

	// true while OStrands' semen is on (Anatomica then lets OStrands draw its cum).
	__declspec(dllexport) bool OStrands_SemenOn()
	{
		return OStrands::Settings::Get().enabled && OStrands::FluidManager::Get().SemenEnabled();
	}
}
