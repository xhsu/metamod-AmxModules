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
extern template void LINK_ENTITY_TO_CLASS<struct CPistolGlock>(entvars_t* pev) noexcept;
extern template void LINK_ENTITY_TO_CLASS<struct CPistolUSP>(entvars_t* pev) noexcept;
extern template void LINK_ENTITY_TO_CLASS<struct CPistolP228>(entvars_t* pev) noexcept;
extern template void LINK_ENTITY_TO_CLASS<struct CPistolDeagle>(entvars_t* pev) noexcept;
extern template void LINK_ENTITY_TO_CLASS<struct CPistolFN57>(entvars_t* pev) noexcept;
extern template void LINK_ENTITY_TO_CLASS<struct CPistolBeretta>(entvars_t* pev) noexcept;
//

// Buy.cpp
extern bool Buy_HandleBuyAliasCommands(CBasePlayer* pPlayer, std::string_view szCommand) noexcept;
extern bool Buy_GunAmmo(CBasePlayer* pPlayer, CBasePlayerWeapon* pWeapon, bool bBlinkMoney) noexcept;
extern bool Buy_Equipment(CBasePlayer* pPlayer, int iSlot) noexcept;
//


PFN_ENTITYINIT __cdecl OrpheuF_GetDispatch(char const* pszClassName) noexcept
{
	std::string_view const szClassname{ pszClassName };

	if (szClassname == "weapon_glock18")
		return &LINK_ENTITY_TO_CLASS<CPistolGlock>;
	else if (szClassname == "weapon_usp")
		return &LINK_ENTITY_TO_CLASS<CPistolUSP>;
	else if (szClassname == "weapon_p228")
		return &LINK_ENTITY_TO_CLASS<CPistolP228>;
	else if (szClassname == "weapon_deagle")
		return &LINK_ENTITY_TO_CLASS<CPistolDeagle>;
	else if (szClassname == "weapon_fiveseven")
		return &LINK_ENTITY_TO_CLASS<CPistolFN57>;
	else if (szClassname == "weapon_elite")
		return &LINK_ENTITY_TO_CLASS<CPistolBeretta>;

	return HookInfo::GetDispatch(pszClassName);
}

void __fastcall OrpheuF_DropPlayerItem(CBasePlayer* pPlayer, void* edx, char const* pszItemName) noexcept
{
	if (strlen(pszItemName) == 0)
	{
		if (pPlayer->HasShield())
		{
			pPlayer->DropShield();
			return;
		}
		else if (UTIL_IsLocalRtti(pPlayer->m_pActiveItem))
		{
			pPlayer->m_pActiveItem->Drop();
			return;
		}
	}
	else
	{
		for (CBasePlayerWeapon* pWeapon : Query::all_weapons_belongs_to(pPlayer))
		{
			if (UTIL_IsLocalRtti(pWeapon) && strcmp(STRING(pWeapon->pev->classname), pszItemName) == 0)
			{
				pWeapon->Drop();
				return;
			}
		}
	}

	return HookInfo::DropPlayerItem(pPlayer, edx, pszItemName);
}

void __cdecl OrpheuF_packPlayerItem(CBasePlayer* pPlayer, CBasePlayerItem* pItem, bool packAmmo) noexcept
{
	if (UTIL_IsLocalRtti(pItem))
	{
		pItem->Drop();
		return;
	}

	return HookInfo::packPlayerItem(pPlayer, pItem, packAmmo);
}

qboolean __cdecl OrpheuF_HandleBuyAliasCommands(CBasePlayer* player, const char* pszCommand) noexcept
{
	if (Buy_HandleBuyAliasCommands(player, pszCommand))
		return true;

	return HookInfo::HandleBuyAliasCommands(player, pszCommand);
}

bool __cdecl OrpheuF_BuyGunAmmo(CBasePlayer* player, CBasePlayerItem* weapon, bool bBlinkMoney) noexcept
{
	return Buy_GunAmmo(
		player,
#ifdef _DEBUG
		dynamic_cast<CBasePlayerWeapon*>(weapon),
#else
		static_cast<CBasePlayerWeapon*>(weapon),
#endif
		bBlinkMoney
	);
}

void DeployInlineHooks() noexcept
{
	HookInfo::GetDispatch.ApplyOn(HW::GetDispatch::pfn);
	HookInfo::DropPlayerItem.ApplyOn(Uranus::BasePlayer::DropPlayerItem::pfn);
	HookInfo::packPlayerItem.ApplyOn(Uranus::packPlayerItem::pfn);
	HookInfo::HandleBuyAliasCommands.ApplyOn(Uranus::HandleBuyAliasCommands::pfn);
	HookInfo::BuyGunAmmo.ApplyOn(Uranus::BuyGunAmmo::pfn);
}

void RestoreInlineHooks() noexcept
{
	HookInfo::GetDispatch.UndoPatch();
	HookInfo::DropPlayerItem.UndoPatch();
	HookInfo::packPlayerItem.UndoPatch();
	HookInfo::HandleBuyAliasCommands.UndoPatch();
	HookInfo::BuyGunAmmo.UndoPatch();
}
