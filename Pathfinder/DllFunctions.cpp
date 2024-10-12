import std;
import hlsdk;

import CBase;
import ConsoleVar;
import LocalNav;
import MonsterNav;
import Nav;
import Pathfinder;
import Plugin;
import Task;


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
	for (;;)
	{
		for (size_t i = 2; i < PF.m_pathLength; ++i)
		{
			auto& prev = PF.m_path[i - 1];
			auto& cur = PF.m_path[i];

			switch (cur.how)
			{
			case GO_LADDER_DOWN:
				UTIL_DrawBeamPoints(cur.ladder->m_top, cur.ladder->m_bottom, 5, 192, 0, 0);
				break;

			case GO_LADDER_UP:
				UTIL_DrawBeamPoints(cur.ladder->m_bottom, cur.ladder->m_top, 5, 0, 192, 0);
				break;

			case GO_JUMP:
				UTIL_DrawBeamPoints(prev.pos, cur.pos, 9, 0, 0, 192);
				break;

			default:
				UTIL_DrawBeamPoints(prev.pos, cur.pos, 9, 255, 255, 255);
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

	return MRES_IGNORED;
}

void fw_ServerDeactivate_Post() noexcept
{
	s_bShouldPrecache = true;
	TaskScheduler::Clear();
}
