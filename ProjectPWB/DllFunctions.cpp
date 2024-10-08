import std;
import hlsdk;

import FileSystem;
import Hook;
import Plugin;
import Task;
import Uranus;


static bool g_bShouldPrecache = true;

void fw_GameInit_Post() noexcept
{
	// Execute only once.

	Uranus::RetrieveUranusLocal();
	FileSystem::Init();
	PrecacheModelInfo();	// not actually precaching. Must behind filesystem.
}

auto fw_Spawn_Post(edict_t* pEdict) noexcept -> qboolean
{
	[[likely]]
	if (!g_bShouldPrecache)
		return 0;

	g_bShouldPrecache = false;
	return 0;
}

void fw_Touch_Post(edict_t* pentTouched, edict_t* pentOther) noexcept
{
	// Purpose: resolve the 'stuck in the air' situation.

	if (pentTouched->v.classname != pentOther->v.classname)
		return;

	if (std::strcmp(STRING(pentTouched->v.classname), "weaponbox"))
		return;

	if ((pentTouched->v.flags & FL_ONGROUND) && (pentOther->v.flags & FL_ONGROUND))
		return;

	Vector vecVel{
		g_engfuncs.pfnRandomFloat(-1.f, 1.f),
		g_engfuncs.pfnRandomFloat(-1.f, 1.f),
		g_engfuncs.pfnRandomFloat(-0.5f, 1.f),
	};
	vecVel = vecVel.Normalize() * g_engfuncs.pfnRandomFloat(50.f, 60.f);

	pentTouched->v.velocity = vecVel;
	pentOther->v.velocity = -vecVel;
}

void fw_ServerActivate_Post(edict_t* pEdictList, int edictCount, int clientMax) noexcept
{
	static bool bHooked = false;

	[[unlikely]]
	if (!bHooked)
	{
		HookInfo::DefaultDeploy.ApplyOn(gUranusCollection.pfnDefaultDeploy);
		DeployVftInjection();

		bHooked = true;
	}
}

void fw_ServerDeactivate_Post() noexcept
{
	g_bShouldPrecache = true;
	TaskScheduler::Clear();
}

/*

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

		if (ppmove->physents[i].fuser4 == 9527.f)
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
	std::span all_entities{ g_engfuncs.pfnPEntityOfEntIndex(0), (size_t)gpGlobals->maxEntities };

	for (auto&& ent : all_entities)
	{
		if (ent.v.fuser4 == 9527.f)
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

	std::span all_entities{ g_engfuncs.pfnPEntityOfEntIndex(0), (size_t)gpGlobals->maxEntities };

	for (auto&& ent : all_entities)
	{
		if (ent.v.fuser4 == 9527.f)
		{
			ent.v.solid = SOLID_BBOX;
			g_engfuncs.pfnSetOrigin(&ent, ent.v.origin);	// Purpose: Calling SV_LinkEdict(e, false)
		}
	}

	g_bShouldRestore = false;
	return MRES_HANDLED;
}

// irrelevent with the solid hacking.
void fw_PlayerPostThink_Post(edict_t* pEdict) noexcept
{
	if (!pEdict->pvPrivateData)
		return;

	CBasePlayer* pPlayer = ent_cast<CBasePlayer*>(pEdict);

	for (auto pWeapon : pPlayer->m_rgpPlayerItems)
	{
		for (; pWeapon; pWeapon = pWeapon->m_pNext)
		{
			if (FClassnameIs(pWeapon->pev, "weapon_ak47"))
			{
			}
		}
	}
}

qboolean fw_AddToFullPack_Post(entity_state_t* pState, int iEntIndex, edict_t* pEdict, edict_t* pClientSendTo, qboolean cl_lw, qboolean bIsPlayer, unsigned char* pSet) noexcept
{
	gpMetaGlobals->mres = MRES_IGNORED;

	// Filter out weaponbox. Match the client prediction with the server action.
	if (pClientSendTo->v.deadflag == DEAD_NO && pEdict->v.fuser4 == 9527.f)
	{
		pState->solid = SOLID_NOT;
	}

	return false;
}
