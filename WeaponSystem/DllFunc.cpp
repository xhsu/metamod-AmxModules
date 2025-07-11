#include <assert.h>	// #UPDATE_AT_CPP26 contract

import std;
import hlsdk;

import CBase;
import ConditionZero;
import Decal;
import Engine;
import FileSystem;
import GameRules;
import Message;
import PlayerItem;
import Prefab;
import Resources;
import Server;
import Sprite;
import Task;
import Uranus;
import VTFH;
import ZBot;

import BPW;

import Hook;
import Plugin;
import WinAPI;


// Hook.cpp
extern void DeployInlineHooks() noexcept;
extern void RestoreInlineHooks() noexcept;
//

// Round.cpp
extern void DeployRoundHook() noexcept;
//

static bool g_bShouldPrecache = true;


void fw_GameInit_Post() noexcept
{
	Uranus::RetrieveUranusLocal();	// As early as possible.
	FileSystem::Init();
	Engine::Init();	// Get engine build number
	TaskScheduler::Policy() = ESchedulerPolicy::UNORDERED;	// It is very likely that we don't need to have it sorted.
	RetrieveServerStatics();

	DeployInlineHooks();

	// post
}

void fw_GameShutdown_Post() noexcept
{
	FileSystem::Shutdown();
	RestoreInlineHooks();
	// post
}

void fw_ServerActivate_Post(edict_t* pEdictList, int edictCount, int clientMax) noexcept
{
	// post

	RetrieveMessageHandles();
	RetrieveGameRules();
	RetrieveCBaseVirtualFn();
	RetrieveConditionZeroVar();
	Decal::RetrieveIndices();
	ZBot::RetrieveManager();

	DeployRoundHook();	// Do it after retrieving gamerules
}

void fw_ServerDeactivate_Post() noexcept
{
	// post

	// Precache should be done across on every map change.
	g_bShouldPrecache = true;

	// CGameRules class is re-install every map change. Hence we should re-hook it everytime.
	g_pGameRules = nullptr;

	// Remove ALL existing tasks.
	TaskScheduler::Clear();
}

auto fw_Spawn(edict_t* pEdict) noexcept -> qboolean
{
	// pre
	gpMetaGlobals->mres = MRES_IGNORED;

	[[likely]]
	if (!g_bShouldPrecache)
		return 0;

	// plugin_precache

	Resource::Precache();
	PrecacheCombinedModels();

	g_bShouldPrecache = false;
	return 0;
}

void fw_UpdateClientData_Post(const edict_t* ent, qboolean sendweapons, clientdata_t* cd) noexcept
{
	// post

	if (auto const pPlayer = reinterpret_cast<CBasePlayer*>(ent->pvPrivateData);
		pPlayer->IsPlayer())
	{
		if (UTIL_IsLocalRtti(pPlayer->m_pActiveItem))
		{
#ifdef _DEBUG
			auto const pWeapon = dynamic_cast<CPrefabWeapon*>(pPlayer->m_pActiveItem);
			assert(pWeapon != nullptr);
			pWeapon->UpdateClientData(cd);
#else
			auto const pWeapon = static_cast<CPrefabWeapon*>(pPlayer->m_pActiveItem);
			pWeapon->UpdateClientData(cd);
#endif
		}
	}
}

void fw_OnFreeEntPrivateData(edict_t* pEdict) noexcept
{
	gpMetaGlobals->mres = MRES_IGNORED;
	// pre

	[[likely]]
	if (auto const pEntity = (CBaseEntity*)pEdict->pvPrivateData; pEntity != nullptr)
	{
		if (gpMetaGlobals->prev_mres == MRES_SUPERCEDE	// It had been handled by other similar plugins.
			|| !UTIL_IsLocalRtti(pEdict->pvPrivateData))
		{
			return;
		}

		// No [[unlikely]] prediction anymore, because it's actually very likely - RTTI filtered.
		if (auto const pWeapon = dynamic_cast<CPrefabWeapon*>(pEntity); pWeapon != nullptr)
		{
			std::destroy_at(pWeapon);	// Thanks to C++17 we can finally patch up this old game.
			gpMetaGlobals->mres = MRES_SUPERCEDE;
		}
		else if (auto const pPrefab = dynamic_cast<Prefab_t*>(pEntity); pPrefab != nullptr)
		{
			std::destroy_at(pPrefab);
			gpMetaGlobals->mres = MRES_SUPERCEDE;
		}
	}
}

qboolean fw_ShouldCollide(edict_t* pentTouched, edict_t* pentOther) noexcept
{
	gpMetaGlobals->mres = MRES_IGNORED;
	// pre

	if (gpMetaGlobals->prev_mres == MRES_SUPERCEDE
		|| pentTouched->pvPrivateData == nullptr
		|| !UTIL_IsLocalRtti(pentTouched->pvPrivateData))
	{
		// the return value will be ignored anyways, because it's MRES_IGNORED
		return false;
	}

	EHANDLE<CBaseEntity> pEntity(pentTouched);

	// No [[unlikely]] prediction anymore, because it's actually very likely - RTTI filtered.
	if (auto const pNeoWpn = pEntity.As<CPrefabWeapon>(); pNeoWpn)
	{
		gpMetaGlobals->mres = MRES_SUPERCEDE;
		return pNeoWpn->ShouldCollide(pentOther);
	}
	else if (auto const pPrefab = pEntity.As<Prefab_t>(); pPrefab != nullptr)
	{
		gpMetaGlobals->mres = MRES_SUPERCEDE;
		return pPrefab->ShouldCollide(pentOther);
	}

	return false;
}
