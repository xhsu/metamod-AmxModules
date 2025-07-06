import std;
import hlsdk;

import UtlHook;

import GameRules;


// CsWpn.cpp
extern void ClearNewWeapons() noexcept;
//


inline constexpr size_t VFTIDX_CHalfLifeMultiplay_CleanUpMap = 63;
using fnCleanUpMap_t = void(__thiscall*)(CHalfLifeMultiplay*) noexcept;
inline fnCleanUpMap_t g_pfnCleanUpMap = nullptr;


static void __fastcall HamF_GameRule_CleanUpMap(CHalfLifeMultiplay* pGameRule, void* edx) noexcept
{
	ClearNewWeapons();
	return g_pfnCleanUpMap(pGameRule);
}

void DeployRoundHook() noexcept
{
	static bool bHooked = false;
	if (bHooked) [[likely]]
		return;

	auto const vft = UTIL_RetrieveVirtualFunctionTable(g_pGameRules);

	UTIL_VirtualTableInjection(vft, VFTIDX_CHalfLifeMultiplay_CleanUpMap, &HamF_GameRule_CleanUpMap, (void**)&g_pfnCleanUpMap);

	bHooked = true;
}
