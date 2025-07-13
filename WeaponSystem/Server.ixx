export module Server;

import std;
import hlsdk;

import Uranus;
import UtlHook;

// Corresponding to g_psvs in ReHLDS.
export extern "C++" inline server_static_t* gpServerStatics = nullptr;

// LUNA: I would suggest in GameInit, but it actually doesn't matter.
export void RetrieveServerVariables() noexcept
{
	// Server statics
	if (g_engfuncs.pfnSetView != nullptr)
	{
		static constexpr auto OFS = 0x101E6D68 - 0x101E6D50;	// Anniversary (9980)

		// We are only getting the pointer to client_s. Move back to get the head of struct.
		auto const p
			// Minus 8 for alignment.
			= UTIL_RetrieveGlobalVariable<unsigned char>(g_engfuncs.pfnSetView, OFS) - 8;

		gpServerStatics = reinterpret_cast<server_static_t*>(p);
	}

	// Server variables
	if (gUranusCollection.pfnHost_ShutdownServer)
	{
		static constexpr auto OFS = 0x101D3398 - 0x101D3390;	// Anniversary (9980)
		gpServerVars = UTIL_RetrieveGlobalVariable<server_t>(gUranusCollection.pfnHost_ShutdownServer, OFS);
	}
}

export [[nodiscard]] bool UTIL_IsFirstPersonal(edict_t* pPlayer) noexcept
{
	auto const iPlayer = ent_cast<short>(pPlayer);
	if (iPlayer < 1 || iPlayer >= (int)gpServerStatics->maxclientslimit)
		return false;

	auto const& Client = gpServerStatics->clients[iPlayer - 1];

	return Client.pViewEntity == Client.edict;
}
