export module Server;

import std;
import hlsdk;

import UtlHook;

// Corresponding to g_psvs in ReHLDS.
export extern "C++" inline server_static_t* gpServerStatics = nullptr;

// LUNA: I would suggest in GameInit, but it actually doesn't matter.
export void RetrieveServerStatics() noexcept
{
	static constexpr auto OFS = 0x101E6D68 - 0x101E6D50;	// Anniversary (9980)

	if (g_engfuncs.pfnSetView != nullptr)
	{
		// We are only getting the pointer to client_s. Move back to get the head of struct.
		auto const p
			// Minus 8 for alignment.
			= UTIL_RetrieveGlobalVariable<unsigned char>(g_engfuncs.pfnSetView, OFS) - 8;

		gpServerStatics = reinterpret_cast<server_static_t*>(p);
	}
}
