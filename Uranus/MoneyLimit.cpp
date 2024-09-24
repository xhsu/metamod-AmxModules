import std;

import Plugin;

import Uranus;

import UtlHook;

void DeployMoneyHook() noexcept
{
	static bool bHookPerformed = false;
	
	[[likely]]
	if (bHookPerformed)
		return;

	bHookPerformed = true;

	// CheckStartMoney

	constexpr ptrdiff_t CSM_Ofs_UpperCap_Int32 = 0x10061099 - 0x10061090;	// upper cap
	constexpr ptrdiff_t CSM_Ofs_UpperCap_Flt32 = 0x100610A3 - 0x10061090;
	constexpr ptrdiff_t CSM_Ofs_LowerCap_Int32 = 0x100610B7 - 0x10061090;	// lower cap
	constexpr ptrdiff_t CSM_Ofs_LowerCap_Flt32 = 0x100610C1 - 0x10061090;

	auto const CSM_UpperCap_Int32_Addr = (intptr_t)gUranusCollection.pfnCheckStartMoney + CSM_Ofs_UpperCap_Int32;
	auto const CSM_UpperCap_Flt32_Addr = (intptr_t)gUranusCollection.pfnCheckStartMoney + CSM_Ofs_UpperCap_Flt32;
	auto const CSM_LowerCap_Int32_Addr = (intptr_t)gUranusCollection.pfnCheckStartMoney + CSM_Ofs_LowerCap_Int32;
	auto const CSM_LowerCap_Flt32_Addr = (intptr_t)gUranusCollection.pfnCheckStartMoney + CSM_Ofs_LowerCap_Flt32;

	UTIL_WriteMemory((void*)CSM_UpperCap_Int32_Addr, 99999);
	UTIL_WriteMemory((void*)CSM_UpperCap_Flt32_Addr, 99999.f);
	UTIL_WriteMemory((void*)CSM_LowerCap_Int32_Addr, 0);
	UTIL_WriteMemory((void*)CSM_LowerCap_Flt32_Addr, 0.f);

	// CBasePlayer::AddAccount

	constexpr ptrdiff_t CBPAA_Ofs_UpperCap_Int32 = 0x10093DF4 - 0x10093DD0;

	auto const CBPAA_Int32_Addr = (intptr_t)gUranusCollection.pfnAddAccount + CBPAA_Ofs_UpperCap_Int32;

	UTIL_WriteMemory((void*)CBPAA_Int32_Addr, 99999);
}
