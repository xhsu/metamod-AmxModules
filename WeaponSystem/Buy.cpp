import std;
import hlsdk;

import CBase;
import GameRules;
import Message;
import PlayerItem;
import Uranus;

import Ammo;
import Buy;



/*
WeaponBuyAliasInfo weaponBuyAliasInfo[] =
{
	{ "galil", WEAPON_GALIL, "#Galil" },
	{ "defender", WEAPON_GALIL, "#Galil" },
	{ "ak47", WEAPON_AK47, "#AK47" },
	{ "cv47", WEAPON_AK47, "#AK47" },
	{ "scout", WEAPON_SCOUT, NULL },
	{ "sg552", WEAPON_SG552, "#SG552" },
	{ "krieg552", WEAPON_SG552, "#SG552" },
	{ "awp", WEAPON_AWP, NULL },
	{ "magnum", WEAPON_AWP, NULL },
	{ "g3sg1", WEAPON_G3SG1, "#G3SG1" },
	{ "d3au1", WEAPON_G3SG1, "#G3SG1" },
	{ "famas", WEAPON_FAMAS, "#Famas" },
	{ "clarion", WEAPON_FAMAS, "#Famas" },
	{ "m4a1", WEAPON_M4A1, "#M4A1" },
	{ "aug", WEAPON_AUG, "#Aug" },
	{ "bullpup", WEAPON_AUG, "#Aug" },
	{ "sg550", WEAPON_SG550, "#SG550" },
	{ "krieg550", WEAPON_SG550, "#SG550" },
	{ "glock", WEAPON_GLOCK18, NULL },
	{ "9x19mm", WEAPON_GLOCK18, NULL },
	{ "usp", WEAPON_USP, NULL },
	{ "km45", WEAPON_USP, NULL },
	{ "p228", WEAPON_P228, NULL },
	{ "228compact", WEAPON_P228, NULL },
	{ "deagle", WEAPON_DEAGLE, NULL },
	{ "nighthawk", WEAPON_DEAGLE, NULL },
	{ "elites", WEAPON_ELITE, "#Beretta96G" },
	{ "fn57", WEAPON_FIVESEVEN, "#FiveSeven" },
	{ "fiveseven", WEAPON_FIVESEVEN, "#FiveSeven" },
	{ "m3", WEAPON_M3, NULL },
	{ "12gauge", WEAPON_M3, NULL },
	{ "xm1014", WEAPON_XM1014, NULL },
	{ "autoshotgun", WEAPON_XM1014, NULL },
	{ "mac10", WEAPON_MAC10, "#Mac10" },
	{ "tmp", WEAPON_TMP, "#tmp" },
	{ "mp", WEAPON_TMP, "#tmp" },
	{ "mp5", WEAPON_MP5N, NULL },
	{ "smg", WEAPON_MP5N, NULL },
	{ "ump45", WEAPON_UMP45, NULL },
	{ "p90", WEAPON_P90, NULL },
	{ "c90", WEAPON_P90, NULL },
	{ "m249", WEAPON_M249, NULL },
	{ NULL, 0 }
};
*/

static constexpr bool CanBuyWeaponByMaptype(ECsTeams iTeam, WeaponIdType iId, bool bAssassinationGame) noexcept
{
	if (bAssassinationGame)
	{
		if (iTeam == TEAM_CT)
		{
			switch (iId)
			{
			case WEAPON_P228:
			case WEAPON_XM1014:
			case WEAPON_AUG:
			case WEAPON_FIVESEVEN:
			case WEAPON_UMP45:
			case WEAPON_SG550:
			case WEAPON_FAMAS:
			case WEAPON_USP:
			case WEAPON_GLOCK18:
			case WEAPON_MP5N:
			case WEAPON_M249:
			case WEAPON_M3:
			case WEAPON_M4A1:
			case WEAPON_TMP:
			case WEAPON_DEAGLE:
			case WEAPON_P90:
			case WEAPON_SHIELDGUN:
				return true;
			default:
				return false;
			}
		}
		else if (iTeam == TEAM_TERRORIST)
		{
			switch (iId)
			{
			case WEAPON_P228:
			case WEAPON_MAC10:
			case WEAPON_ELITE:
			case WEAPON_UMP45:
			case WEAPON_GALIL:
			case WEAPON_USP:
			case WEAPON_GLOCK18:
			case WEAPON_AWP:
			case WEAPON_DEAGLE:
			case WEAPON_AK47:
				return true;
			default:
				return false;
			}
		}

		return false;
	}
	if (iTeam == TEAM_CT)
	{
		switch (iId)
		{
		case WEAPON_P228:
		case WEAPON_SCOUT:
		case WEAPON_XM1014:
		case WEAPON_AUG:
		case WEAPON_FIVESEVEN:
		case WEAPON_UMP45:
		case WEAPON_SG550:
		case WEAPON_FAMAS:
		case WEAPON_USP:
		case WEAPON_GLOCK18:
		case WEAPON_AWP:
		case WEAPON_MP5N:
		case WEAPON_M249:
		case WEAPON_M3:
		case WEAPON_M4A1:
		case WEAPON_TMP:
		case WEAPON_DEAGLE:
		case WEAPON_P90:
		case WEAPON_SHIELDGUN:
			return true;
		default:
			return false;
		}
	}
	else if (iTeam == TEAM_TERRORIST)
	{
		switch (iId)
		{
		case WEAPON_P228:
		case WEAPON_SCOUT:
		case WEAPON_XM1014:
		case WEAPON_MAC10:
		case WEAPON_ELITE:
		case WEAPON_UMP45:
		case WEAPON_GALIL:
		case WEAPON_USP:
		case WEAPON_GLOCK18:
		case WEAPON_AWP:
		case WEAPON_MP5N:
		case WEAPON_M249:
		case WEAPON_M3:
		case WEAPON_G3SG1:
		case WEAPON_DEAGLE:
		case WEAPON_SG552:
		case WEAPON_AK47:
		case WEAPON_P90:
			return true;
		default:
			return false;
		}
	}

	return false;
}


static bool CanBuyThis(CBasePlayer* pPlayer, WeaponIdType iWeapon) noexcept
{
	if (pPlayer->HasShield() && iWeapon == WEAPON_ELITE)
		return false;

	if (pPlayer->HasShield() && iWeapon == WEAPON_SHIELDGUN)
		return false;

	if (pPlayer->m_rgpPlayerItems[2] && pPlayer->m_rgpPlayerItems[2]->m_iId == WEAPON_ELITE && iWeapon == WEAPON_SHIELDGUN)
		return false;

	if (pPlayer->m_rgpPlayerItems[1] && pPlayer->m_rgpPlayerItems[1]->m_iId == iWeapon)
	{
		gmsgTextMsg::Send(pPlayer->edict(), HUD_PRINTCENTER, "#Cstrike_Already_Own_Weapon");
		return false;
	}

	if (pPlayer->m_rgpPlayerItems[2] && pPlayer->m_rgpPlayerItems[2]->m_iId == iWeapon)
	{
		gmsgTextMsg::Send(pPlayer->edict(), HUD_PRINTCENTER, "#Cstrike_Already_Own_Weapon");
		return false;
	}

	if (!CanBuyWeaponByMaptype(pPlayer->m_iTeam, (WeaponIdType)iWeapon, g_pGameRules->m_iMapHasVIPSafetyZone != 0))
	{
		gmsgTextMsg::Send(pPlayer->edict(), HUD_PRINTCENTER, "#Cannot_Buy_This");
		return false;
	}

	return true;
}

extern template CPrefabWeapon* BuyWeaponByCppClass<struct CPistolGlock>(CBasePlayer* pPlayer) noexcept;
extern template CPrefabWeapon* BuyWeaponByCppClass<struct CPistolUSP>(CBasePlayer* pPlayer) noexcept;
extern template CPrefabWeapon* BuyWeaponByCppClass<struct CPistolP228>(CBasePlayer* pPlayer) noexcept;
extern template CPrefabWeapon* BuyWeaponByCppClass<struct CPistolDeagle>(CBasePlayer* pPlayer) noexcept;
extern template CPrefabWeapon* BuyWeaponByCppClass<struct CPistolFN57>(CBasePlayer* pPlayer) noexcept;
extern template CPrefabWeapon* BuyWeaponByCppClass<struct CPistolBeretta>(CBasePlayer* pPlayer) noexcept;

bool Buy_HandleBuyAliasCommands(CBasePlayer* pPlayer, std::string_view szCommand) noexcept
{
	if (szCommand == "glock" || szCommand == "9x19mm")
	{
		BuyWeaponByCppClass<CPistolGlock>(pPlayer);
		return true;
	}
	else if (szCommand == "usp" || szCommand == "km45")
	{
		BuyWeaponByCppClass<CPistolUSP>(pPlayer);
		return true;
	}
	else if (szCommand == "p228" || szCommand == "228compact")
	{
		BuyWeaponByCppClass<CPistolP228>(pPlayer);
		return true;
	}
	else if (szCommand == "deagle" || szCommand == "nighthawk")
	{
		BuyWeaponByCppClass<CPistolDeagle>(pPlayer);
		return true;
	}
	else if (szCommand == "fiveseven" || szCommand == "fn57")
	{
		BuyWeaponByCppClass<CPistolFN57>(pPlayer);
		return true;
	}
	else if (szCommand == "elites")
	{
		BuyWeaponByCppClass<CPistolBeretta>(pPlayer);
		return true;
	}

	return false;
}

bool Buy_GunAmmo(CBasePlayer* pPlayer, CBasePlayerWeapon* pWeapon, bool bBlinkMoney) noexcept
{
	if (!pPlayer->CanPlayerBuy(true))
		return false;

	// Ensure that the weapon uses ammo
	auto const iAmmoIdx = pWeapon->m_iPrimaryAmmoType;
	if (iAmmoIdx == -1)
		return false;

	auto const pAmmoInfo = Ammo_InfoByIndex(iAmmoIdx);

	// Can only buy if the player does not already have full ammo
	if (pPlayer->m_rgAmmo[iAmmoIdx] >= pAmmoInfo->m_iMax)
		return false;

	auto const iCost = std::lroundf(float(pAmmoInfo->m_iMax - pPlayer->m_rgAmmo[iAmmoIdx]) * pAmmoInfo->m_flCost);

	// Purchase the ammo if the player has enough money
	if (pPlayer->m_iAccount >= iCost)
	{
		if (pPlayer->GiveAmmo(pAmmoInfo->m_iMax, (char*)pAmmoInfo->m_szName.data(), pAmmoInfo->m_iMax) == -1)
			return false;

		g_engfuncs.pfnEmitSound(pPlayer->edict(), CHAN_ITEM, "items/9mmclip1.wav", VOL_NORM, ATTN_NORM, SND_FL_NONE, PITCH_NORM);

		Uranus::BasePlayer::AddAccount{}(pPlayer, -iCost);
		return true;
	}

	if (bBlinkMoney)
	{
		// Not enough money.. let the player know
		gmsgTextMsg::Send(pPlayer->edict(), HUD_PRINTCENTER, "#Not_Enough_Money");
		gmsgBlinkAcct::Send(pPlayer->edict(), 2);
	}

	return false;
}

bool Buy_Equipment(CBasePlayer* pPlayer, int iSlot) noexcept
{
	return false;
}
