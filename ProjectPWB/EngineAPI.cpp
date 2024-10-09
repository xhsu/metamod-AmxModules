import std;
import hlsdk;

import CBase;
import Configs;
import Entities;
import Hook;
import Plugin;


auto fw_PrecacheModel(const char* psz) noexcept -> int
{
	gpMetaGlobals->mres = MRES_IGNORED;
	// pre

	if (gWorldModelRpl.contains(psz))
	{
		gpMetaGlobals->mres = MRES_SUPERCEDE;
		return g_engfuncs.pfnPrecacheModel(THE_BPW_MODEL);
	}

	return -1;
}

void fw_SetModel(edict_t* pEdict, const char* pszModel) noexcept
{
	gpMetaGlobals->mres = MRES_IGNORED;
	// pre

	if (gWorldModelRpl.contains(pszModel))
	{
		gpMetaGlobals->mres = MRES_SUPERCEDE;
		g_engfuncs.pfnSetModel(pEdict, THE_BPW_MODEL);
		pEdict->v.body = gWorldModelRpl.at(pszModel);

		// The followings are weaponbox phys

		Materialization(&pEdict->v);

		EHANDLE<CBasePlayer> pPlayer{ pEdict->v.owner };
		if (pPlayer && pPlayer->IsAlive())
		{
			g_engfuncs.pfnSetOrigin(pEdict, UTIL_GetPlayerFront(pPlayer->pev, 64.f));
			pEdict->v.velocity = pPlayer->pev->v_angle.Front() * (float)cvar_throwingweaponvelocity;
		}

		FreeRotationInTheAir(&pEdict->v);
	}
}

auto fw_ModelIndex(const char* pszModel) noexcept -> int
{
	gpMetaGlobals->mres = MRES_IGNORED;
	// pre

	if (gWorldModelRpl.contains(pszModel))
	{
		gpMetaGlobals->mres = MRES_SUPERCEDE;
		return g_engfuncs.pfnModelIndex(THE_BPW_MODEL);
	}

	return -1;
}
