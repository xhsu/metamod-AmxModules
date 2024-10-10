import std;
import hlsdk;

import Entities;
import FileSystem;
import Hook;
import Plugin;
import Task;
import Uranus;
import ConsoleVar;

import UtlString;


static bool g_bShouldPrecache = true;

void fw_GameInit_Post() noexcept
{
	// Execute only once.

	Uranus::RetrieveUranusLocal();
	FileSystem::Init();
	ListPwbModels();	// not actually precaching. Must behind filesystem.
}

auto fw_Spawn_Post(edict_t* pEdict) noexcept -> qboolean
{
	[[likely]]
	if (!g_bShouldPrecache)
		return 0;

	g_engfuncs.pfnPrecacheSound(WEAPONBOX_SFX_DROP);
	g_engfuncs.pfnPrecacheSound(WEAPONBOX_SFX_HIT);

	for (auto&& [szClass, info] : gBackModelRpl)
		g_engfuncs.pfnPrecacheModel(info.m_model.c_str());

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

META_RES OnClientCommand(CBasePlayer* pPlayer, std::string_view szCmd) noexcept
{
	if (szCmd == "take_your")
	{
		if (g_engfuncs.pfnCmd_Argc() != 2)
			return MRES_SUPERCEDE;

		auto const iSlot = UTIL_StrToNum<int>(g_engfuncs.pfnCmd_Argv(1));

		if (iSlot < 1 || iSlot > 5)
			return MRES_SUPERCEDE;

		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + pPlayer->pev->v_angle.Front() * 8192.0;

		TraceResult tr{};
		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, dont_ignore_glass | dont_ignore_monsters, pPlayer->edict(), &tr);

		if (pev_valid(tr.pHit) != EValidity::Full)
			return MRES_SUPERCEDE;

		EHANDLE<CBaseEntity> pEnt{ tr.pHit };
		if (auto const pTarget = pEnt.As<CBasePlayer>())
		{
			if (pTarget->m_rgpPlayerItems[iSlot] != nullptr)
				Uranus::BasePlayer::SelectItem{}(pTarget, STRING(pTarget->m_rgpPlayerItems[iSlot]->pev->classname));
		}

		return MRES_SUPERCEDE;
	}
	else if (szCmd == "drop_it")
	{
		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + pPlayer->pev->v_angle.Front() * 8192.0;

		TraceResult tr{};
		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, dont_ignore_glass | dont_ignore_monsters, pPlayer->edict(), &tr);

		if (pev_valid(tr.pHit) != EValidity::Full)
			return MRES_SUPERCEDE;

		EHANDLE<CBaseEntity> pEnt{ tr.pHit };
		if (auto const pTarget = pEnt.As<CBasePlayer>())
		{
			Uranus::BasePlayer::DropPlayerItem{}(
				pTarget,
				STRING(pTarget->m_pActiveItem->pev->classname)
				);
		}

		return MRES_SUPERCEDE;
	}
	else if (szCmd == "give_you")	// #BUGBUG crash if switched.
	{
		if (g_engfuncs.pfnCmd_Argc() != 2)
			return MRES_SUPERCEDE;

		auto const iStr = MAKE_STRING_UNSAFE(g_engfuncs.pfnCmd_Argv(1));

		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + pPlayer->pev->v_angle.Front() * 8192.0;

		TraceResult tr{};
		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, dont_ignore_glass | dont_ignore_monsters, pPlayer->edict(), &tr);

		if (pev_valid(tr.pHit) != EValidity::Full)
			return MRES_SUPERCEDE;

		EHANDLE<CBaseEntity> pEnt{ tr.pHit };
		if (auto const pTarget = pEnt.As<CBasePlayer>())
		{
			// This is just a modified version of CBP::GiveNamedItem()

			auto const pEdict = g_engfuncs.pfnCreateNamedEntity(iStr);
			if (pev_valid(pEdict) != EValidity::Full)
				return MRES_SUPERCEDE;

			pEdict->v.origin = pTarget->pev->origin;
			pEdict->v.spawnflags |= SF_NORESPAWN;

			gpGamedllFuncs->dllapi_table->pfnSpawn(pEdict);

			auto const iSolid = pEdict->v.solid;
			gpGamedllFuncs->dllapi_table->pfnTouch(pEdict, pTarget->edict());

			if (iSolid != pEdict->v.solid)
				return MRES_SUPERCEDE;

			pEdict->v.flags |= FL_KILLME;
			return MRES_SUPERCEDE;
		}
	}

	return MRES_IGNORED;
}

void fw_ServerActivate_Post(edict_t* pEdictList, int edictCount, int clientMax) noexcept
{
	CVarManager::Init();

	static bool bHooked = false;

	[[unlikely]]
	if (!bHooked)
	{
		HookInfo::DefaultDeploy.ApplyOn(gUranusCollection.pfnDefaultDeploy);
		DeployVftInjection();
		DeployWeaponBoxHook();

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

		// kick the weaponbox out of phyents list is still needed.
		// 'cause we wish it blocks no player.
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
