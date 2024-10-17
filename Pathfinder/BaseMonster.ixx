module;

#ifdef __INTELLISENSE__
#include <algorithm>
#include <ranges>
#endif

#include <assert.h>

export module BaseMonster;

import std;
import hlsdk;

import Prefab;
import Task;
import Models;

import Improvisational;
import LocalNav;

import UtlRandom;
import UtlString;



extern "C++" inline void UTIL_DrawBeamPoints(Vector const& vecStart, Vector const& vecEnd,
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
	g_engfuncs.pfnWriteShort(g_engfuncs.pfnModelIndex("sprites/smoke.spr"));
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

export struct Navigator
{
	EHANDLE<CBaseEntity> m_pHost{};
	EHANDLE<CBaseEntity> m_pTargetEnt{};
	std::vector<localnode_t> m_nodeArr{ (size_t)0x80, localnode_t{} };
	node_index_t m_nindexAvailableNode{};
	Vector m_vecStartingLoc{};
	std::deque<PathSegment> m_Segments{};
	std::move_only_function<float(CNavArea*, CNavArea*, const CNavLadder*) noexcept> m_CostFunc{ HostagePathCost{} };
	mutable bool m_fTargetEntHit{ false };

	static inline int& m_NodeValue{ CLocalNav::m_NodeValue };
	static inline float& m_flLastThinkTime{ CLocalNav::m_flLastThinkTime };

	inline bool IsValid() const noexcept { return !m_Segments.empty(); }
	inline void Invalidate() noexcept { m_Segments.clear(); }

#pragma region Path Building

	// Build an area-to-area framework of our path.
	bool Compute(Vector const& vecSrc, Vector const& vecGoal) noexcept
	{
		Invalidate();

		auto const pStartArea = TheNavAreaGrid.GetNearestNavArea(vecSrc);
		auto const pGoalArea = TheNavAreaGrid.GetNearestNavArea(vecGoal);

		if (!pStartArea || !pGoalArea)
			return false;

		// if we are already in the goal area, build trivial path
		if (pStartArea == pGoalArea)
			return BuildTrivialPath(vecSrc, vecGoal);

		// Compute shortest path to goal
		CNavArea* pClosestArea{};
		auto const bPathFound =
			NavAreaBuildPath(pStartArea, pGoalArea, vecGoal, m_CostFunc, &pClosestArea);

		auto const effectiveGoalArea = bPathFound ? pGoalArea : pClosestArea;

		// Build path by following parent links
		for (auto pArea = effectiveGoalArea; pArea; pArea = pArea->GetParent())
		{
			// This linked list is tracking our path backwards.
			// Hence emplace_front.
			m_Segments.emplace_front(
				pArea,
				pArea->GetParentHow(),
				pArea->GetCenter(),
				nullptr
			);
		}

		if (m_Segments.size() == 1)
			return BuildTrivialPath(vecSrc, vecGoal);

		// LUNA: this is a cropped version of ComputePathPositions()
		// Only handles ladders.
		for (auto&& [from, to] : m_Segments | std::views::adjacent<2>)
		{
			// to get to next area, must go up a ladder
			if (to.how == GO_LADDER_UP)
			{
				// find our ladder
				for (auto&& ladder : *from.area->GetLadderList(LADDER_UP))
				{
					// can't use "behind" area when ascending...
					if (ladder->m_topForwardArea == to.area
						|| ladder->m_topLeftArea == to.area
						|| ladder->m_topRightArea == to.area)
					{
						to.ladder = ladder;
						to.pos = ladder->m_bottom;

						// Ensure this point was not stuck in the wall or ladder entity.
						AddDirectionVector(&to.pos, ladder->m_dir, 2.0f * HalfHumanWidth);
						break;
					}
				}

				[[unlikely]]
				if (!to.ladder)
				{
					g_engfuncs.pfnServerPrint(
						std::format("Up ladder no found at area: {:.1f} {:.1f} {:.1f}\n", to.pos.x, to.pos.y, to.pos.z).c_str()
					);

					return false;
				}
			}

			// to get to next area, must go down a ladder
			else if (to.how == GO_LADDER_DOWN)
			{
				// find our ladder
				for (auto&& ladder : *from.area->GetLadderList(LADDER_DOWN))
				{
					if (ladder->m_bottomArea == to.area)
					{
						to.ladder = ladder;
						to.pos = ladder->m_top;

						// LUNA: originally it's Opposite[ladder->m_dir] here.
						// I don't get it, this calc will cause the pos ends up in the ladder or wall.
						AddDirectionVector(&to.pos, ladder->m_dir, 2.0f * HalfHumanWidth);
						break;
					}
				}

				[[unlikely]]
				if (!to.ladder)
				{
					g_engfuncs.pfnServerPrint(
						std::format("Down ladder no found at area: {:.1f} {:.1f} {:.1f}\n", to.pos.x, to.pos.y, to.pos.z).c_str()
					);

					return false;
				}
			}
		}

		// append path end position
		m_Segments.emplace_back(
			effectiveGoalArea,
			GO_DIRECTLY,
			Vector{ vecGoal.x, vecGoal.y, pGoalArea->GetZ(vecGoal) },
			nullptr
		);

		m_Segments.front().how = GO_DIRECTLY;
		return true;
	}

	// Build trivial path when start and goal are in the same nav area
	bool BuildTrivialPath(Vector const& vecSrc, Vector const& vecGoal) noexcept
	{
		Invalidate();

		auto const startArea = TheNavAreaGrid.GetNearestNavArea(vecSrc);
		auto const goalArea = TheNavAreaGrid.GetNearestNavArea(vecGoal);

		if (!startArea || !goalArea)
			return false;

		// There are only two points, start and end.

		m_Segments.emplace_front(
			startArea,
			GO_DIRECTLY,
			Vector{ vecSrc.x, vecSrc.y, startArea->GetZ(vecSrc) },
			nullptr
		);

		m_Segments.emplace_back(
			goalArea,
			GO_DIRECTLY,
			Vector{ vecGoal.x, vecGoal.y, goalArea->GetZ(vecGoal) },
			nullptr
		);

		return true;
	}

	// LUNA: Calls FindLocalPath() and append the result to m_Segments
	auto ConcatLocalPath(Vector const& vecStart, float flTargetRadius = 80) noexcept -> decltype(std::ranges::subrange(m_Segments.begin(), m_Segments.end()))
	{
		auto const&& [nCount, Path] = SimplifiedPath(vecStart);

		// Delete the first N accessable nodes.
		if (nCount > 0)
			m_Segments.erase(m_Segments.begin(), m_Segments.begin() + nCount);
		// Remember now that the view-Range Path is invalidate!

		assert(!m_Segments.empty());
		auto const nindexPath =
			FindLocalPath(vecStart, m_Segments.front().pos, flTargetRadius);

		if (nindexPath != NODE_INVALID_EMPTY)
		{
			// Append the local nav result onto m_Segments.
			for (auto nCurrentIndex = nindexPath; nCurrentIndex != NODE_INVALID_EMPTY; /* Does nothing here. */)
			{
				auto const& CurNode = m_nodeArr[nCurrentIndex];
				auto const pArea = TheNavAreaGrid.GetNearestNavArea(CurNode.vecLoc);

				// Same as CZBOT nav, it's tracking from end to start.
				m_Segments.emplace_front(
					pArea,
					GO_DIRECTLY,
					Vector{ CurNode.vecLoc.x, CurNode.vecLoc.y, pArea->GetZ(CurNode.vecLoc) },
					nullptr
				);

				nCurrentIndex = CurNode.nindexParent;	// fuck the C++ scope.
			}
		}

		return SimplifiedPath(vecStart).second;
	}

	node_index_t FindLocalPath(Vector const& vecStart, Vector const& vecDest, float flTargetRadius = 80, TRACE_FL fNoMonsters = dont_ignore_glass | dont_ignore_monsters) noexcept
	{
		node_index_t nIndexBest = FindDirectPath(vecStart, vecDest, flTargetRadius, fNoMonsters);

		if (nIndexBest != NODE_INVALID_EMPTY)
		{
			return nIndexBest;
		}

		m_vecStartingLoc = vecStart;
		m_nindexAvailableNode = 0;

		AddPathNodes(NODE_INVALID_EMPTY, fNoMonsters);
		nIndexBest = GetBestNode(vecStart, vecDest);

		while (nIndexBest != NODE_INVALID_EMPTY)
		{
			auto& node = m_nodeArr[nIndexBest];
			node.fSearched = true;

			auto const& vecNodeLoc = node.vecLoc;
			auto const flDistToDest = (vecDest - node.vecLoc).Length2D();

			if (flDistToDest <= flTargetRadius)
				break;

			if (flDistToDest <= HOSTAGE_STEPSIZE)
				break;

			if (((flDistToDest - flTargetRadius) > ((m_nodeArr.size() - m_nindexAvailableNode) * HOSTAGE_STEPSIZE))
				|| m_nindexAvailableNode == m_nodeArr.size())
			{
				nIndexBest = NODE_INVALID_EMPTY;
				break;
			}

			AddPathNodes(nIndexBest, fNoMonsters);
			nIndexBest = GetBestNode(vecNodeLoc, vecDest);
		}

		if (m_nindexAvailableNode <= 10)
			m_NodeValue += 2;

		else if (m_nindexAvailableNode <= 20)
			m_NodeValue += 4;

		else if (m_nindexAvailableNode <= 30)
			m_NodeValue += 8;

		else if (m_nindexAvailableNode <= 40)
			m_NodeValue += 13;

		else if (m_nindexAvailableNode <= 50)
			m_NodeValue += 19;

		else if (m_nindexAvailableNode <= 60)
			m_NodeValue += 26;

		else if (m_nindexAvailableNode <= 70)
			m_NodeValue += 34;

		else if (m_nindexAvailableNode <= 80)
			m_NodeValue += 43;

		else if (m_nindexAvailableNode <= 90)
			m_NodeValue += 53;

		else if (m_nindexAvailableNode <= 100)
			m_NodeValue += 64;

		else if (m_nindexAvailableNode <= 110)
			m_NodeValue += 76;

		else if (m_nindexAvailableNode <= 120)
			m_NodeValue += 89;

		else if (m_nindexAvailableNode <= 130)
			m_NodeValue += 103;

		else if (m_nindexAvailableNode <= 140)
			m_NodeValue += 118;

		else if (m_nindexAvailableNode <= 150)
			m_NodeValue += 134;

		else if (m_nindexAvailableNode <= 160)
			m_NodeValue += 151;
		else
			m_NodeValue += 169;

		return nIndexBest;
	}

	node_index_t FindDirectPath(Vector const& vecStart, Vector const& vecDest, float flTargetRadius, TRACE_FL fNoMonsters) noexcept
	{
		auto const vecPathDir = (vecStart - vecDest).Normalize();
		auto vecActualDest = vecDest - (vecPathDir * flTargetRadius);

		if (PathTraversable(vecStart, &vecActualDest, fNoMonsters) == PTRAVELS_NO)
		{
			return NODE_INVALID_EMPTY;
		}

		auto nIndexLast = NODE_INVALID_EMPTY;
		auto vecNodeLoc = vecStart;
		m_nindexAvailableNode = 0;

		while ((vecNodeLoc - vecActualDest).Length2D() >= HOSTAGE_STEPSIZE)
		{
			node_index_t nindexCurrent = nIndexLast;

			vecNodeLoc = vecNodeLoc + (vecPathDir * HOSTAGE_STEPSIZE);
			nIndexLast = AddNode(nindexCurrent, vecNodeLoc);

			if (nIndexLast == NODE_INVALID_EMPTY)
				break;
		}

		return nIndexLast;
	}

	node_index_t AddNode(node_index_t nindexParent, Vector const& vecLoc, int offsetX = 0, int offsetY = 0, uint8_t bDepth = 0) noexcept
	{
		if (m_nindexAvailableNode == std::ssize(m_nodeArr))
			return NODE_INVALID_EMPTY;

		auto& nodeNew = m_nodeArr[m_nindexAvailableNode];

		nodeNew.vecLoc = vecLoc;
		nodeNew.offsetX = offsetX;
		nodeNew.offsetY = offsetY;
		nodeNew.bDepth = bDepth;
		nodeNew.fSearched = false;
		nodeNew.nindexParent = nindexParent;

		return m_nindexAvailableNode++;
	}

	void AddPathNodes(node_index_t nindexSource, TRACE_FL fNoMonsters) noexcept
	{
		AddPathNode(nindexSource, 1, 0, fNoMonsters);
		AddPathNode(nindexSource, -1, 0, fNoMonsters);
		AddPathNode(nindexSource, 0, 1, fNoMonsters);
		AddPathNode(nindexSource, 0, -1, fNoMonsters);
		AddPathNode(nindexSource, 1, 1, fNoMonsters);
		AddPathNode(nindexSource, 1, -1, fNoMonsters);
		AddPathNode(nindexSource, -1, 1, fNoMonsters);
		AddPathNode(nindexSource, -1, -1, fNoMonsters);
	}

	void AddPathNode(node_index_t nindexSource, int offsetX, int offsetY, TRACE_FL fNoMonsters) noexcept
	{
		Vector vecSource{}, vecDest{};
		int offsetXAbs{}, offsetYAbs{};
		uint8_t bDepth{};

		if (nindexSource == NODE_INVALID_EMPTY)
		{
			bDepth = 1;

			offsetXAbs = offsetX;
			offsetYAbs = offsetY;

			vecSource = m_vecStartingLoc;
			vecDest = vecSource + Vector(double(offsetX) * HOSTAGE_STEPSIZE, double(offsetY) * HOSTAGE_STEPSIZE, 0);
		}
		else
		{
			auto nodeCurrent = &m_nodeArr[nindexSource];
			offsetXAbs = offsetX + nodeCurrent->offsetX;
			offsetYAbs = offsetY + nodeCurrent->offsetY;

			if (m_nindexAvailableNode >= std::ssize(m_nodeArr))
			{
				m_nodeArr.resize(m_nindexAvailableNode * 2);
				g_engfuncs.pfnServerPrint(std::format("ARR EXPANDED: {}\n", m_nindexAvailableNode * 2).c_str());
			}

			auto nodeSource = &m_nodeArr[m_nindexAvailableNode];	// we actually need ptr here.

			// if there exists a node, then to ignore adding a the new node
			if (NodeExists(offsetXAbs, offsetYAbs) != NODE_INVALID_EMPTY)
			{
				return;
			}

			vecSource = nodeCurrent->vecLoc;
			vecDest = vecSource + Vector((double(offsetX) * HOSTAGE_STEPSIZE), (double(offsetY) * HOSTAGE_STEPSIZE), 0);

			if (m_nindexAvailableNode)
			{
				auto nindexCurrent = m_nindexAvailableNode;

				do
				{
					nodeSource--;
					nindexCurrent--;

					offsetX = (nodeSource->offsetX - offsetXAbs);

					if (offsetX >= 0)
					{
						if (offsetX > 1)
						{
							continue;
						}
					}
					else
					{
						if (-offsetX > 1)
						{
							continue;
						}
					}

					offsetY = (nodeSource->offsetY - offsetYAbs);

					if (offsetY >= 0)
					{
						if (offsetY > 1)
						{
							continue;
						}
					}
					else
					{
						if (-offsetY > 1)
						{
							continue;
						}
					}

					if (PathTraversable(nodeSource->vecLoc, &vecDest, fNoMonsters) != PTRAVELS_NO)
					{
						nodeCurrent = nodeSource;
						nindexSource = nindexCurrent;
					}
				} while (nindexCurrent);
			}

			vecSource = nodeCurrent->vecLoc;
			bDepth = (nodeCurrent->bDepth + 1) & 0xff;
		}

		if (PathTraversable(vecSource, &vecDest, fNoMonsters) != PTRAVELS_NO)
		{
			AddNode(nindexSource, vecDest, offsetXAbs, offsetYAbs, bDepth);
		}
	}

	node_index_t NodeExists(int offsetX, int offsetY) const noexcept
	{
		node_index_t nindexCurrent = NODE_INVALID_EMPTY;

		for (nindexCurrent = m_nindexAvailableNode - 1; nindexCurrent != NODE_INVALID_EMPTY; nindexCurrent--)
		{
			auto const& nodeCurrent = m_nodeArr[nindexCurrent];

			if (nodeCurrent.offsetX == offsetX && nodeCurrent.offsetY == offsetY)
			{
				break;
			}
		}

		return nindexCurrent;
	}

	node_index_t GetBestNode(Vector const& vecOrigin, Vector const& vecDest) const noexcept
	{
		auto nindexBest = NODE_INVALID_EMPTY;
		auto nindexCurrent = 0;
		auto flBestVal = 1000000.0;

		while (nindexCurrent < m_nindexAvailableNode)
		{
			auto const& nodeCurrent = m_nodeArr[nindexCurrent];

			if (!nodeCurrent.fSearched)
			{
				auto flZDiff = -1.0;
				auto const flDistFromStart = (vecDest - nodeCurrent.vecLoc).Length();
				auto const flDistToDest = nodeCurrent.vecLoc.z - vecDest.z;

				if (flDistToDest >= 0.0)
				{
					flZDiff = 1.0;
				}

				if ((flDistToDest * flZDiff) <= (float)cvar_stepsize)
					flZDiff = 1.0;
				else
					flZDiff = 1.25;

				auto const flCurrentVal = flZDiff * (double(nodeCurrent.bDepth) * HOSTAGE_STEPSIZE + flDistFromStart);
				if (flCurrentVal < flBestVal)
				{
					flBestVal = flCurrentVal;
					nindexBest = nindexCurrent;
				}
			}

			nindexCurrent++;
		}

		return nindexBest;
	}

#pragma endregion Path Building

#pragma region Path Verification

	auto SimplifiedPath(Vector const& vecSrc, TRACE_FL fNoMonsters = dont_ignore_glass | dont_ignore_monsters) noexcept -> std::pair<int, decltype(std::ranges::subrange(m_Segments.begin(), m_Segments.end()))>
	{
		auto& Path = m_Segments;
		Vector vecDummy{};
		int nCount{};

		// You cannot simplify across jumping and ladder points.

		for (nCount = 0; auto && Seg : Path)
		{
			if (Seg.how > NT_SIMPLE)
				break;

			++nCount;
		}

		// Including the first ladder we encounter.
		if (nCount < std::ssize(Path) &&
			(Path[nCount].how == GO_LADDER_DOWN || Path[nCount].how == GO_LADDER_UP))
		{
			++nCount;
		}

		std::ranges::subrange const SimplifiableSegments{ Path.begin(), Path.begin() + nCount };

		if (nCount <= 0)
			return { 0, Path };

		// Counting down from the farthest.
		for (nCount = std::ssize(SimplifiableSegments) - 1; auto && Seg : SimplifiableSegments | std::views::reverse)
		{
			ETraversable res{ PTRAVELS_NO };
			switch (Seg.how)
			{
			case GO_LADDER_UP:
				vecDummy = Seg.ladder->m_bottom/* + Vector{ Seg.ladder->m_dirVector, 0 } * 17.0*/;
				res = PathTraversable(vecSrc, &vecDummy, fNoMonsters);
				break;

				// Skipping the offset, as it is normally in mid-air
			case GO_LADDER_DOWN:
				vecDummy = Seg.ladder->m_top/* + Vector{ Seg.ladder->m_dirVector, 0 } * 17.0*/;
				res = PathTraversable(vecSrc, &vecDummy, fNoMonsters);
				break;

			default:
				res = PathTraversable(vecSrc, &Seg.pos, fNoMonsters);
				break;
			}

			assert(std::ssize(Path) > nCount);

			// The node gets modified in the process.
			if (res != PTRAVELS_NO)
				return { nCount, std::ranges::subrange{ Path.begin() + nCount, Path.end() - nCount } };

			--nCount;
		}

		// None of them can be simplified, so just return the whole path.
		return { 0, Path };
	}

	ETraversable PathTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters) const noexcept
	{
		TraceResult tr{};
		auto retval = PTRAVELS_NO;

		auto vecSrcTmp{ vecSource };
		auto vecDestTmp = *pvecDest - vecSource;

		auto vecDir = vecDestTmp.Normalize();
		vecDir.z = 0;

		auto flTotal = vecDestTmp.Length2D();

		while (flTotal > 1.0f)
		{
			if (flTotal >= (float)cvar_stepsize)
				vecDestTmp = vecSrcTmp + (vecDir * (float)cvar_stepsize);
			else
				vecDestTmp = *pvecDest;

			m_fTargetEntHit = false;

			if (PathClear(vecSrcTmp, vecDestTmp, fNoMonsters, &tr))
			{
				vecDestTmp = tr.vecEndPos;

				if (retval == PTRAVELS_NO)
				{
					retval = PTRAVELS_SLOPE;
				}
			}
			else
			{
				if (tr.fStartSolid)
				{
					return PTRAVELS_NO;
				}

				if (tr.pHit && !(std::to_underlying(fNoMonsters) & ignore_monsters) && tr.pHit->v.classname)
				{
					// #PF_LOCAL_HOSTAGE
					if (FClassnameIs(&tr.pHit->v, "hostage_entity"))
					{
						return PTRAVELS_NO;
					}
				}

				vecSrcTmp = tr.vecEndPos;

				if (tr.vecPlaneNormal.z <= MaxUnitZSlope)
				{
					if (StepTraversable(vecSrcTmp, &vecDestTmp, fNoMonsters, &tr))
					{
						if (retval == PTRAVELS_NO)
						{
							retval = PTRAVELS_STEP;
						}
					}
					else
					{
						if (!StepJumpable(vecSrcTmp, &vecDestTmp, fNoMonsters, &tr))
						{
							return PTRAVELS_NO;
						}

						if (retval == PTRAVELS_NO)
						{
							retval = PTRAVELS_STEPJUMPABLE;
						}
					}
				}
				else
				{
					if (!SlopeTraversable(vecSrcTmp, &vecDestTmp, fNoMonsters, &tr))
					{
						return PTRAVELS_NO;
					}

					if (retval == PTRAVELS_NO)
					{
						retval = PTRAVELS_SLOPE;
					}
				}
			}

			Vector const vecDropDest = vecDestTmp - Vector(0, 0, 300);

			if (PathClear(vecDestTmp, vecDropDest, fNoMonsters, &tr))
			{
				return (m_pHost && m_pHost->pev->movetype == MOVETYPE_FLY) ? PTRAVELS_MIDAIR : PTRAVELS_NO;
			}

			if (!tr.fStartSolid)
				vecDestTmp = tr.vecEndPos;

			vecSrcTmp = vecDestTmp;

			Vector const vecSrcThisTime = *pvecDest - vecDestTmp;

			if (m_fTargetEntHit)
				break;

			flTotal = vecSrcThisTime.Length2D();
		}

		*pvecDest = vecDestTmp;

		return retval;
	}

	bool PathClear(Vector const& vecOrigin, Vector const& vecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		g_engfuncs.pfnTraceMonsterHull(m_pHost.Get(), vecOrigin, vecDest, fNoMonsters, m_pHost.Get(), tr);

		if (tr->fStartSolid)
			return false;

		if (tr->flFraction == 1.0f)
			return true;

		if (m_pTargetEnt && tr->pHit == m_pTargetEnt.Get())
		{
			m_fTargetEntHit = true;
			return true;
		}

		return false;
	}
	bool PathClear(Vector const& vecSource, Vector const& vecDest, TRACE_FL fNoMonsters) const noexcept
	{
		TraceResult tr{};
		return PathClear(vecSource, vecDest, fNoMonsters, &tr);
	}

	bool SlopeTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		auto vecSlopeEnd = *pvecDest;
		auto vecDown = *pvecDest - vecSource;

		auto vecAngles = tr->vecPlaneNormal.VectorAngles();
		vecSlopeEnd.z = static_cast<vec_t>(vecDown.Length2D() * std::tan(double((90.0 - vecAngles.pitch) * (std::numbers::pi / 180.0))) + vecSource.z);

		if (!PathClear(vecSource, vecSlopeEnd, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
				return false;

			if ((tr->vecEndPos - vecSource).Length2D() < 1.0f)
				return false;
		}

		vecSlopeEnd = tr->vecEndPos;

		vecDown = vecSlopeEnd;
		vecDown.z -= (float)cvar_stepsize;

		if (!PathClear(vecSlopeEnd, vecDown, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
			{
				*pvecDest = vecSlopeEnd;
				return true;
			}
		}

		*pvecDest = tr->vecEndPos;
		return true;
	}

	bool StepTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		auto vecStepStart = vecSource;
		auto vecStepDest = *pvecDest;

		vecStepStart.z += (float)cvar_stepsize;
		vecStepDest.z = vecStepStart.z;

		if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
				return false;

			auto const flFwdFraction = (tr->vecEndPos - vecStepStart).Length();

			if (flFwdFraction < 1.0f)
				return false;
		}

		vecStepStart = tr->vecEndPos;

		vecStepDest = vecStepStart;
		vecStepDest.z -= (float)cvar_stepsize;

		if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
			{
				*pvecDest = vecStepStart;
				return true;
			}
		}

		*pvecDest = tr->vecEndPos;
		return true;
	}

	bool StepJumpable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		float flJumpHeight = (float)cvar_stepsize + 1.0f;

		auto vecStepStart = vecSource;
		vecStepStart.z += flJumpHeight;

		while (flJumpHeight < 40.0f)
		{
			auto vecStepDest = *pvecDest;
			vecStepDest.z = vecStepStart.z;

			if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
			{
				if (tr->fStartSolid)
					break;

				auto const flFwdFraction = (tr->vecEndPos - vecStepStart).Length2D();

				if (flFwdFraction < 1.0)
				{
					flJumpHeight += 10.0f;
					vecStepStart.z += 10.0f;

					continue;
				}
			}

			vecStepStart = tr->vecEndPos;
			vecStepDest = vecStepStart;
			vecStepDest.z -= (float)cvar_stepsize;

			if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
			{
				if (tr->fStartSolid)
				{
					*pvecDest = vecStepStart;
					return true;
				}
			}

			*pvecDest = tr->vecEndPos;
			return true;
		}

		return false;
	}

	node_index_t GetFurthestTraversableNode(Vector const& vecStartingLoc, std::vector<Vector>* prgvecNodes, TRACE_FL fNoMonsters) const noexcept
	{
		for (int nCount = 0; auto && vecNode : *prgvecNodes)
		{
			if (PathTraversable(vecStartingLoc, &vecNode, fNoMonsters) != PTRAVELS_NO)
				return nCount;

			++nCount;
		}

		return NODE_INVALID_EMPTY;
	}


#pragma endregion Path Verification

};

export enum ETaskIdMonster : std::uint64_t
{
	TASK_INIT = 1ull << 1,
	TASK_REMOVE = 1ull << 2,
	TASK_DEBUG = 1ull << 3,
	TASK_EVENTS = 1ull << 4,

	TASK_MOVE_TURNING = 1ull << 8,
	TASK_MOVE_WALKING = 1ull << 9,
	TASK_MOVE_CROUCHING = 1ull << 10,
	TASK_MOVEMENTS_SIMPLE = TASK_MOVE_TURNING | TASK_MOVE_WALKING | TASK_MOVE_CROUCHING,

	TASK_MOVE_DETOUR = 1ull << 11,
	TASK_MOVE_LADDER = 1ull << 12,
	TASK_MOVEMENTS_COMPLICATE = TASK_MOVE_DETOUR | TASK_MOVE_LADDER,

	TASK_ANIM_INTERCEPTING = 1ull << 16,
	TASK_ANIMATIONS = TASK_ANIM_INTERCEPTING,

	TASK_PLOT_WALK_TO = 1ull << 24,
	TASK_PLOT_PATROL = 1ull << 25,
};

export struct CBaseAI : Prefab_t
{
	static inline constexpr char CLASSNAME[] = "mob_hgrunt";

	static inline decltype(GoldSrc::m_StudioInfo)::value_type::second_type* m_ModelInfo{};
	using seq_info_t = std::remove_pointer_t<decltype(m_ModelInfo)>::value_type::second_type;
	static inline std::array<std::vector<seq_info_t const*>, 0xFF> m_ActSequences{};

	CNavPath m_path{};
	CLocalNav m_localnav{};
	Navigator m_nav{};

	Vector m_vecGoal{};
	EHANDLE<CBaseEntity> m_pTargetEnt{};
	seq_info_t const* m_pCurInceptingAnim{};	// Promised by: InsertingAnim()
	bool m_bYawReady : 1 { false };
	mutable bool m_fTargetEntHit : 1 { false };

	// Entity properties

	void Spawn() noexcept override
	{
		static bool bPrecached = false;
		if (!bPrecached)
		{
			GoldSrc::CacheStudioModelInfo("models/hgrunt.mdl");
			m_ModelInfo = &GoldSrc::m_StudioInfo.at("models/hgrunt.mdl");

			// Must place after m_ModelInfo prep.
			PrepareActivityInfo("models/hgrunt.mdl");

			bPrecached = true;
		}

		// Must leave it out. Say, what if the map changed?
		if (LoadNavigationMap() != NAV_OK)
			g_engfuncs.pfnServerPrint("NAV map no found when trying to create AI!\n");

		pev->solid = SOLID_SLIDEBOX;
		pev->movetype = MOVETYPE_STEP;
		pev->effects = 0;
		pev->health = 100.f;
		pev->flags |= FL_MONSTER;
		pev->gravity = 1.f;

		g_engfuncs.pfnSetModel(edict(), "models/hgrunt.mdl");	// LUNA: Fuck GoldSrc. This one must place before SetSize()
		g_engfuncs.pfnSetSize(edict(), VEC_HUMAN_HULL_MIN, VEC_HUMAN_HULL_MAX);

		pev->takedamage = DAMAGE_YES;
		pev->ideal_yaw = pev->angles.yaw;
		pev->max_health = pev->health;
		pev->deadflag = DEAD_NO;

		pev->view_ofs = g_engfuncs.pfnGetModelPtr(edict())->eyeposition;

		pev->sequence = m_ModelInfo->at("idle2").m_iSeqIdx;
		pev->animtime = gpGlobals->time;
		pev->framerate = 1.f;
		pev->frame = 0;

		UTIL_SetController(edict(), 0, 0);

		m_Scheduler.Policy() = ESchedulerPolicy::UNORDERED;

		m_Scheduler.Enroll(Task_Init(), TASK_INIT, true);
//		m_Scheduler.Enroll(Task_Kill(), TASK_REMOVE, true);
		m_Scheduler.Enroll(Task_Event_Dispatch());

		m_localnav.m_pOwner = this;
		m_nav.m_pHost = this;
	}

	constexpr int Classify() noexcept override { return CLASS_HUMAN_MILITARY; }

	// Improv properties

/*
	bool IsAlive() const noexcept override { return pev->deadflag == DEAD_NO; }

	void MoveTo(const Vector& goal) noexcept override { Plot_PathToLocation(goal); }
	void LookAt(const Vector& target) noexcept override
	{
		auto const YAW = (float)(goal - pev->origin).Yaw();

		UTIL_SetController(edict(), 0, YAW);
	}
	void ClearLookAt() noexcept override {}	// It's a immed set.
*/

	virtual void FaceTo(const Vector& where) noexcept
	{
		Turn(
			(where - pev->origin).Yaw()
		);
	}
	virtual void ClearFaceTo() noexcept { m_Scheduler.Delist(TASK_MOVE_TURNING); }

	virtual const Vector& GetFeet() const noexcept { return pev->origin; };		// return position of "feet" - point below centroid of improv at feet level
	virtual Vector GetCentroid() const noexcept { return pev->origin + (VEC_HUMAN_HULL_MIN + VEC_HUMAN_HULL_MAX) / 2.f; };
	virtual Vector GetEyes() const noexcept { return pev->origin + pev->view_ofs; };

	// Plots

	// Util

	// @promise: m_pCurInceptingAnim
	// @awaiting: TASK_ANIM_INTERCEPTING
	__forceinline void InsertingAnim(Activity activity) noexcept
	{
		m_Scheduler.Enroll(Task_Anim_Intercepting(activity), TASK_ANIM_INTERCEPTING, true);
	}

	inline auto PlayAnim(std::string_view what) noexcept -> decltype(&m_ModelInfo->find(what)->second)
	{
		if (auto const it = m_ModelInfo->find(what); it != m_ModelInfo->end())
		{
			pev->sequence = it->second.m_iSeqIdx;
			pev->animtime = gpGlobals->time;
			pev->framerate = it->second.m_flFrameRate;
			pev->frame = 0;

			return &it->second;
		}

		return nullptr;
	}

	virtual auto PlayAnim(Activity activity) noexcept -> decltype(&m_ModelInfo->at(""))
	{
		seq_info_t const* seq{};

		auto const DrawRandom =
			[&seq](Activity activity) noexcept
			{
				if (!m_ActSequences[activity].empty())
					seq = UTIL_GetRandomOne(m_ActSequences[activity]);
			};

		switch (activity)
		{
		case ACT_IDLE:
			DrawRandom(activity);
			break;

		case ACT_WALK:
			if (pev->health < pev->max_health * 0.65f)
				DrawRandom(ACT_WALK_HURT);
			if (!seq)
				DrawRandom(activity);
			break;

		case ACT_RUN:
			if (pev->health < pev->max_health * 0.65f)
				DrawRandom(ACT_RUN_HURT);
			if (!seq)
				DrawRandom(activity);
			break;

		case ACT_INVALID:
		default:
			break;
		}

		if (seq)
		{
			pev->sequence = seq->m_iSeqIdx;
			pev->animtime = gpGlobals->time;
			pev->framerate = 1.f;
			pev->frame = 0;

			pev->maxspeed = seq->m_flGroundSpeed;
		}

		return nullptr;
	}

	inline bool IsAnimPlaying(std::string_view what) const noexcept
	{
		if (auto const it = m_ModelInfo->find(what);
			it != m_ModelInfo->end())
		{
			return pev->sequence == it->second.m_iSeqIdx;
		}

		// No info.
		return false;
	}

	virtual bool IsAnimPlaying(Activity activity) const noexcept
	{
		auto const pmodel = g_engfuncs.pfnGetModelPtr(edict());

		if (pev->sequence >= (signed)pmodel->numseq || pev->sequence < 0)
			return false;

		auto const pseqdesc =
			(mstudioseqdesc_t*)((std::byte*)pmodel + pmodel->seqindex) + pev->sequence;

		switch (activity)
		{
		case ACT_IDLE:
			return
				pseqdesc->activity == ACT_COMBAT_IDLE
				|| pseqdesc->activity == ACT_CROUCH_IDLE
				|| pseqdesc->activity == ACT_CROUCH_IDLE_FIDGET
				|| pseqdesc->activity == ACT_CROUCH_IDLE_SCARED
				|| pseqdesc->activity == ACT_CROUCH_IDLE_SCARED_FIDGET
				|| pseqdesc->activity == ACT_CROUCHIDLE
				|| pseqdesc->activity == ACT_FOLLOW_IDLE
				|| pseqdesc->activity == ACT_FOLLOW_IDLE_FIDGET
				|| pseqdesc->activity == ACT_FOLLOW_IDLE_SCARED
				|| pseqdesc->activity == ACT_FOLLOW_IDLE_SCARED_FIDGET
				|| pseqdesc->activity == ACT_IDLE
				|| pseqdesc->activity == ACT_IDLE_ANGRY
				|| pseqdesc->activity == ACT_IDLE_FIDGET
				|| pseqdesc->activity == ACT_IDLE_SCARED
				|| pseqdesc->activity == ACT_IDLE_SCARED_FIDGET
				|| pseqdesc->activity == ACT_IDLE_SNEAKY
				|| pseqdesc->activity == ACT_IDLE_SNEAKY_FIDGET;

		case ACT_RUN:
			return
				pseqdesc->activity == ACT_RUN
				|| pseqdesc->activity == ACT_RUN_HURT
				|| pseqdesc->activity == ACT_RUN_SCARED;

		case ACT_WALK:
			return
				pseqdesc->activity == ACT_CROUCH_WALK
				|| pseqdesc->activity == ACT_CROUCH_WALK_SCARED
				|| pseqdesc->activity == ACT_WALK
				|| pseqdesc->activity == ACT_WALK_BACK
				|| pseqdesc->activity == ACT_WALK_HURT
				|| pseqdesc->activity == ACT_WALK_SCARED
				|| pseqdesc->activity == ACT_WALK_SNEAKY;

		default:
			return pseqdesc->activity == activity;
		}

		std::unreachable();
		return false;
	}

	// @awaiting: TASK_MOVE_TURNING
	inline void Turn(std::floating_point auto YAW, bool bSkipAnim = false) noexcept
	{
		if (std::abs(YAW - pev->ideal_yaw) > 1 && std::abs(YAW - pev->angles.yaw) > 1)
		{
			pev->ideal_yaw = (float)YAW;
			m_Scheduler.Enroll(Task_Move_Turn(bSkipAnim), TASK_MOVE_TURNING, true);
		}
	}

	// @awaiting: TASK_MOVE_DETOUR
	__forceinline void Detour(Vector const& vecTarget, double flApprox = VEC_HUMAN_HULL_MAX.x + 1.0) noexcept
	{
		m_Scheduler.Enroll(
			Task_Move_Detour(vecTarget, flApprox),
			TASK_MOVE_DETOUR,
			true
		);
	}

	// @awaiting: TASK_MOVE_LADDER
	__forceinline void Climb(PathSegment const& segment, CNavArea const* pNextArea = nullptr) noexcept
	{
		m_Scheduler.Enroll(
			Task_Move_Ladder(segment.ladder, segment.how, pNextArea),
			TASK_MOVE_LADDER,
			true
		);
	}

	// Path Verification

	auto SimplifiedPath(TRACE_FL fNoMonsters, std::span<PathSegment> Path = {}) noexcept -> decltype(this->m_path.Inspect())
	{
		if (Path.empty())
			Path = m_path.Inspect();

		decltype(std::addressof(Path[0])) pStart{};
		Vector vecDummy{};
		size_t nCount{};

		// You cannot simplify across jumping and ladder points.

		for (nCount = 0; auto&& Seg : Path)
		{
			if (Seg.how > NT_SIMPLE)
				break;

			++nCount;
		}

		// Including the first ladder we encounter.
		if (nCount < Path.size() &&
			(Path[nCount].how == GO_LADDER_DOWN || Path[nCount].how == GO_LADDER_UP))
		{
			++nCount;
		}

		std::span const SimplifiableSegments{ Path.subspan(0, nCount) };

		if (!nCount)
			return Path;

		// Counting down from the farthest.
		for (nCount = SimplifiableSegments.size() - 1; auto&& Seg : SimplifiableSegments | std::views::reverse)
		{
			ETraversable res{ PTRAVELS_NO };
			switch (Seg.how)
			{
			case GO_LADDER_UP:
				vecDummy = Seg.ladder->m_bottom/* + Vector{ Seg.ladder->m_dirVector, 0 } * 17.0*/;
				res = PathTraversable(pev->origin, &vecDummy, fNoMonsters);
				break;

				// Skipping the offset, as it is normally in mid-air
			case GO_LADDER_DOWN:
				vecDummy = Seg.ladder->m_top/* + Vector{ Seg.ladder->m_dirVector, 0 } * 17.0*/;
				res = PathTraversable(pev->origin, &vecDummy, fNoMonsters);
				break;

			default:
				res = PathTraversable(pev->origin, &Seg.pos, fNoMonsters);
				break;
			}

			assert(Path.size() >= nCount);

			// The node gets modified in the process.
			if (res != PTRAVELS_NO)
				return std::span{ Path.data() + nCount, Path.size() - nCount };

			--nCount;
		}

		// None of them can be simplified, so just return the whole path.
		return Path;
	}

	ETraversable PathTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters) const noexcept
	{
		TraceResult tr{};
		auto retval = PTRAVELS_NO;

		auto vecSrcTmp{ vecSource };
		auto vecDestTmp = *pvecDest - vecSource;

		auto vecDir = vecDestTmp.Normalize();
		vecDir.z = 0;

		auto flTotal = vecDestTmp.Length2D();

		while (flTotal > 1.0f)
		{
			if (flTotal >= (float)cvar_stepsize)
				vecDestTmp = vecSrcTmp + (vecDir * (float)cvar_stepsize);
			else
				vecDestTmp = *pvecDest;

			m_fTargetEntHit = false;

			if (PathClear(vecSrcTmp, vecDestTmp, fNoMonsters, &tr))
			{
				vecDestTmp = tr.vecEndPos;

				if (retval == PTRAVELS_NO)
				{
					retval = PTRAVELS_SLOPE;
				}
			}
			else
			{
				if (tr.fStartSolid)
				{
					return PTRAVELS_NO;
				}

				if (tr.pHit && !(std::to_underlying(fNoMonsters) & ignore_monsters) && tr.pHit->v.classname)
				{
					// #PF_LOCAL_HOSTAGE
					if (FClassnameIs(&tr.pHit->v, "hostage_entity"))
					{
						return PTRAVELS_NO;
					}
				}

				vecSrcTmp = tr.vecEndPos;

				if (tr.vecPlaneNormal.z <= MaxUnitZSlope)
				{
					if (StepTraversable(vecSrcTmp, &vecDestTmp, fNoMonsters, &tr))
					{
						if (retval == PTRAVELS_NO)
						{
							retval = PTRAVELS_STEP;
						}
					}
					else
					{
						if (!StepJumpable(vecSrcTmp, &vecDestTmp, fNoMonsters, &tr))
						{
							return PTRAVELS_NO;
						}

						if (retval == PTRAVELS_NO)
						{
							retval = PTRAVELS_STEPJUMPABLE;
						}
					}
				}
				else
				{
					if (!SlopeTraversable(vecSrcTmp, &vecDestTmp, fNoMonsters, &tr))
					{
						return PTRAVELS_NO;
					}

					if (retval == PTRAVELS_NO)
					{
						retval = PTRAVELS_SLOPE;
					}
				}
			}

			Vector const vecDropDest = vecDestTmp - Vector(0, 0, 300);

			if (PathClear(vecDestTmp, vecDropDest, fNoMonsters, &tr))
			{
				return pev->movetype == MOVETYPE_FLY ? PTRAVELS_MIDAIR : PTRAVELS_NO;
			}

			if (!tr.fStartSolid)
				vecDestTmp = tr.vecEndPos;

			vecSrcTmp = vecDestTmp;

			Vector const vecSrcThisTime = *pvecDest - vecDestTmp;

			if (m_fTargetEntHit)
				break;

			flTotal = vecSrcThisTime.Length2D();
		}

		*pvecDest = vecDestTmp;

		return retval;
	}

	bool PathClear(Vector const& vecOrigin, Vector const& vecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		g_engfuncs.pfnTraceMonsterHull(edict(), vecOrigin, vecDest, fNoMonsters, edict(), tr);

		if (tr->fStartSolid)
			return false;

		if (tr->flFraction == 1.0f)
			return true;

		if (m_pTargetEnt && tr->pHit == m_pTargetEnt.Get())
		{
			m_fTargetEntHit = true;
			return true;
		}

		return false;
	}
	bool PathClear(Vector const& vecSource, Vector const& vecDest, TRACE_FL fNoMonsters) const noexcept
	{
		TraceResult tr{};
		return PathClear(vecSource, vecDest, fNoMonsters, &tr);
	}

	bool SlopeTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		auto vecSlopeEnd = *pvecDest;
		auto vecDown = *pvecDest - vecSource;

		auto vecAngles = tr->vecPlaneNormal.VectorAngles();
		vecSlopeEnd.z = static_cast<vec_t>(vecDown.Length2D() * std::tan(double((90.0 - vecAngles.pitch) * (std::numbers::pi / 180.0))) + vecSource.z);

		if (!PathClear(vecSource, vecSlopeEnd, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
				return false;

			if ((tr->vecEndPos - vecSource).Length2D() < 1.0f)
				return false;
		}

		vecSlopeEnd = tr->vecEndPos;

		vecDown = vecSlopeEnd;
		vecDown.z -= (float)cvar_stepsize;

		if (!PathClear(vecSlopeEnd, vecDown, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
			{
				*pvecDest = vecSlopeEnd;
				return true;
			}
		}

		*pvecDest = tr->vecEndPos;
		return true;
	}

	bool LadderTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		auto vecStepStart = tr->vecEndPos;
		auto vecStepDest = vecStepStart;
		vecStepDest.z += HOSTAGE_STEPSIZE;

		if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
				return false;

			if ((tr->vecEndPos - vecStepStart).Length() < 1.0f)
				return false;
		}

		vecStepStart = tr->vecEndPos;
		pvecDest->z = tr->vecEndPos.z;

		return PathTraversable(vecStepStart, pvecDest, fNoMonsters);
	}

	bool StepTraversable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		auto vecStepStart = vecSource;
		auto vecStepDest = *pvecDest;

		vecStepStart.z += (float)cvar_stepsize;
		vecStepDest.z = vecStepStart.z;

		if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
				return false;

			auto const flFwdFraction = (tr->vecEndPos - vecStepStart).Length();

			if (flFwdFraction < 1.0f)
				return false;
		}

		vecStepStart = tr->vecEndPos;

		vecStepDest = vecStepStart;
		vecStepDest.z -= (float)cvar_stepsize;

		if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
		{
			if (tr->fStartSolid)
			{
				*pvecDest = vecStepStart;
				return true;
			}
		}

		*pvecDest = tr->vecEndPos;
		return true;
	}

	bool StepJumpable(Vector const& vecSource, Vector* pvecDest, TRACE_FL fNoMonsters, TraceResult* tr) const noexcept
	{
		float flJumpHeight = (float)cvar_stepsize + 1.0f;

		auto vecStepStart = vecSource;
		vecStepStart.z += flJumpHeight;

		while (flJumpHeight < 40.0f)
		{
			auto vecStepDest = *pvecDest;
			vecStepDest.z = vecStepStart.z;

			if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
			{
				if (tr->fStartSolid)
					break;

				auto const flFwdFraction = (tr->vecEndPos - vecStepStart).Length2D();

				if (flFwdFraction < 1.0)
				{
					flJumpHeight += 10.0f;
					vecStepStart.z += 10.0f;

					continue;
				}
			}

			vecStepStart = tr->vecEndPos;
			vecStepDest = vecStepStart;
			vecStepDest.z -= (float)cvar_stepsize;

			if (!PathClear(vecStepStart, vecStepDest, fNoMonsters, tr))
			{
				if (tr->fStartSolid)
				{
					*pvecDest = vecStepStart;
					return true;
				}
			}

			*pvecDest = tr->vecEndPos;
			return true;
		}

		return false;
	}



	Task Task_Init() noexcept
	{
		co_await 0.1f;

		pev->origin.z += 1;
		g_engfuncs.pfnDropToFloor(edict());

		// Try to move the monster to make sure it's not stuck in a brush.
		if (!g_engfuncs.pfnWalkMove(edict(), 0, 0, WALKMOVE_NORMAL))
		{
			g_engfuncs.pfnAlertMessage(at_error, "Monster %s stuck in wall--level design error", STRING(pev->classname));
			pev->effects = EF_BRIGHTFIELD;
		}
	}

	Task Task_Kill(float const flTime = 20.f) noexcept
	{
		if (flTime > 0.f)
			co_await flTime;

		pev->flags |= FL_KILLME;
		pev->solid = SOLID_NOT;
		pev->takedamage = DAMAGE_NO;

		co_return;
	}

	// Movement Tasks

	Task Task_Move_Turn(bool const bSkipAnim = false) noexcept;
	Task Task_Move_Walk(Vector const vecTarget, Activity iMoveType = ACT_RUN, double const flApprox = VEC_HUMAN_HULL_MAX.x + 1.0) noexcept;
	Task Task_Move_Detour(Vector const vecTarget, double const flApprox = VEC_HUMAN_HULL_MAX.x + 1.0) noexcept;
	Task Task_Move_Ladder(CNavLadder const* ladder, NavTraverseType how, CNavArea const* pNextArea = nullptr) noexcept;

	Task Task_Patrolling(Vector const vecTarget) noexcept
	{
		auto const vec = pev->origin;

		for (;;)
		{
			m_Scheduler.Enroll(Task_Plot_WalkOnPath(vecTarget, 40), TASK_PLOT_WALK_TO, true);

			while (m_Scheduler.Exist(TASK_PLOT_WALK_TO))
				co_await 1.f;

			m_Scheduler.Enroll(Task_Plot_WalkOnPath(vec, 40), TASK_PLOT_WALK_TO, true);

			while (m_Scheduler.Exist(TASK_PLOT_WALK_TO))
				co_await 1.f;

			co_await TaskScheduler::NextFrame::Rank[1];
		}
	}

	Task Task_Plot_WalkOnPath(Vector const vecTarget, double flApprox = VEC_HUMAN_HULL_MAX.x + 1.0) noexcept;

	Task Task_Anim_Intercepting(Activity activity) noexcept
	{
		m_pCurInceptingAnim = nullptr;

		if (!m_ActSequences[activity].empty())
			m_pCurInceptingAnim = UTIL_GetRandomOne(m_ActSequences[activity]);

		if (!m_pCurInceptingAnim)
			co_return;

		pev->sequence = m_pCurInceptingAnim->m_iSeqIdx;
		pev->animtime = gpGlobals->time;
		pev->framerate = m_pCurInceptingAnim->m_flFrameRate;
		pev->frame = 0;

		pev->maxspeed = m_pCurInceptingAnim->m_flGroundSpeed;

		// keep the task alive, occupying the channel.
		co_await m_pCurInceptingAnim->m_total_length;

		co_return;
	}

	Task Task_Debug_ShowPath(Vector const vecSrc) noexcept
	{
#ifdef _DEBUG
		static constexpr Vector VEC_OFS{ 0, 0, VEC_DUCK_HULL_MAX.z / 2.f };
		static constexpr std::array<std::string_view, NUM_TRAVERSE_TYPES> TRAV_MEANS =
		{
			"GO_NORTH",
			"GO_EAST",
			"GO_SOUTH",
			"GO_WEST",
			"GO_DIRECTLY",
			"GO_LADDER_UP",
			"GO_LADDER_DOWN",
			"GO_JUMP",
		};

		auto segments = m_nav.m_Segments;	// must be a copy. This is a coro.

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
			co_await 0.01f;	// avoid inf loop.

			if (segments.size() >= 1)
			{
				UTIL_DrawBeamPoints(
					vecSrc + VEC_OFS,
					segments.front().pos + VEC_OFS,
					5, 255, 255, 255
				);
			}

			for (auto&& [src, dest] : segments /*| std::views::take(15)*/ | std::views::adjacent<2>)
			{
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

				co_await 0.01f;
			}

			UTIL_DrawBeamPoints(
				m_vecGoal,
				{ m_vecGoal.x, m_vecGoal.y, m_vecGoal.z + 36.f },
				5, 0x09, 0x10, 0x57
			);

			UTIL_DrawBeamPoints(
				pev->origin,
				pev->origin + pev->angles.Front() * (float)cvar_stepsize,
				5, 0, 0xFF, 0x9C
			);
		}
#endif

		co_return;
	}

	Task Task_Event_Dispatch() noexcept
	{
		for (;;)
		{
			co_await TaskScheduler::NextFrame::Rank.back();

			if (IsAnimPlaying(ACT_WALK))
			{
				auto const vecDir = (m_vecGoal - pev->origin).Make2D().Normalize();

				pev->velocity.x = vecDir.x * pev->maxspeed;
				pev->velocity.y = vecDir.y * pev->maxspeed;
			}

			//g_engfuncs.pfnServerPrint(
			//	std::format("{}\n", pev->frame).c_str()
			//);
		}

		co_return;
	}

	// Static Precache

	static void PrepareActivityInfo(std::string_view szModel) noexcept;
};
