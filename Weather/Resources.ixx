export module Resources;

import std;

import Plugin;

export inline constexpr std::string_view SFX_RAIN[] = { "ambience/rainsound.wav", "ambience/stormrain.wav", };
export inline constexpr std::string_view thundersound[] = { "ambience/thunder1.wav", "ambience/thunder2.wav", "ambience/thunder3.wav", };
export inline constexpr std::string_view thunderclap[] = { "ambience/thunderflash1.wav", "ambience/thunderflash2.wav", "ambience/thunderflash3.wav", "ambience/thunderflash4.wav", "ambience/thunderflash5.wav", };

export inline std::int16_t g_fxbeam{};

inline bool g_bShouldPrecache = true;

export void Resource_Precache() noexcept
{
	[[likely]]
	if (!g_bShouldPrecache)
		return;

	g_bShouldPrecache = false;

	g_fxbeam = (std::int16_t)g_engfuncs.pfnPrecacheModel("sprites/laserbeam.spr");
}

export void Resource_GameInit() noexcept
{
	g_bShouldPrecache = true;
}
