import std;
import hlsdk;

import CBase;
import Uranus;
import Message;

import Ammo;


bool Buy_HandleBuyAliasCommands(CBasePlayer* pPlayer, std::string_view szCommand) noexcept
{
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
