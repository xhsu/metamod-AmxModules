import std;

import Plugin;

import CBase;
import Uranus;

import util;

// native Uranus_DropShield(iPlayer);
static cell Native_DropShield(AMX* amx, cell* params) noexcept
{
	auto const iPlayer = params[1];

	auto const pPlayer = ent_cast<CBasePlayer*>(iPlayer);
	Uranus::BasePlayer::DropShield{}(pPlayer, true);

	return true;
}

void DeployNatives() noexcept
{
	static constexpr AMX_NATIVE_INFO rgAmxNativeInfo[] =
	{
		{ "Uranus_DropShield",		&Native_DropShield },

		{ nullptr, nullptr },
	};

	MF_AddNatives(rgAmxNativeInfo);
}
