#ifdef __INTELLISENSE__
#include <ranges>
#endif

import std;
import hlsdk;

import BaseMonster;
import CBase;
import ConsoleVar;
import FileSystem;
import Models;
import Plugin;
import Query;
import Task;
import Prefab;
import VTFH;

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
			UTIL_DrawBeamPoints(ladder.m_bottom, ladder.m_top, 9, 192, 0, 0);
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

Task Task_ShowArea(CBasePlayer* pPlayer) noexcept
{
	for (;;)
	{
		co_await 0.1f;

		auto area = TheNavAreaGrid.GetNavArea(pPlayer->pev->origin);

		if (area)
		{
			area->Draw(255, 255, 255);
			area->DrawHidingSpots();
		}
	}
}

extern Task Task_Cheat_Dispatch(CBasePlayer* pPlayer) noexcept;

void fw_GameInit_Post() noexcept
{
	CVarManager::Init();
	FileSystem::Init();

	TaskScheduler::Enroll(CLocalNav::Task_LocalNav());
}

auto fw_Spawn_Post(edict_t* pEdict) noexcept -> qboolean
{
	[[likely]]
	if (!s_bShouldPrecache)
		return false;

	s_iBeamSprite = g_engfuncs.pfnPrecacheModel("sprites/smoke.spr");
	g_engfuncs.pfnPrecacheModel("models/w_galil.mdl");	// random bugfix
	g_engfuncs.pfnPrecacheModel("models/hgrunt.mdl");	// AI test

	s_bShouldPrecache = false;
	return false;
}

META_RES OnClientCommand(CBasePlayer* pPlayer, std::string_view szCmd) noexcept
{
	static Vector vecTarget{};
	static EHANDLE<CBaseAI> AI{};

	// CZ BOT NAV
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
		PF.m_lastKnownArea = TheNavAreaGrid.GetNavArea(pPlayer->pev->origin);
		PF.m_areaEnteredTimestamp = gpGlobals->time;

		if (PF.ComputePath(TheNavAreaGrid.GetNavArea(vecTarget), &vecTarget, FASTEST_ROUTE))
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

	else if (szCmd == "pf_area")
	{
		TaskScheduler::Enroll(Task_ShowArea(pPlayer), 1ull << 0, true);
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
		TaskScheduler::Enroll(Task_Cheat_Dispatch(pPlayer), (1ull << 2), true);
		return MRES_SUPERCEDE;
	}
	else if (szCmd == "pf_stopch")
	{
		TaskScheduler::Delist((1ull << 0) | (1ull << 1) | (1ull << 2));
		return MRES_SUPERCEDE;
	}

	else if (szCmd == "mdl_read")
	{
		if (GoldSrc::CacheStudioModelInfo("models/hgrunt.mdl"))
		{
			for (auto&& [szName, info] : GoldSrc::m_StudioInfo.at("models/hgrunt.mdl"))
			{
				g_engfuncs.pfnServerPrint(
					std::format("[{}]: {} - {:.2f}\n", info.m_iSeqIdx, szName, info.m_total_length).c_str()
				);
			}
		}

		return MRES_SUPERCEDE;
	}

	// AI
	else if (szCmd == "ai_hg")
	{
		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + pPlayer->pev->v_angle.Front() * 8192.0;

		TraceResult tr{};
		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, dont_ignore_glass | dont_ignore_monsters, pPlayer->edict(), &tr);

		AI = Prefab_t::Create<CBaseAI>(tr.vecEndPos, Angles{});

		//auto e = g_engfuncs.pfnCreateNamedEntity(MAKE_STRING("info_target"));

		//e->v.solid = SOLID_SLIDEBOX;
		//e->v.movetype = MOVETYPE_STEP;

		//g_engfuncs.pfnSetSize(e, VEC_HUMAN_HULL_MIN, VEC_HUMAN_HULL_MAX);
		//g_engfuncs.pfnSetModel(e, "models/hgrunt.mdl");
		//g_engfuncs.pfnSetOrigin(e, tr.vecEndPos);

		return MRES_SUPERCEDE;
	}
	else if (szCmd == "ai_move")
	{
		if (AI)
			AI->Plot_PathToLocation(vecTarget);

		return MRES_SUPERCEDE;
	}
	else if (szCmd == "ai_anim")
	{
		if (g_engfuncs.pfnCmd_Argc() != 2)
			return MRES_SUPERCEDE;

		if (AI)
			AI->PlayAnim(g_engfuncs.pfnCmd_Argv(1));

		return MRES_SUPERCEDE;
	}
	else if (szCmd == "ai_loop")
	{
		if (AI)
			AI->m_Scheduler.Enroll(AI->Task_Patrolling(vecTarget), TASK_PLOT_PATROL, true);

		return MRES_SUPERCEDE;
	}
	else if (szCmd == "ai_kill")
	{
		if (AI)
			AI->m_Scheduler.Enroll(AI->Task_Kill(0.1f), TASK_REMOVE, true);

		return MRES_SUPERCEDE;
	}

	return MRES_IGNORED;
}

void fw_ServerActivate_Post(edict_t* pEdictList, int edictCount, int clientMax) noexcept
{
	RetrieveCBaseVirtualFn();	// for Prefab
}

void fw_ServerDeactivate_Post() noexcept
{
	s_bShouldPrecache = true;
	TaskScheduler::Clear();
	DestroyNavigationMap();
}
