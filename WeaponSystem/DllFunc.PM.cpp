import std;
import hlsdk;

import CBase;

import Plugin;

/*

Purpose: Ensure the materialized weapon won't block player walking, but still able to hit by traceline.

Nagi:

pfnCmdStart
SV_PlayerRunPreThink
SV_PlayerRunThink
SV_AddLinksToPM
pfnPM_Move Post 修改成SOLID_TRIGGER，SetOrigin - 因为在这里触发的玩家touch
pfnPlayerPostThink Pre只恢复SOLID
pfnCmdEnd

*/

static bool g_bShouldRestore = false;

META_RES fw_PM_Move(playermove_t* ppmove, qboolean server) noexcept
{
	if (ppmove->spectator)
		return MRES_IGNORED;

	static std::vector<size_t> rgiPhysEntsIndex{};
	rgiPhysEntsIndex.clear();
	//rgiPhysEntsIndex.reserve(MAX_PHYSENTS);	// 'reserve' have a good use only in the very first call

	for (size_t i = 0; i < (size_t)ppmove->numphysent; ++i)
	{
		[[unlikely]]
		if (ppmove->physents[i].info == 0)	// entindex == 0 is the WORLD, mate.
		{
			rgiPhysEntsIndex.emplace_back(i);
			continue;
		}

		// kick the weaponbox out of phyents list is still needed.
		// 'cause we wish it blocks no player.
		if (ppmove->physents[i].vuser4.z == 9527)
			continue;

		rgiPhysEntsIndex.emplace_back(i);
	}

	for (size_t i = 0; i < rgiPhysEntsIndex.size(); ++i)
	{
		if (i != rgiPhysEntsIndex[i])
			ppmove->physents[i] = ppmove->physents[rgiPhysEntsIndex[i]];
	}

	ppmove->numphysent = std::ssize(rgiPhysEntsIndex);
	return MRES_HANDLED;
}

void fw_PM_Move_Post(playermove_t* ppmove, qboolean server) noexcept
{
	std::span const all_entities{ g_engfuncs.pfnPEntityOfEntIndex(0), (size_t)gpGlobals->maxEntities };

	for (auto&& ent : all_entities)
	{
		if (ent.v.vuser4.z == 9527)
		{
			ent.v.solid = SOLID_TRIGGER;
			g_engfuncs.pfnSetOrigin(&ent, ent.v.origin);	// Purpose: Calling SV_LinkEdict(e, false)
		}
	}

	g_bShouldRestore = true;
}

META_RES fw_PlayerPostThink(edict_t*) noexcept
{
	if (!g_bShouldRestore)
		return MRES_IGNORED;

	std::span const all_entities{ g_engfuncs.pfnPEntityOfEntIndex(0), (size_t)gpGlobals->maxEntities };

	for (auto&& ent : all_entities)
	{
		if (ent.v.vuser4.z == 9527)
		{
			ent.v.solid = SOLID_BBOX;
			g_engfuncs.pfnSetOrigin(&ent, ent.v.origin);	// Purpose: Calling SV_LinkEdict(e, false)
		}
	}

	g_bShouldRestore = false;
	return MRES_HANDLED;
}

qboolean fw_AddToFullPack_Post(entity_state_t* pState, int iEntIndex, edict_t* pEdict, edict_t* pClientSendTo, qboolean cl_lw, qboolean bIsPlayer, unsigned char* pSet) noexcept
{
	gpMetaGlobals->mres = MRES_IGNORED;
	// post

	// Filter out weaponbox. Match the client prediction with the server action.
	if (pClientSendTo->v.deadflag == DEAD_NO && pEdict->v.vuser4.z == 9527)
	{
		pState->solid = SOLID_NOT;
	}

	return false;
}
