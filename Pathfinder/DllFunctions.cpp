import std;
import hlsdk;

import CBase;
import ConsoleVar;
import Plugin;
import Query;
import Task;

import Improvisational;	// CZ Hostage
import LocalNav;	// CS Hostage
import MonsterNav;	// HL1
import Nav;			// CZBOT
import Pathfinder;	// Testing module


static short s_iBeamSprite = 0;
static bool s_bShouldPrecache = true;

inline void UTIL_DrawBeamPoints(Vector const& vecStart, Vector const& vecEnd,
	int iLifetime, std::uint8_t bRed, std::uint8_t bGreen, std::uint8_t bBlue) noexcept
{
	g_engfuncs.pfnMessageBegin(MSG_PVS, SVC_TEMPENTITY, vecStart, nullptr);
	g_engfuncs.pfnWriteByte(TE_BEAMPOINTS);
	g_engfuncs.pfnWriteCoord(vecStart.x);
	g_engfuncs.pfnWriteCoord(vecStart.y);
	g_engfuncs.pfnWriteCoord(vecStart.z);
	g_engfuncs.pfnWriteCoord(vecEnd.x);
	g_engfuncs.pfnWriteCoord(vecEnd.y);
	g_engfuncs.pfnWriteCoord(vecEnd.z);
	g_engfuncs.pfnWriteShort(s_iBeamSprite);
	g_engfuncs.pfnWriteByte(0);	// starting frame
	g_engfuncs.pfnWriteByte(0);	// frame rate
	g_engfuncs.pfnWriteByte(iLifetime);
	g_engfuncs.pfnWriteByte(10);	// width
	g_engfuncs.pfnWriteByte(0);	// noise
	g_engfuncs.pfnWriteByte(bRed);
	g_engfuncs.pfnWriteByte(bGreen);
	g_engfuncs.pfnWriteByte(bBlue);
	g_engfuncs.pfnWriteByte(255);	// brightness
	g_engfuncs.pfnWriteByte(0);	// scroll speed
	g_engfuncs.pfnMessageEnd();
}

Task Task_ShowPathfinder(Pathfinder const& PF) noexcept
{
	static constexpr Vector VEC_OFS{ 0, 0, VEC_DUCK_HULL_MAX.z / 2.f };

	for (;;)
	{
		for (size_t i = 2; i < PF.m_pathLength; ++i)
		{
			auto& prev = PF.m_path[i - 1];
			auto& cur = PF.m_path[i];

			switch (cur.how)
			{
			case GO_LADDER_DOWN:
				UTIL_DrawBeamPoints(
					cur.ladder->m_top,
					cur.ladder->m_bottom,
					5, 192, 0, 0
				);
				break;

			case GO_LADDER_UP:
				UTIL_DrawBeamPoints(
					cur.ladder->m_bottom,
					cur.ladder->m_top,
					5, 0, 192, 0
				);
				break;

			case GO_JUMP:
				UTIL_DrawBeamPoints(
					prev.pos + VEC_OFS,
					cur.pos + VEC_OFS,
					9, 0, 0, 192
				);
				break;

			default:
				UTIL_DrawBeamPoints(
					prev.pos + VEC_OFS,
					cur.pos + VEC_OFS,
					9, 255, 255, 255
				);
				break;
			}

			co_await 0.01f;
		}

		co_await 0.01f;	// avoid inf loop.
	}

	co_return;
}

Task Task_ShowNavPath(std::span<PathSegment const> seg, Vector const vecSrc) noexcept
{
	static constexpr Vector VEC_OFS{ 0, 0, VEC_DUCK_HULL_MAX.z / 2.f };

	for (;;)
	{
		if (seg.size() >= 1)
		{
			UTIL_DrawBeamPoints(
				vecSrc + VEC_OFS,
				seg.front().pos + VEC_OFS,
				5, 255, 255, 255
			);
		}

		for (size_t i = 1; i < seg.size(); ++i)
		{
			auto& prev = seg[i - 1];
			auto& cur = seg[i];

/*
			if (prev.how != GO_LADDER_DOWN && prev.how != GO_LADDER_UP
				&& cur.how != GO_LADDER_DOWN && cur.how != GO_LADDER_UP)
			{
				continue;
			}
*/

			switch (cur.how)
			{
			case GO_LADDER_DOWN:
				UTIL_DrawBeamPoints(
					cur.ladder->m_top,
					cur.ladder->m_bottom,
					5, 192, 0, 0
				);
				break;

			case GO_LADDER_UP:
				UTIL_DrawBeamPoints(
					cur.ladder->m_bottom,
					cur.ladder->m_top,
					5, 0, 192, 0
				);
				break;

			case GO_JUMP:
				UTIL_DrawBeamPoints(
					prev.pos + VEC_OFS,
					cur.pos + VEC_OFS,
					5, 0, 0, 192
				);
				break;

				// Connect regular path with ladders.
			default:
				if (prev.how == GO_LADDER_UP)
				{
					UTIL_DrawBeamPoints(
						prev.ladder->m_top,
						cur.pos + VEC_OFS,
						5, 255, 255, 255
					);
				}
				else if (cur.how == GO_LADDER_UP)
				{
					UTIL_DrawBeamPoints(
						prev.pos + VEC_OFS,
						cur.ladder->m_bottom,
						5, 255, 255, 255
					);
				}
				else if (prev.how == GO_LADDER_DOWN)
				{
					UTIL_DrawBeamPoints(
						prev.ladder->m_bottom,
						cur.pos + VEC_OFS,
						5, 255, 255, 255
					);
				}
				else if (cur.how == GO_LADDER_DOWN)
				{
					UTIL_DrawBeamPoints(
						prev.pos + VEC_OFS,
						cur.ladder->m_top,
						5, 255, 255, 255
					);
				}
				else
				{
					UTIL_DrawBeamPoints(
						prev.pos + VEC_OFS,
						cur.pos + VEC_OFS,
						5, 255, 255, 255
					);
				}
				break;
			}

			co_await 0.01f;
		}

		co_await 0.01f;	// avoid inf loop.
	}

	co_return;
}

Task Task_ShowLadders() noexcept
{
	for (; !TheNavLadderList.empty();)
	{
		for (auto&& ladder : TheNavLadderList)
		{
			UTIL_DrawBeamPoints(ladder->m_bottom, ladder->m_top, 9, 192, 0, 0);
			co_await 0.1f;
		}

		co_await 0.01f;	// avoid inf loop.
	}

	g_engfuncs.pfnServerPrint("[PF] No ladders in NAV.\n");
	co_return;
}

Task Task_ShowLN(std::span<Vector> rgvec, Vector const vecSrc) noexcept
{
	for (;;)
	{
		UTIL_DrawBeamPoints(vecSrc, rgvec.back(), 9, 255, 255, 255);

		co_await 0.01f;

		for (auto i = 1u; i < rgvec.size(); ++i)
		{
			UTIL_DrawBeamPoints(rgvec[i - 1], rgvec[i], 9, 255, 255, 255);
			co_await 0.01f;
		}

		co_await 0.1f;
	}

	co_return;
}

Task Task_ShowMN(MonsterNav const& MN, Vector const vecSrc) noexcept
{
	for (;;)
	{
		UTIL_DrawBeamPoints(vecSrc, MN.m_Route[MN.m_iRouteIndex].vecLocation, 9, 255, 255, 255);

		co_await 0.01f;

		for (auto i = MN.m_iRouteIndex + 1; i < std::ssize(MN.m_Route); ++i)
		{
			if (MN.m_Route[i].iType == 0)
				break;

			UTIL_DrawBeamPoints(MN.m_Route[i - 1].vecLocation, MN.m_Route[i - 1].vecLocation, 9, 255, 255, 255);
			co_await 0.01f;
		}

		co_await 0.1f;
	}

	co_return;
}

enum GameScenarioType
{
	SCENARIO_DEATHMATCH,
	SCENARIO_DEFUSE_BOMB,
	SCENARIO_RESCUE_HOSTAGES,
	SCENARIO_ESCORT_VIP,
	SCENARIO_ESCAPE
};

Task Task_Cheat_FindGameTarget(CBasePlayer* pPlayer) noexcept
{
	CNavPath np{};
	TraceResult tr{};

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

	for (;;)
	{
		co_await 0.9f;

		if (!pPlayer->IsAlive())
			co_return;

		CBaseEntity* pTarget{};

		switch (iGameScenario)
		{
		default:
		case SCENARIO_DEATHMATCH:
			break;	// find last player?

		case SCENARIO_DEFUSE_BOMB:
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

			if (!pTarget)
			{
				for (CBaseEntity* pEntity :
					Query::all_nonplayer_entities()
					| std::views::filter([](CBaseEntity* e) noexcept { return FClassnameIs(e->pev, "weapon_c4"); })
					)
				{
					pTarget = pEntity;
					break;
				}
			}

			break;

		case SCENARIO_RESCUE_HOSTAGES:
			for (CBaseEntity* pEntity :
				Query::all_nonplayer_entities()
				| std::views::filter([](CBaseEntity* e) noexcept { return FClassnameIs(e->pev, "hostage_entity"); })
				)
			{
				auto const hostage = dynamic_cast<CHostage*>(pEntity);

				if (!hostage->m_bTouched && hostage->IsValid())
				{
					pTarget = pEntity;
					break;
				}
			}

			if (!pTarget)
			{
				std::vector<CBaseEntity*> zones =
					Query::all_nonplayer_entities()
					| std::views::filter([](CBaseEntity* e) noexcept { return FClassnameIs(e->pev, "func_hostage_rescue") || FClassnameIs(e->pev, "info_hostage_rescue"); })
					| std::ranges::to<std::vector>();

				if (zones.empty())
				{
					zones =
						Query::all_nonplayer_entities()
						| std::views::filter([](CBaseEntity* e) noexcept { return FClassnameIs(e->pev, "info_player_start"); })
						| std::ranges::to<std::vector>();
				}

				std::ranges::sort(zones, {}, [&](CBaseEntity* e) noexcept { return (e->pev->origin - pPlayer->pev->origin).LengthSquared(); });
				pTarget = zones.front();	// sorted by std::less<>
			}

			break;
		case SCENARIO_ESCORT_VIP:
		case SCENARIO_ESCAPE:
			break;
		}

		if (!pTarget)
			continue;

		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + Vector{ 0, 0, -9999 };

		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, ignore_monsters | dont_ignore_glass, pPlayer->edict(), &tr);

		if (np.Compute(tr.vecEndPos, pTarget->Center(), HostagePathCost{}))
			TaskScheduler::Enroll(Task_ShowNavPath(np.Inspect(), tr.vecEndPos), (1ull << 0), true);
		else
			TaskScheduler::Delist((1ull << 0));
	}

	co_return;
}

void fw_GameInit_Post() noexcept
{
	CVarManager::Init();

	TaskScheduler::Enroll(CLocalNav::Task_LocalNav());
}

auto fw_Spawn_Post(edict_t* pEdict) noexcept -> qboolean
{
	[[likely]]
	if (!s_bShouldPrecache)
		return false;

	s_iBeamSprite = g_engfuncs.pfnPrecacheModel("sprites/smoke.spr");
	g_engfuncs.pfnPrecacheModel("models/w_galil.mdl");

	s_bShouldPrecache = false;
	return false;
}

META_RES OnClientCommand(CBasePlayer* pPlayer, std::string_view szCmd) noexcept
{
	static Vector vecTarget{};

	if (szCmd == "pf_load")
	{
		auto const ret = LoadNavigationMap();
		if (ret != NAV_OK)
			g_engfuncs.pfnServerPrint("[PF] NAV loading error.\n");

		return MRES_SUPERCEDE;
	}
	else if (szCmd == "pf_run")
	{
		static Pathfinder PF{};
		PF.pev = pPlayer->pev;
		PF.m_lastKnownArea = TheNavAreaGrid.GetNavArea(&pPlayer->pev->origin);
		PF.m_areaEnteredTimestamp = gpGlobals->time;

		if (PF.ComputePath(TheNavAreaGrid.GetNavArea(&vecTarget), &vecTarget, FASTEST_ROUTE))
		{
			for (size_t i = 1; i < PF.m_pathLength; ++i)
			{
				auto const text = std::format(
					"[{:0>3}] {} {} {}\n",
					i,
					PF.m_path[i].pos.x,
					PF.m_path[i].pos.y,
					PF.m_path[i].pos.z
				);

				g_engfuncs.pfnServerPrint(text.c_str());
			}

			TaskScheduler::Enroll(Task_ShowPathfinder(PF), (1 << 0), true);
		}
		else
			g_engfuncs.pfnServerPrint("No path found!\n");

		return MRES_SUPERCEDE;
	}
	else if (szCmd == "pf_ladders")
	{
		TaskScheduler::Enroll(Task_ShowLadders(), (1 << 0), true);
		return MRES_SUPERCEDE;
	}

	else if (szCmd == "pf_set")
	{
		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + pPlayer->pev->v_angle.Front() * 8192.0;

		TraceResult tr{};
		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, dont_ignore_glass | dont_ignore_monsters, pPlayer->edict(), &tr);

		vecTarget = tr.vecEndPos;

		return MRES_SUPERCEDE;
	}

	// Local NAV
	else if (szCmd == "pf_ln")
	{
		static CLocalNav LocalNav{ pPlayer };
		static std::vector<Vector> Nodes{};

		if (auto const fl = (pPlayer->pev->origin - vecTarget).Length(); fl > 1000)
		{
			g_engfuncs.pfnServerPrint(std::format("Too far! ({:.1f})\n", fl).c_str());
			return MRES_SUPERCEDE;
		}

		node_index_t nindexPath = LocalNav.FindPath(pPlayer->pev->origin, vecTarget, 40, ignore_monsters | dont_ignore_glass);
		if (nindexPath == NODE_INVALID_EMPTY)
		{
			g_engfuncs.pfnServerPrint(std::format("Path no found!\n").c_str());
		}
		else
		{
			LocalNav.SetupPathNodes(nindexPath, &Nodes);
			auto const m_nTargetNode = LocalNav.GetFurthestTraversableNode(pPlayer->pev->origin, &Nodes, ignore_monsters | dont_ignore_glass);
			g_engfuncs.pfnServerPrint(std::format("m_nTargetNode == {}\n", m_nTargetNode).c_str());
			TaskScheduler::Enroll(Task_ShowLN({ Nodes.begin(), Nodes.begin() + m_nTargetNode + 1 }, pPlayer->pev->origin), (1 << 0), true);
			//TaskScheduler::Enroll(Task_ShowLN(Nodes, pPlayer->pev->origin), (1 << 0), true);
		}

		return MRES_SUPERCEDE;
	}

	// Monster NAV
	else if (szCmd == "pf_mn")
	{
		static MonsterNav MN{
			.pev = pPlayer->pev,
		};

		if (MN.BuildRoute(vecTarget, bits_MF_TO_LOCATION, nullptr))
		{
			TaskScheduler::Enroll(Task_ShowMN(MN, pPlayer->pev->origin), (1 << 0), true);
		}
		else
			g_engfuncs.pfnServerPrint("No path found!\n");

		return MRES_SUPERCEDE;
	}

	// Improv NAV
	else if (szCmd == "pf_im")
	{
		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + Vector{ 0, 0, -9999 };

		TraceResult tr{};
		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, ignore_monsters | dont_ignore_glass, pPlayer->edict(), &tr);

		static CNavPath np{};
		if (np.Compute(tr.vecEndPos, vecTarget, HostagePathCost{}))
			TaskScheduler::Enroll(Task_ShowNavPath(np.Inspect(), tr.vecEndPos), (1ull << 0), true);
		else
			g_engfuncs.pfnServerPrint("No path found!\n");

		return MRES_SUPERCEDE;
	}

	// Testing & cheating
	else if (szCmd == "pf_cheat")
	{
		TaskScheduler::Enroll(Task_Cheat_FindGameTarget(pPlayer), (1ull << 1), true);
		return MRES_SUPERCEDE;
	}
	else if (szCmd == "pf_stopch")
	{
		TaskScheduler::Delist((1ull << 0) | (1ull << 1));
		return MRES_SUPERCEDE;
	}

	return MRES_IGNORED;
}

void fw_ServerDeactivate_Post() noexcept
{
	s_bShouldPrecache = true;
	TaskScheduler::Clear();
	DestroyNavigationMap();
}
