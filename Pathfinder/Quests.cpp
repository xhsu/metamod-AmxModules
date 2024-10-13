/*
* Day-Night Survival Dev Team
* File Creation: 13 Oct 2024
*
* Programmer: Luna the Reborn
* Consultant: Crsky
*/

import std;
import hlsdk;

import CBase;
import Improvisational;
import Query;
import Task;


enum ETaskCheat : std::uint64_t
{
	TASK_PATH_DRAWING = (1ull << 0),
	TASK_PATH_COMPUTING = (1ull << 1),
	TASK_PATH_CHEAT_DISPATCH = (1ull << 2),
};

extern Task Task_ShowNavPath(std::span<PathSegment const> seg, Vector const vecSrc) noexcept;

static Task Task_Cheat_DeathMatch(CBasePlayer* pPlayer) noexcept
{
	CNavPath np{};
	std::vector<CBasePlayer*> enemies{};
	TraceResult tr{};

	for (;;)
	{
		co_await 0.9f;

		if (!pPlayer->IsAlive())
		{
			TaskScheduler::Delist(TASK_PATH_DRAWING);	// kill sub-routine
			co_return;
		}

		enemies =
			Query::all_living_players()
			| std::views::filter([&](CBasePlayer* e) noexcept { return pPlayer->m_iTeam != e->m_iTeam; })
			| std::ranges::to<std::vector>();

		std::ranges::sort(enemies, {}, [&](CBasePlayer* e) noexcept { return (e->pev->origin - pPlayer->pev->origin).LengthSquared(); });

		if (enemies.empty() || !enemies.front())
			continue;

		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + Vector{ 0, 0, -9999 };

		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, ignore_monsters | dont_ignore_glass, pPlayer->edict(), &tr);

		if (np.Compute(tr.vecEndPos, enemies.front()->Center(), HostagePathCost{}))
			TaskScheduler::Enroll(Task_ShowNavPath(np.Inspect(), tr.vecEndPos), TASK_PATH_DRAWING, true);
		else
			TaskScheduler::Delist(TASK_PATH_DRAWING);
	}
}

static Task Task_Cheat_DefuseBomb_CT(CBasePlayer* pPlayer) noexcept
{
	CNavPath np{};
	CBaseEntity* pTarget{};
	TraceResult tr{};

	for (;;)
	{
		co_await 0.9f;

		if (!pPlayer->IsAlive())
		{
			TaskScheduler::Delist(TASK_PATH_DRAWING);	// kill sub-routine
			co_return;
		}

		pTarget = nullptr;

		// Bomb planted?
		for (CBaseEntity* pEntity :
			Query::all_nonplayer_entities()
			| std::views::filter([](CBaseEntity* e) noexcept { return FClassnameIs(e->pev, "grenade"); })
			)
		{
			auto const gr = dynamic_cast<CGrenade*>(pEntity);

			if (gr->m_bIsC4)
			{
				pTarget = pEntity;
				break;
			}
		}

		// Who's holding it?
		if (!pTarget)
		{
			for (CBasePlayer* pOther : Query::all_living_players())
			{
				if (pOther->pev->weapons & (1 << WEAPON_C4))
				{
					pTarget = pOther;
					break;
				}
			}
		}

		// Is it on the ground?
		if (!pTarget)
		{
			for (CBaseEntity* pEntity :
				Query::all_nonplayer_entities()
				| std::views::filter([](CBaseEntity* e) noexcept { return FClassnameIs(e->pev, "weaponbox"); })
				)
			{
				auto const box = dynamic_cast<CWeaponBox*>(pEntity);

				if (box->m_rgpPlayerItems[5])
				{
					pTarget = pEntity;
					break;
				}
			}
		}

		if (!pTarget)
			continue;

		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + Vector{ 0, 0, -9999 };

		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, ignore_monsters | dont_ignore_glass, pPlayer->edict(), &tr);

		if (np.Compute(tr.vecEndPos, pTarget->Center(), HostagePathCost{}))
			TaskScheduler::Enroll(Task_ShowNavPath(np.Inspect(), tr.vecEndPos), TASK_PATH_DRAWING, true);
		else
			TaskScheduler::Delist(TASK_PATH_DRAWING);
	}
}

static Task Task_Cheat_Hostages_CT(CBasePlayer* pPlayer) noexcept
{
	CNavPath np{};
	std::vector<CBaseEntity*> enemies{};
	TraceResult tr{};

	for (;;)
	{
		co_await 0.9f;

		if (!pPlayer->IsAlive())
		{
			TaskScheduler::Delist(TASK_PATH_DRAWING);	// kill sub-routine
			co_return;
		}

		enemies =
			Query::all_nonplayer_entities()
			| std::views::filter([](CBaseEntity* e) noexcept { return FClassnameIs(e->pev, "hostage_entity"); })
			| std::views::filter([](CBaseEntity* e) noexcept { auto const hostage = dynamic_cast<CHostage*>(e); return !hostage->m_bTouched && hostage->IsValid(); })
			| std::ranges::to<std::vector>();

		if (enemies.empty())
		{
			enemies =
				Query::all_nonplayer_entities()
				| std::views::filter([](CBaseEntity* e) noexcept { return FClassnameIs(e->pev, "func_hostage_rescue") || FClassnameIs(e->pev, "info_hostage_rescue"); })
				| std::ranges::to<std::vector>();

			if (enemies.empty())
			{
				enemies =
					Query::all_nonplayer_entities()
					| std::views::filter([](CBaseEntity* e) noexcept { return FClassnameIs(e->pev, "info_player_start"); })
					| std::ranges::to<std::vector>();
			}
		}

		std::ranges::sort(enemies, {}, [&](CBaseEntity* e) noexcept { return (e->pev->origin - pPlayer->pev->origin).LengthSquared(); });

		if (enemies.empty())
			continue;

		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + Vector{ 0, 0, -9999 };

		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, ignore_monsters | dont_ignore_glass, pPlayer->edict(), &tr);

		if (np.Compute(tr.vecEndPos, enemies.front()->Center(), HostagePathCost{}))
			TaskScheduler::Enroll(Task_ShowNavPath(np.Inspect(), tr.vecEndPos), TASK_PATH_DRAWING, true);
		else
			TaskScheduler::Delist(TASK_PATH_DRAWING);
	}
}

enum GameScenarioType
{
	SCENARIO_DEATHMATCH,
	SCENARIO_DEFUSE_BOMB,
	SCENARIO_RESCUE_HOSTAGES,
	SCENARIO_ESCORT_VIP,
	SCENARIO_ESCAPE
};

Task Task_Cheat_Dispatch(CBasePlayer* pPlayer) noexcept
{
	TaskScheduler::Delist(TASK_PATH_DRAWING | TASK_PATH_COMPUTING);

	if (LoadNavigationMap() != NAV_OK)
		g_engfuncs.pfnServerPrint("[PF] NAV loading error.\n");

	co_await 0.1f;

	GameScenarioType iGameScenario{ SCENARIO_DEATHMATCH };

	for (CBaseEntity* pEntity : Query::all_nonplayer_entities())
	{
		if (FClassnameIs(pEntity->pev, "func_bomb_target")
			|| FClassnameIs(pEntity->pev, "info_bomb_target"))
		{
			iGameScenario = SCENARIO_DEFUSE_BOMB;
			break;
		}
		else if (FClassnameIs(pEntity->pev, "func_hostage_rescue")
			|| FClassnameIs(pEntity->pev, "info_hostage_rescue")
			|| FClassnameIs(pEntity->pev, "hostage_entity"))
		{
			iGameScenario = SCENARIO_RESCUE_HOSTAGES;
			break;
		}
		else if (FClassnameIs(pEntity->pev, "func_vip_safetyzone"))
		{
			iGameScenario = SCENARIO_ESCORT_VIP;
			break;
		}
		else if (FClassnameIs(pEntity->pev, "func_escapezone"))
		{
			iGameScenario = SCENARIO_ESCAPE;
			break;
		}
	}

	co_await 0.1f;

	switch (iGameScenario)
	{
	default:
	case SCENARIO_DEATHMATCH:
		TaskScheduler::Enroll(Task_Cheat_DeathMatch(pPlayer), TASK_PATH_COMPUTING, true);
		break;

	case SCENARIO_DEFUSE_BOMB:
		if (pPlayer->m_iTeam == TEAM_CT)
			TaskScheduler::Enroll(Task_Cheat_DefuseBomb_CT(pPlayer), TASK_PATH_COMPUTING, true);
		break;

	case SCENARIO_RESCUE_HOSTAGES:
		if (pPlayer->m_iTeam == TEAM_CT)
			TaskScheduler::Enroll(Task_Cheat_Hostages_CT(pPlayer), TASK_PATH_COMPUTING, true);
		break;

	case SCENARIO_ESCAPE:
	case SCENARIO_ESCORT_VIP:
		break;
	}

	co_return;
}
