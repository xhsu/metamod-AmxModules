import std;
import hlsdk;

import CBase;
import ConditionZero;
import Engine;
import GameRules;
import Message;
import PlayerItem;
import Prefab;
import Uranus;

import FileSystem;
import Hook;
import Plugin;
import WinAPI;


// Hook.cpp
extern void DeployInlineHooks() noexcept;
extern void RestoreInlineHooks() noexcept;


void fw_GameInit_Post() noexcept
{
	Engine::Init();	// Get engine build number
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
	RetrieveMessageHandles();
	RetrieveGameRules();
	RetrieveConditionZeroVar();
	// post
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
			std::destroy_at(pWeapon);
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

	return 0;
}
