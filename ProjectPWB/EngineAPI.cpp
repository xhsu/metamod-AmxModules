import std;
import hlsdk;

import CBase;
import Configs;
import Entities;
import Hook;
import Plugin;

import UtlRandom;



auto fw_PrecacheModel(const char* psz) noexcept -> int
{
	gpMetaGlobals->mres = MRES_IGNORED;
	// pre

	if (gRplInfo.contains(psz))
	{
		gpMetaGlobals->mres = MRES_SUPERCEDE;
		return g_engfuncs.pfnPrecacheModel(gRplInfo.at(psz).m_model.c_str());
	}

	return -1;
}

void fw_SetModel(edict_t* pEdict, const char* pszModel) noexcept
{
	gpMetaGlobals->mres = MRES_IGNORED;
	// pre

	if (gRplInfo.contains(pszModel))
	{
		auto& info = gRplInfo.at(pszModel);

		gpMetaGlobals->mres = MRES_SUPERCEDE;
		g_engfuncs.pfnSetModel(pEdict, info.m_model.c_str());
		pEdict->v.body = info.m_body;


		pEdict->v.sequence = info.m_seq;
		pEdict->v.framerate = 1.f;
		pEdict->v.animtime = gpGlobals->time;

		// The followings are weaponbox phys

		Materialization(&pEdict->v);

		EHANDLE<CBasePlayer> pPlayer{ pEdict->v.owner };
		if (pPlayer && pPlayer->IsAlive())
		{
			g_engfuncs.pfnSetOrigin(pEdict, UTIL_GetPlayerFront(pPlayer->pev, 64.f));
			pEdict->v.velocity = pPlayer->pev->v_angle.Front() * (float)cvar_throwingweaponvelocity;
		}

		FreeRotationInTheAir(&pEdict->v);

		// Rotating on ground init.
		pEdict->v.armorvalue = UTIL_Random(0.f, 360.f);
		auto const pWeaponBox = ent_cast<CBaseEntity*>(pEdict);
		pWeaponBox->m_flReleaseThrow = UTIL_Random(0.5f, 2.f) * (UTIL_Random() ? -1.f : 1.f);
	}
}

auto fw_ModelIndex(const char* pszModel) noexcept -> int
{
	gpMetaGlobals->mres = MRES_IGNORED;
	// pre

	if (gRplInfo.contains(pszModel))
	{
		gpMetaGlobals->mres = MRES_SUPERCEDE;
		return g_engfuncs.pfnModelIndex(gRplInfo.at(pszModel).m_model.c_str());
	}

	return -1;
}
