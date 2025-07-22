#ifdef __INTELLISENSE__
import std;
#else
import std.compat;
#endif
import hlsdk;

import CBase;
import Plugin;


META_RES OnClientCommand(CBasePlayer* pPlayer, std::string_view szCmd) noexcept
{
	if (szCmd == "give_me")
	{
		if (g_engfuncs.pfnCmd_Argc() != 2)
			return MRES_SUPERCEDE;

		auto const iStr = MAKE_STRING_UNSAFE(g_engfuncs.pfnCmd_Argv(1));

		// This is just a modified version of CBP::GiveNamedItem()

		auto const pEdict = g_engfuncs.pfnCreateNamedEntity(iStr);
		if (pev_valid(pEdict) != EValidity::Full)
			return MRES_SUPERCEDE;

		pEdict->v.origin = pPlayer->pev->origin;
		pEdict->v.spawnflags |= SF_NORESPAWN;

		gpGamedllFuncs->dllapi_table->pfnSpawn(pEdict);

		auto const iSolid = pEdict->v.solid;
		gpGamedllFuncs->dllapi_table->pfnTouch(pEdict, pPlayer->edict());

		if (iSolid != pEdict->v.solid)
			return MRES_SUPERCEDE;

		pEdict->v.flags |= FL_KILLME;
		return MRES_SUPERCEDE;
	}
	else if (szCmd == "give_shield")
	{
		if (pPlayer->HasShield())
			return MRES_SUPERCEDE;

		pPlayer->GiveShield();
		return MRES_SUPERCEDE;
	}

	return MRES_IGNORED;
}
