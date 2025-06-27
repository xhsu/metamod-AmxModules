#ifdef __INTELLISENSE__
import std;
#else
import std.compat;	// #MSVC_BUG_STDCOMPAT
#endif
import hlsdk;

import CBase;
import Prefab;
import Uranus;
import Query;

import Hook;
import WinAPI;


// CsWpn.cpp
struct G18C_VER2;
extern template void LINK_ENTITY_TO_CLASS<G18C_VER2>(entvars_t* pev) noexcept;
struct USP2;
extern template void LINK_ENTITY_TO_CLASS<USP2>(entvars_t* pev) noexcept;
//


PFN_ENTITYINIT __cdecl OrpheuF_GetDispatch(char const* pszClassName) noexcept
{
	std::string_view const szClassname{ pszClassName };

	if (szClassname == "weapon_glock18")
		return &LINK_ENTITY_TO_CLASS<G18C_VER2>;
	else if (szClassname == "weapon_usp")
		return &LINK_ENTITY_TO_CLASS<USP2>;

	return HookInfo::GetDispatch(pszClassName);
}

void __fastcall OrpheuF_DropPlayerItem(CBasePlayer* pPlayer, void* edx, char const* pszItemName) noexcept
{
	if (!strlen(pszItemName) && UTIL_IsLocalRtti(pPlayer->m_pActiveItem))
	{
		pPlayer->m_pActiveItem->Drop();
		return;
	}

	return HookInfo::DropPlayerItem(pPlayer, edx, pszItemName);
}

void __cdecl OrpheuF_packPlayerItem(CBasePlayer* pPlayer, CBasePlayerItem* pItem, bool packAmmo) noexcept
{
	return HookInfo::packPlayerItem(pPlayer, pItem, packAmmo);
}

void DeployInlineHooks() noexcept
{
	HookInfo::GetDispatch.ApplyOn(HW::GetDispatch::pfn);
	HookInfo::DropPlayerItem.ApplyOn(Uranus::BasePlayer::DropPlayerItem::pfn);
	HookInfo::packPlayerItem.ApplyOn(Uranus::packPlayerItem::pfn);
}

void RestoreInlineHooks() noexcept
{
	HookInfo::GetDispatch.UndoPatch();
	HookInfo::DropPlayerItem.UndoPatch();
	HookInfo::packPlayerItem.UndoPatch();
}
