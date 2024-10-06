module;

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

export module WinAPI;

import std;

import UtlHook;

export inline decltype(std::invoke(&MH_GetModuleBase, HMODULE{})) gSelfModuleBase{};
export inline decltype(std::invoke(&MH_GetModuleBase, HMODULE{})) gSelfModuleSize{};
export inline HMODULE gSelfModuleHandle{};


// Is the RTTI store in my module?
export inline bool UTIL_IsLocalRtti(void* object) noexcept
{
	auto const vft = *(decltype(gSelfModuleBase)*)object;

	return vft >= gSelfModuleBase && vft <= (gSelfModuleBase + gSelfModuleSize);
}
