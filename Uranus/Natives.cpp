import std;

import Plugin;

import CBase;
import Uranus;

// native Uranus_DropShield(iPlayer);
static cell Native_DropShield(AMX* amx, cell* params) noexcept
{
	auto const iPlayer = params[1];

	auto const pPlayer = ent_cast<CBasePlayer*>(iPlayer);
	Uranus::BasePlayer::DropShield{}(pPlayer, true);

	return true;
}

// native bool:Uranus_CanPlayerBuy(iPlayer);
static cell Native_CanPlayerBuy(AMX* amx, cell* params) noexcept
{
	auto const iPlayer = params[1];

	auto const pPlayer = ent_cast<CBasePlayer*>(iPlayer);
	auto const result = Uranus::BasePlayer::CanPlayerBuy{}(pPlayer, true);

	return (cell)result;
}

void DeployNatives() noexcept
{
	static constexpr AMX_NATIVE_INFO rgAmxNativeInfo[] =
	{
		{ "Uranus_DropShield",		&Native_DropShield },
		{ "Uranus_CanPlayerBuy",	&Native_CanPlayerBuy },

		{ nullptr, nullptr },
	};

	MF_AddNatives(rgAmxNativeInfo);
}
