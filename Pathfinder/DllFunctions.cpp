#ifdef __INTELLISENSE__
#include <ranges>
#endif

#include <assert.h>

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
import LocalNav;		// CS Hostage
//import MonsterNav;		// HL1
import Nav;
//import Pathfinder;		// CZBOT


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


// Monitors

Task Task_ShowNavPath(std::ranges::input_range auto segments, Vector const vecSrc) noexcept
{
	static constexpr Vector VEC_OFS{ 0, 0, VEC_DUCK_HULL_MAX.z / 2.f };
	static constexpr std::array<std::string_view, NUM_TRAVERSE_TYPES+1> TRAV_MEANS =
	{
		"GO_NORTH",
		"GO_EAST",
		"GO_SOUTH",
		"GO_WEST",
		"GO_DIRECTLY",
		"GO_LADDER_UP",
		"GO_LADDER_DOWN",
		"GO_JUMP",
		"WHAT THE FUCK?"
	};

	g_engfuncs.pfnServerPrint(
		std::format("{} Segments in total.\n", segments.size()).c_str()
	);

	for (auto&& Seg : segments)
	{
		g_engfuncs.pfnServerPrint(
			std::format("    {}@ {: <6.1f}{: <6.1f}{: <6.1f}\n", TRAV_MEANS[Seg.how], Seg.pos.x, Seg.pos.y, Seg.pos.z).c_str()
		);
	}

	for (;;)
	{
		co_await TaskScheduler::NextFrame::Rank.back();;	// avoid inf loop.

		if (segments.size() >= 1 && segments.front().how <= NT_SIMPLE)
		{
			UTIL_DrawBeamPoints(
				vecSrc + VEC_OFS,
				segments.front().pos + VEC_OFS,
				5, 255, 255, 255
			);
		}

		for (auto&& [src, dest] : segments /*| std::views::take(15)*/ | std::views::adjacent<2>)
		{
			if (!src.ladder && (src.how == GO_LADDER_DOWN || src.how == GO_LADDER_UP))
				continue;

			// Connect regular path with ladders.
			switch (src.how)
			{
			case GO_LADDER_DOWN:
				UTIL_DrawBeamPoints(
					src.ladder->m_top,
					src.ladder->m_bottom,
					5, 128, 0, 0
				);
				UTIL_DrawBeamPoints(
					src.ladder->m_bottom,
					dest.pos + VEC_OFS,
					5, 255, 255, 255
				);
				break;

			case GO_LADDER_UP:
				UTIL_DrawBeamPoints(
					src.ladder->m_bottom,
					src.ladder->m_top,
					5, 0, 128, 0
				);
				UTIL_DrawBeamPoints(
					src.ladder->m_top,
					dest.pos + VEC_OFS,
					5, 255, 255, 255
				);
				break;

			case GO_JUMP:
				UTIL_DrawBeamPoints(
					src.pos + VEC_OFS,
					dest.pos + VEC_OFS,
					5, 0, 0, 128
				);
				break;

			default:
				UTIL_DrawBeamPoints(
					src.pos + VEC_OFS,
					dest.pos + VEC_OFS,
					5, 255, 255, 255
				);
				break;
			}

			co_await TaskScheduler::NextFrame::Rank.back();
		}
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

// DLL Export Functions

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
//	else if (szCmd == "pf_run") {}
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
//	else if (szCmd == "pf_ln") {}

	// Monster NAV
//	else if (szCmd == "pf_mn") {}

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

	// My creation
	else if (szCmd == "pf_nv")
	{
		TraceResult tr{};
		g_engfuncs.pfnTraceLine(
			pPlayer->pev->origin,
			pPlayer->pev->origin + Vector::Down() * 80.f,
			dont_ignore_glass | dont_ignore_monsters,
			pPlayer->edict(),
			&tr
		);

		static Navigator nav{ .m_pHost{pPlayer}, };

		if (!nav.Compute(tr.vecEndPos, vecTarget))
		{
			g_engfuncs.pfnServerPrint("No path found!\n");
			return MRES_SUPERCEDE;
		}
		else
			TaskScheduler::Enroll(Task_ShowNavPath(nav.ConcatLocalPath(tr.vecEndPos), tr.vecEndPos), (1ull << 0), true);


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
			AI->m_Scheduler.Enroll(AI->Task_Plot_WalkOnPath(vecTarget), TASK_PLOT_WALK_TO, true);

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
	else if (szCmd == "ai_qs")
	{
		if (AI)
			AI->m_Scheduler.Enroll(AI->Task_Kill(0.1f), TASK_REMOVE, true);

		auto const vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs;
		auto const vecEnd = vecSrc + pPlayer->pev->v_angle.Front() * 8192.0;

		TraceResult tr{};
		g_engfuncs.pfnTraceLine(vecSrc, vecEnd, dont_ignore_glass | dont_ignore_monsters, pPlayer->edict(), &tr);

		AI = Prefab_t::Create<CBaseAI>(tr.vecEndPos, Angles{});

		g_engfuncs.pfnTraceLine(
			pPlayer->pev->origin,
			pPlayer->pev->origin + Vector::Down() * 80.f,
			dont_ignore_glass | dont_ignore_monsters,
			pPlayer->edict(),
			&tr
		);

		vecTarget = tr.vecEndPos;
		AI->m_Scheduler.Enroll(AI->Task_Patrolling(vecTarget), TASK_PLOT_PATROL, true);

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
