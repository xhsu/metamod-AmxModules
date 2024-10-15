module;

#include <assert.h>

export module Improvisational;

import std;
import hlsdk;

import CBase;
import Nav;



#pragma region Util

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

constexpr float NormalizeAnglePositive(float angle) noexcept
{
	while (angle < 0.0f)
		angle += 360.0f;

	while (angle >= 360.0f)
		angle -= 360.0f;

	return angle;
}

struct cos_table_t final
{
	cos_table_t() noexcept
	{
		for (size_t i = 0; i < m_table.size(); ++i)
		{
			auto const angle = 2.0 * std::numbers::pi * double(i) / double(m_table.size() - 1);
			m_table[i] = std::cos(angle);
		}
	}

	double const& operator[](std::integral auto i) const noexcept { return m_table[i]; }
	double const& operator[](std::floating_point auto angle) const noexcept { size_t i = (size_t)(angle * (double(m_table.size() - 1) / 360.0)); return m_table[i]; }

	std::array<double, 256> m_table{};
};

export inline auto BotCOS(float angle) noexcept
{
	static cos_table_t cosTable{};

	angle = NormalizeAnglePositive(angle);
	return cosTable[angle];
}

export inline auto BotSIN(float angle) noexcept
{
	static cos_table_t cosTable{};

	angle = NormalizeAnglePositive(angle - 90);
	return cosTable[angle];
}

#pragma endregion Util


// Find path from startArea to goalArea via an A* search, using supplied cost heuristic.
// If cost functor returns -1 for an area, that area is considered a dead end.
// This doesn't actually build a path, but the path is defined by following parent
// pointers back from goalArea to startArea.
// If 'closestArea' is non-NULL, the closest area to the goal is returned (useful if the path fails).
// If 'goalArea' is NULL, will compute a path as close as possible to 'goalPos'.
// If 'goalPos' is NULL, will use the center of 'goalArea' as the goal position.
// Returns true if a path exists.
bool NavAreaBuildPath(
	CNavArea* startArea,
	CNavArea* goalArea,
	const Vector& goalPos,
	std::move_only_function<float(CNavArea*, CNavArea*, const CNavLadder*) noexcept>& costFunc,
	CNavArea** closestArea = nullptr) noexcept
{
	if (closestArea)
		*closestArea = nullptr;

	if (!startArea)
		return false;

	// If goalArea is NULL, this function will return the closest area to the goal.
	// However, if there is also no goal, we can't do anything.
	if (!goalArea/* && !goalPos*/)
	{
		return false;
	}

	startArea->SetParent(nullptr);

	// if we are already in the goal area, build trivial path
	if (startArea == goalArea)
	{
		goalArea->SetParent(nullptr);

		if (closestArea)
			*closestArea = goalArea;

		return true;
	}

	// determine actual goal position
	Vector const actualGoalPos = (goalPos != nullptr) ? goalPos : goalArea->GetCenter();

	// start search
	CNavArea::ClearSearchLists();

	// compute estimate of path length
	// #PF_TODO: Cost might work as "manhattan distance"
	// LUNA: manhattan distance for 3-dim vectors: std::abs(x1-x2) + abs(y1-y2) + abs(z1-z2)
	startArea->SetTotalCost((float)(startArea->GetCenter() - actualGoalPos).Length());

	auto const initCost = costFunc(startArea, nullptr, nullptr);
	if (initCost < 0.0f)
		return false;

	startArea->SetCostSoFar(initCost);
	startArea->AddToOpenList();

	// keep track of the area we visit that is closest to the goal
	if (closestArea)
		*closestArea = startArea;

	auto closestAreaDist = startArea->GetTotalCost();

	// do A* search
	while (!CNavArea::IsOpenListEmpty())
	{
		// get next area to check
		CNavArea* area = CNavArea::PopOpenList();

		// check if we have found the goal area
		if (area == goalArea)
		{
			if (closestArea)
				*closestArea = goalArea;

			return true;
		}

		// search adjacent areas
		bool searchFloor = true;
		std::underlying_type_t<NavDirType> dir = NORTH;
		auto floorList = area->GetAdjacentList(NORTH);
		auto floorIter = floorList->begin();

		bool ladderUp = true;
		const NavLadderList* ladderList = nullptr;
		NavLadderList::const_iterator ladderIter{};

		enum ELadderTopDir
		{
			AHEAD = 0,
			LEFT,
			RIGHT,
			BEHIND,
			NUM_TOP_DIRECTIONS
		};
		std::underlying_type_t<ELadderTopDir> ladderTopDir{};

		while (true)
		{
			CNavArea* newArea = nullptr;
			NavTraverseType how{};
			const CNavLadder* ladder = nullptr;

			// Get next adjacent area - either on floor or via ladder
			if (searchFloor)
			{
				// if exhausted adjacent connections in current direction, begin checking next direction
				if (floorIter == floorList->end())
				{
					++dir;

					if (dir >= NUM_DIRECTIONS)
					{
						// checked all directions on floor - check ladders next
						searchFloor = false;

						ladderList = area->GetLadderList(LADDER_UP);
						ladderIter = ladderList->begin();
						ladderTopDir = AHEAD;
					}
					else
					{
						// start next direction
						floorList = area->GetAdjacentList((NavDirType)dir);
						floorIter = floorList->begin();
					}
					continue;
				}

				newArea = (*floorIter).area;
				how = (NavTraverseType)dir;
				++floorIter;

				assert(newArea);

				if (!newArea)
					continue;
			}
			// search ladders
			else
			{
				if (ladderIter == ladderList->end())
				{
					if (!ladderUp)
					{
						// checked both ladder directions - done
						break;
					}
					else
					{
						// check down ladders
						ladderUp = false;
						ladderList = area->GetLadderList(LADDER_DOWN);
						ladderIter = ladderList->begin();
					}
					continue;
				}

				if (ladderUp)
				{
					ladder = (*ladderIter);

					// cannot use this ladder if the ladder bottom is hanging above our head
					if (ladder->m_isDangling)
					{
						ladderIter++;
						continue;
					}

					// do not use BEHIND connection, as its very hard to get to when going up a ladder
					if (ladderTopDir == AHEAD)
						newArea = ladder->m_topForwardArea;
					else if (ladderTopDir == LEFT)
						newArea = ladder->m_topLeftArea;
					else if (ladderTopDir == RIGHT)
						newArea = ladder->m_topRightArea;
					else
					{
						ladderIter++;
						continue;
					}

					how = GO_LADDER_UP;
					ladderTopDir++;
				}
				else
				{
					newArea = (*ladderIter)->m_bottomArea;
					how = GO_LADDER_DOWN;
					ladder = (*ladderIter);
					ladderIter++;
				}

				if (!newArea)
					continue;
			}

			// don't backtrack
			if (newArea == area)
				continue;

			auto const newCostSoFar = costFunc(newArea, area, ladder);

			// check if cost functor says this area is a dead-end
			if (newCostSoFar < 0.0f)
				continue;

			if ((newArea->IsOpen() || newArea->IsClosed()) && newArea->GetCostSoFar() <= newCostSoFar)
			{
				// this is a worse path - skip it
				continue;
			}
			else
			{
				// compute estimate of distance left to go
				auto const newCostRemaining = (newArea->GetCenter() - actualGoalPos).Length();

				// track closest area to goal in case path fails
				if (closestArea && newCostRemaining < closestAreaDist)
				{
					*closestArea = newArea;
					closestAreaDist = (float)newCostRemaining;
				}

				newArea->SetParent(area, how);
				newArea->SetCostSoFar(newCostSoFar);
				newArea->SetTotalCost(newCostSoFar + (float)newCostRemaining);

				if (newArea->IsClosed())
					newArea->RemoveFromClosedList();

				if (newArea->IsOpen())
				{
					// area already on open list, update the list order to keep costs sorted
					newArea->UpdateOnOpenList();
				}
				else
					newArea->AddToOpenList();
			}
		}

		// we have searched this area
		area->AddToClosedList();
	}

	return false;
}

// Check LOS, ignoring any entities that we can walk through
inline bool IsWalkableTraceLineClear(Vector const& from, Vector const& to, unsigned int flags = 0) noexcept
{
	TraceResult result{};
	edict_t* pEntIgnore = nullptr;
	edict_t* pEntPrev = nullptr;
	Vector useFrom = from;

	static constexpr auto maxTries = 50;
	for (auto t = 0; t < maxTries; ++t)
	{
		g_engfuncs.pfnTraceLine(useFrom, to, ignore_monsters | dont_ignore_glass, pEntIgnore, &result);

		// if we hit a walkable entity, try again
		if (result.flFraction != 1.0f && (result.pHit && IsEntityWalkable(&result.pHit->v, flags)))
		{
			if (result.pHit == pEntPrev)
				return false; // deadlock, give up

			pEntPrev = pEntIgnore;
			pEntIgnore = result.pHit;

			// start from just beyond where we hit to avoid infinite loops
			auto const dir = (to - from).Normalize();
			useFrom = result.vecEndPos + 5.0f * dir;
		}
		else
		{
			break;
		}
	}

	if (result.flFraction == 1.0f)
		return true;

	return false;
}

// Functor used with NavAreaBuildPath() for building Hostage paths.
// Once we hook up crouching and ladders, this can be removed and ShortestPathCost() can be used instead.
export struct HostagePathCost
{
	float operator()(CNavArea* area, CNavArea* fromArea, const CNavLadder* ladder) const noexcept
	{
		if (fromArea == nullptr)
		{
			// first area in path, no cost
			return 0.0f;
		}
		else
		{
			// compute distance travelled along path so far
			float dist{};

			if (ladder)
			{
				static constexpr float ladderCost = 10.0f;
				return ladder->m_length * ladderCost + fromArea->GetCostSoFar();
			}
			else
			{
				dist = (float)(area->GetCenter() - fromArea->GetCenter()).Length();
			}

			float cost = dist + fromArea->GetCostSoFar();

			// if this is a "crouch" area, add penalty
			if (area->GetAttributes() & NAV_CROUCH)
			{
				static constexpr float crouchPenalty = 10.0f;
				cost += crouchPenalty * dist;
			}

			// if this is a "jump" area, add penalty
			if (area->GetAttributes() & NAV_JUMP)
			{
				static constexpr float jumpPenalty = 10.0f;
				cost += jumpPenalty * dist;
			}

			return cost;
		}
	}
};
















export struct PathSegment
{
	CNavArea* area{};			// the area along the path
	NavTraverseType how{};		// how to enter this area from the previous one
	Vector pos{};				// our movement goal position at this point in the path
	const CNavLadder* ladder{};	// if "how" refers to a ladder, this is it
};

export struct CNavPath
{
	constexpr auto operator[](int i) noexcept -> PathSegment* { return (i >= 0 && i < m_segmentCount) ? &m_path[i] : nullptr; }
	constexpr auto operator[](int i) const noexcept -> PathSegment const* { return (i >= 0 && i < m_segmentCount) ? &m_path[i] : nullptr; }

	constexpr int GetSegmentCount() const noexcept { return m_segmentCount; }
	constexpr auto GetEndpoint() const noexcept -> const Vector& { return m_path[m_segmentCount - 1].pos; }

	constexpr auto Inspect() const noexcept -> std::span<PathSegment const> { return { &m_path.front(), (std::size_t)m_segmentCount}; }
	constexpr auto Inspect() noexcept -> std::span<PathSegment> { return { &m_path.front(), (std::size_t)m_segmentCount }; }

	// Return true if position is at the end of the path
	constexpr bool IsAtEnd(const Vector& pos) const noexcept
	{
		if (!IsValid())
			return false;

		constexpr float epsilon = 20.0f;
		return (pos - GetEndpoint()).LengthSquared() < (epsilon * epsilon);
	}

	// Return length of path from start to finish
	double GetLength() const noexcept
	{
		auto length = 0.0;
		for (int i = 1; i < GetSegmentCount(); i++)
		{
			length += (m_path[i].pos - m_path[i - 1].pos).Length();
		}

		return length;
	}

	// Return point a given distance along the path - if distance is out of path bounds, point is clamped to start/end
	// #PF_TODO: Be careful of returning "positions" along one-way drops, ladders, etc.
	bool GetPointAlongPath(float distAlong, Vector* pointOnPath) const noexcept
	{
		if (!IsValid() || !pointOnPath)
			return false;

		if (distAlong <= 0.0f)
		{
			*pointOnPath = m_path[0].pos;
			return true;
		}

		auto lengthSoFar = 0.0;
		for (int i = 1; i < GetSegmentCount(); i++)
		{
			auto dir = m_path[i].pos - m_path[i - 1].pos;
			auto segmentLength = dir.Length();

			if (segmentLength + lengthSoFar >= distAlong)
			{
				// desired point is on this segment of the path
				auto const delta = distAlong - lengthSoFar;
				auto const t = delta / segmentLength;

				*pointOnPath = m_path[i].pos + t * dir;

				return true;
			}

			lengthSoFar += segmentLength;
		}

		*pointOnPath = m_path[GetSegmentCount() - 1].pos;
		return true;
	}

	// Return the node index closest to the given distance along the path without going over - returns (-1) if error
	int GetSegmentIndexAlongPath(double distAlong) const noexcept
	{
		if (!IsValid())
			return -1;

		if (distAlong <= 0.0f)
		{
			return 0;
		}

		auto lengthSoFar = 0.0;
		for (int i = 1; i < GetSegmentCount(); i++)
		{
			lengthSoFar += (m_path[i].pos - m_path[i - 1].pos).Length();

			if (lengthSoFar > distAlong)
			{
				return i - 1;
			}
		}

		return GetSegmentCount() - 1;
	}

	constexpr bool IsValid() const noexcept { return (m_segmentCount > 0); }
	constexpr void Invalidate() noexcept { m_segmentCount = 0; }

	// Draw the path for debugging
	void Draw() const noexcept
	{
		if (!IsValid())
			return;

		for (int i = 1; i < m_segmentCount; i++)
		{
			auto const vecStart = m_path[i - 1].pos + Vector(0, 0, HalfHumanHeight);
			auto const vecEnd = m_path[i].pos + Vector(0, 0, HalfHumanHeight);

			UTIL_DrawBeamPoints(vecStart, vecEnd, 2, 255, 75, 0);
		}
	}

	// Compute closest point on path to given point
	// NOTE: This does not do line-of-sight tests, so closest point may be thru the floor, etc
	bool FindClosestPointOnPath(const Vector* worldPos, int startIndex, int endIndex, Vector* close) const noexcept
	{
		if (!IsValid() || !close)
			return false;

		decltype(worldPos->Length()) closeDistSq = 9999999999.9;

		for (int i = startIndex; i <= endIndex; i++)
		{
			auto from = &m_path[i - 1].pos;
			auto to = &m_path[i].pos;

			// compute ray along this path segment
			auto along = *to - *from;

			// make it a unit vector along the path
			auto length = along.Length();
			along = along.Normalize();

			// compute vector from start of segment to our point
			auto const toWorldPos = *worldPos - *from;

			// find distance of closest point on ray
			auto const closeLength = DotProduct(toWorldPos, along);

			// constrain point to be on path segment
			Vector pos{};
			if (closeLength <= 0.0f)
				pos = *from;
			else if (closeLength >= length)
				pos = *to;
			else
				pos = *from + closeLength * along;

			auto const distSq = (pos - *worldPos).LengthSquared();

			// keep the closest point so far
			if (distSq < closeDistSq)
			{
				closeDistSq = distSq;
				*close = pos;
			}
		}

		return true;
	}

	// Smooth out path, removing redundant nodes
	// DONT USE THIS: Optimizing the path results in cutting thru obstacles
	consteval void Optimize() const noexcept
	{
#if 0
		if (m_segmentCount < 3)
			return;

		int anchor = 0;
		while (anchor < m_segmentCount)
		{
			int occluded = FindNextOccludedNode(anchor);
			int nextAnchor = occluded - 1;

			if (nextAnchor > anchor)
			{
				// remove redundant nodes between anchor and nextAnchor
				int removeCount = nextAnchor - anchor - 1;
				if (removeCount > 0)
				{
					for (int i = nextAnchor; i < m_segmentCount; i++)
					{
						m_path[i - removeCount] = m_path[i];
					}
					m_segmentCount -= removeCount;
				}
			}

			anchor++;
		}
#endif
	}

	// Compute shortest path from 'start' to 'goal' via A* algorithm
	bool Compute(
		const Vector& start,
		const Vector& goal,
		std::move_only_function<float(CNavArea*, CNavArea*, const CNavLadder*) noexcept> costFunc
	) noexcept
	{
		Invalidate();

		auto const startArea = TheNavAreaGrid.GetNearestNavArea(start);
		if (!startArea)
			return false;

		auto const goalArea = TheNavAreaGrid.GetNavArea(goal);

		// if we are already in the goal area, build trivial path
		if (startArea == goalArea)
		{
			BuildTrivialPath(start, goal);
			return true;
		}

		// make sure path end position is on the ground
		Vector pathEndPosition = goal;

		if (goalArea)
			pathEndPosition.z = goalArea->GetZ(pathEndPosition);
		else
			GetGroundHeight(pathEndPosition, &pathEndPosition.z);

		// Compute shortest path to goal
		CNavArea* closestArea{};
		bool pathToGoalExists = NavAreaBuildPath(startArea, goalArea, goal, costFunc, &closestArea);

		auto const effectiveGoalArea = (pathToGoalExists) ? goalArea : closestArea;

		// Build path by following parent links
		int count = 0;
		CNavArea* area{ effectiveGoalArea };
		for (; area; area = area->GetParent())
			count++;

		// save room for endpoint
		if (count >= std::ssize(m_path))
			count = std::ssize(m_path) - 1;

		if (count == 0)
			return false;

		if (count == 1)
		{
			BuildTrivialPath(start, goal);
			return true;
		}

		m_segmentCount = count;
		for (area = effectiveGoalArea; count && area; area = area->GetParent())
		{
			--count;
			m_path[count].area = area;
			m_path[count].how = area->GetParentHow();
		}

		// compute path positions
		if (ComputePathPositions() == false)
		{
			Invalidate();
			return false;
		}

		// append path end position
		m_path[m_segmentCount].area = effectiveGoalArea;
		m_path[m_segmentCount].pos = pathEndPosition;
		m_path[m_segmentCount].ladder = nullptr;
		m_path[m_segmentCount].how = NUM_TRAVERSE_TYPES;
		++m_segmentCount;

		return true;
	}

private:
	std::array<PathSegment, 256> m_path{};
	int m_segmentCount{ 0 };

	// Determine actual path positions
	bool ComputePathPositions() noexcept
	{
		if (m_segmentCount == 0)
			return false;

		// start in first area's center
		m_path[0].pos = m_path[0].area->GetCenter();
		m_path[0].ladder = nullptr;
		m_path[0].how = NUM_TRAVERSE_TYPES;

		for (int i = 1; i < m_segmentCount; i++)
		{
			auto const from = &m_path[i - 1];
			auto const to = &m_path[i];

			// walk along the floor to the next area
			if (to->how <= GO_WEST)
			{
				to->ladder = nullptr;

				// compute next point, keeping path as straight as possible
				from->area->ComputeClosestPointInPortal(to->area, (NavDirType)to->how, &from->pos, &to->pos);

				// move goal position into the goal area a bit
				// how far to "step into" an area - must be less than min area size
				const float stepInDist = 5.0f;
				AddDirectionVector(&to->pos, (NavDirType)to->how, stepInDist);

				// we need to walk out of "from" area, so keep Z where we can reach it
				to->pos.z = from->area->GetZ(to->pos);

				// if this is a "jump down" connection, we must insert an additional point on the path
				if (to->area->IsConnected(from->area, NUM_DIRECTIONS) == false)
				{
					// this is a "jump down" link
					// compute direction of path just prior to "jump down"
					Vector2D dir{};
					DirectionToVector2D((NavDirType)to->how, &dir);

					// shift top of "jump down" out a bit to "get over the ledge"
					static constexpr float pushDist = 25.0f;
					to->pos.x += pushDist * dir.x;
					to->pos.y += pushDist * dir.y;

					// insert a duplicate node to represent the bottom of the fall
					if (m_segmentCount < std::ssize(m_path) - 1)
					{
						// copy nodes down
						for (int j = m_segmentCount; j > i; --j)
							m_path[j] = m_path[j - 1];

						// path is one node longer
						m_segmentCount++;

						// move index ahead into the new node we just duplicated
						++i;

						m_path[i].pos.x = to->pos.x + pushDist * dir.x;
						m_path[i].pos.y = to->pos.y + pushDist * dir.y;

						// put this one at the bottom of the fall
						m_path[i].pos.z = to->area->GetZ(m_path[i].pos);
					}
				}
			}
			// to get to next area, must go up a ladder
			else if (to->how == GO_LADDER_UP)
			{
				// find our ladder
				auto const list = from->area->GetLadderList(LADDER_UP);
				auto iter{ list->begin() };
				for (; iter != list->end(); ++iter)
				{
					auto const ladder = (*iter);

					// can't use "behind" area when ascending...
					if (ladder->m_topForwardArea == to->area || ladder->m_topLeftArea == to->area || ladder->m_topRightArea == to->area)
					{
						to->ladder = ladder;
						to->pos = ladder->m_bottom;
						AddDirectionVector(&to->pos, ladder->m_dir, 2.0f * HalfHumanWidth);
						break;
					}
				}

				if (iter == list->end())
				{
					//PrintIfWatched( "ERROR: Can't find ladder in path\n" );
					return false;
				}
			}
			// to get to next area, must go down a ladder
			else if (to->how == GO_LADDER_DOWN)
			{
				// find our ladder
				const NavLadderList* list = from->area->GetLadderList(LADDER_DOWN);
				NavLadderList::const_iterator iter;
				for (iter = list->begin(); iter != list->end(); iter++)
				{
					CNavLadder* ladder = (*iter);

					if (ladder->m_bottomArea == to->area)
					{
						to->ladder = ladder;
						to->pos = ladder->m_top;
						AddDirectionVector(&to->pos, Opposite[ladder->m_dir], 2.0f * HalfHumanWidth);
						break;
					}
				}
				if (iter == list->end())
				{
					//PrintIfWatched( "ERROR: Can't find ladder in path\n" );
					return false;
				}
			}
		}

		return true;
	}

	// Build trivial path when start and goal are in the same nav area
	bool BuildTrivialPath(const Vector& start, const Vector& goal) noexcept
	{
		m_segmentCount = 0;

		auto const startArea = TheNavAreaGrid.GetNearestNavArea(start);
		if (!startArea)
			return false;

		auto const goalArea = TheNavAreaGrid.GetNearestNavArea(goal);
		if (!goalArea)
			return false;

		m_segmentCount = 2;

		m_path[0].area = startArea;
		m_path[0].pos.x = start.x;
		m_path[0].pos.y = start.y;
		m_path[0].pos.z = startArea->GetZ(start);
		m_path[0].ladder = nullptr;
		m_path[0].how = NUM_TRAVERSE_TYPES;

		m_path[1].area = goalArea;
		m_path[1].pos.x = goal.x;
		m_path[1].pos.y = goal.y;
		m_path[1].pos.z = goalArea->GetZ(goal);
		m_path[1].ladder = nullptr;
		m_path[1].how = NUM_TRAVERSE_TYPES;

		return true;
	}

	// Check line of sight from 'anchor' node on path to subsequent nodes until
	// we find a node that can't been seen from 'anchor'
	// Used by Optimized()
	int FindNextOccludedNode(int anchor) const noexcept
	{
		for (int i = anchor + 1; i < m_segmentCount; i++)
		{
			// don't remove ladder nodes
			if (m_path[i].ladder)
				return i;

			if (!IsWalkableTraceLineClear(m_path[anchor].pos, m_path[i].pos))
			{
				// cant see this node from anchor node
				return i;
			}

			Vector anchorPlusHalf = m_path[anchor].pos + Vector(0, 0, HalfHumanHeight);
			Vector iPlusHalf = m_path[i].pos + Vector(0, 0, HalfHumanHeight);
			if (!IsWalkableTraceLineClear(anchorPlusHalf, iPlusHalf))
			{
				// cant see this node from anchor node
				return i;
			}

			Vector anchorPlusFull = m_path[anchor].pos + Vector(0, 0, HumanHeight);
			Vector iPlusFull = m_path[i].pos + Vector(0, 0, HumanHeight);
			if (!IsWalkableTraceLineClear(anchorPlusFull, iPlusFull))
			{
				// cant see this node from anchor node
				return i;
			}
		}

		return m_segmentCount;
	}
};











export enum GameEventType
{
	EVENT_INVALID = 0,
	EVENT_WEAPON_FIRED,					// tell bots the player is attack (argumens: 1 = attacker, 2 = NULL)
	EVENT_WEAPON_FIRED_ON_EMPTY,		// tell bots the player is attack without clip ammo (argumens: 1 = attacker, 2 = NULL)
	EVENT_WEAPON_RELOADED,				// tell bots the player is reloading his weapon (argumens: 1 = reloader, 2 = NULL)

	EVENT_HE_GRENADE_EXPLODED,			// tell bots the HE grenade is exploded (argumens: 1 = grenade thrower, 2 = NULL)
	EVENT_FLASHBANG_GRENADE_EXPLODED,	// tell bots the flashbang grenade is exploded (argumens: 1 = grenade thrower, 2 = explosion origin)
	EVENT_SMOKE_GRENADE_EXPLODED,		// tell bots the smoke grenade is exploded (argumens: 1 = grenade thrower, 2 = NULL)
	EVENT_GRENADE_BOUNCED,

	EVENT_BEING_SHOT_AT,
	EVENT_PLAYER_BLINDED_BY_FLASHBANG,	// tell bots the player is flashed (argumens: 1 = flashed player, 2 = NULL)
	EVENT_PLAYER_FOOTSTEP,				// tell bots the player is running (argumens: 1 = runner, 2 = NULL)
	EVENT_PLAYER_JUMPED,				// tell bots the player is jumped (argumens: 1 = jumper, 2 = NULL)
	EVENT_PLAYER_DIED,					// tell bots the player is killed (argumens: 1 = victim, 2 = killer)
	EVENT_PLAYER_LANDED_FROM_HEIGHT,	// tell bots the player is fell with some damage (argumens: 1 = felled player, 2 = NULL)
	EVENT_PLAYER_TOOK_DAMAGE,			// tell bots the player is take damage (argumens: 1 = victim, 2 = attacker)
	EVENT_HOSTAGE_DAMAGED,				// tell bots the player has injured a hostage (argumens: 1 = hostage, 2 = injurer)
	EVENT_HOSTAGE_KILLED,				// tell bots the player has killed a hostage (argumens: 1 = hostage, 2 = killer)

	EVENT_DOOR,							// tell bots the door is moving (argumens: 1 = door, 2 = NULL)
	EVENT_BREAK_GLASS,					// tell bots the glass has break (argumens: 1 = glass, 2 = NULL)
	EVENT_BREAK_WOOD,					// tell bots the wood has break (argumens: 1 = wood, 2 = NULL)
	EVENT_BREAK_METAL,					// tell bots the metal/computer has break (argumens: 1 = metal/computer, 2 = NULL)
	EVENT_BREAK_FLESH,					// tell bots the flesh has break (argumens: 1 = flesh, 2 = NULL)
	EVENT_BREAK_CONCRETE,				// tell bots the concrete has break (argumens: 1 = concrete, 2 = NULL)

	EVENT_BOMB_PLANTED,					// tell bots the bomb has been planted (argumens: 1 = planter, 2 = NULL)
	EVENT_BOMB_DROPPED,					// tell bots the bomb has been dropped (argumens: 1 = NULL, 2 = NULL)
	EVENT_BOMB_PICKED_UP,				// let the bots hear the bomb pickup (argumens: 1 = player that pickup c4, 2 = NULL)
	EVENT_BOMB_BEEP,					// let the bots hear the bomb beeping (argumens: 1 = c4, 2 = NULL)
	EVENT_BOMB_DEFUSING,				// tell the bots someone has started defusing (argumens: 1 = defuser, 2 = NULL)
	EVENT_BOMB_DEFUSE_ABORTED,			// tell the bots someone has aborted defusing (argumens: 1 = NULL, 2 = NULL)
	EVENT_BOMB_DEFUSED,					// tell the bots the bomb is defused (argumens: 1 = defuser, 2 = NULL)
	EVENT_BOMB_EXPLODED,				// let the bots hear the bomb exploding (argumens: 1 = NULL, 2 = NULL)

	EVENT_HOSTAGE_USED,					// tell bots the hostage is used (argumens: 1 = user, 2 = NULL)
	EVENT_HOSTAGE_RESCUED,				// tell bots the hostage is rescued (argumens: 1 = rescuer (CBasePlayer *), 2 = hostage (CHostage *))
	EVENT_ALL_HOSTAGES_RESCUED,			// tell bots the all hostages are rescued (argumens: 1 = NULL, 2 = NULL)

	EVENT_VIP_ESCAPED,					// tell bots the VIP is escaped (argumens: 1 = NULL, 2 = NULL)
	EVENT_VIP_ASSASSINATED,				// tell bots the VIP is assassinated (argumens: 1 = NULL, 2 = NULL)
	EVENT_TERRORISTS_WIN,				// tell bots the terrorists won the round (argumens: 1 = NULL, 2 = NULL)
	EVENT_CTS_WIN,						// tell bots the CTs won the round (argumens: 1 = NULL, 2 = NULL)
	EVENT_ROUND_DRAW,					// tell bots the round was a draw (argumens: 1 = NULL, 2 = NULL)
	EVENT_ROUND_WIN,					// tell carreer the round was a win (argumens: 1 = NULL, 2 = NULL)
	EVENT_ROUND_LOSS,					// tell carreer the round was a loss (argumens: 1 = NULL, 2 = NULL)
	EVENT_ROUND_START,					// tell bots the round was started (when freeze period is expired) (argumens: 1 = NULL, 2 = NULL)
	EVENT_PLAYER_SPAWNED,				// tell bots the player is spawned (argumens: 1 = spawned player, 2 = NULL)
	EVENT_CLIENT_CORPSE_SPAWNED,
	EVENT_BUY_TIME_START,
	EVENT_PLAYER_LEFT_BUY_ZONE,
	EVENT_DEATH_CAMERA_START,
	EVENT_KILL_ALL,
	EVENT_ROUND_TIME,
	EVENT_DIE,
	EVENT_KILL,
	EVENT_HEADSHOT,
	EVENT_KILL_FLASHBANGED,
	EVENT_TUTOR_BUY_MENU_OPENNED,
	EVENT_TUTOR_AUTOBUY,
	EVENT_PLAYER_BOUGHT_SOMETHING,
	EVENT_TUTOR_NOT_BUYING_ANYTHING,
	EVENT_TUTOR_NEED_TO_BUY_PRIMARY_WEAPON,
	EVENT_TUTOR_NEED_TO_BUY_PRIMARY_AMMO,
	EVENT_TUTOR_NEED_TO_BUY_SECONDARY_AMMO,
	EVENT_TUTOR_NEED_TO_BUY_ARMOR,
	EVENT_TUTOR_NEED_TO_BUY_DEFUSE_KIT,
	EVENT_TUTOR_NEED_TO_BUY_GRENADE,
	EVENT_CAREER_TASK_DONE,

	EVENT_START_RADIO_1,
	EVENT_RADIO_COVER_ME,
	EVENT_RADIO_YOU_TAKE_THE_POINT,
	EVENT_RADIO_HOLD_THIS_POSITION,
	EVENT_RADIO_REGROUP_TEAM,
	EVENT_RADIO_FOLLOW_ME,
	EVENT_RADIO_TAKING_FIRE,
	EVENT_START_RADIO_2,
	EVENT_RADIO_GO_GO_GO,
	EVENT_RADIO_TEAM_FALL_BACK,
	EVENT_RADIO_STICK_TOGETHER_TEAM,
	EVENT_RADIO_GET_IN_POSITION_AND_WAIT,
	EVENT_RADIO_STORM_THE_FRONT,
	EVENT_RADIO_REPORT_IN_TEAM,
	EVENT_START_RADIO_3,
	EVENT_RADIO_AFFIRMATIVE,
	EVENT_RADIO_ENEMY_SPOTTED,
	EVENT_RADIO_NEED_BACKUP,
	EVENT_RADIO_SECTOR_CLEAR,
	EVENT_RADIO_IN_POSITION,
	EVENT_RADIO_REPORTING_IN,
	EVENT_RADIO_GET_OUT_OF_THERE,
	EVENT_RADIO_NEGATIVE,
	EVENT_RADIO_ENEMY_DOWN,
	EVENT_END_RADIO,

	EVENT_NEW_MATCH,				// tell bots the game is new (argumens: 1 = NULL, 2 = NULL)
	EVENT_PLAYER_CHANGED_TEAM,		// tell bots the player is switch his team (also called from ClientPutInServer()) (argumens: 1 = switcher, 2 = NULL)
	EVENT_BULLET_IMPACT,			// tell bots the player is shoot at wall (argumens: 1 = shooter, 2 = shoot trace end position)
	EVENT_GAME_COMMENCE,			// tell bots the game is commencing (argumens: 1 = NULL, 2 = NULL)
	EVENT_WEAPON_ZOOMED,			// tell bots the player is switch weapon zoom (argumens: 1 = zoom switcher, 2 = NULL)
	EVENT_HOSTAGE_CALLED_FOR_HELP,	// tell bots the hostage is talking (argumens: 1 = listener, 2 = NULL)
	NUM_GAME_EVENTS,
};

// Improv-specific events
struct IImprovEvent
{
	// invoked when an improv reaches its MoveTo goal
	virtual void OnMoveToSuccess(const Vector& goal) noexcept = 0;

	enum MoveToFailureType
	{
		FAIL_INVALID_PATH = 0,
		FAIL_STUCK,
		FAIL_FELL_OFF,
	};

	// invoked when an improv fails to reach a MoveTo goal
	virtual void OnMoveToFailure(const Vector& goal, MoveToFailureType reason) noexcept = 0;

	// invoked when the improv is injured
	virtual void OnInjury(float amount) noexcept = 0;
};

// The Improv interface definition
// An "Improv" is an improvisational actor that simulates the
// behavor of a human in an unscripted, "make it up as you go" manner.
export struct CImprov : IImprovEvent
{
	virtual ~CImprov() noexcept = default;

	virtual bool IsAlive() const noexcept = 0;							// return true if this improv is alive

	virtual void MoveTo(const Vector& goal) noexcept = 0;				// move improv towards far-away goal (pathfind)
	virtual void LookAt(const Vector& target) noexcept = 0;				// define desired view target
	virtual void ClearLookAt() noexcept = 0;							// remove view goal
	virtual void FaceTo(const Vector& goal) noexcept = 0;				// orient body towards goal
	virtual void ClearFaceTo() noexcept = 0;							// remove body orientation goal

	virtual bool IsAtMoveGoal(float error = 20.0f) const noexcept = 0;								// return true if improv is standing on its movement goal
	virtual bool HasLookAt() const noexcept = 0;													// return true if improv has a look at goal
	virtual bool HasFaceTo() const noexcept = 0;													// return true if improv has a face to goal
	virtual bool IsAtFaceGoal() const noexcept = 0;													// return true if improv is facing towards its face goal
	virtual bool IsFriendInTheWay(const Vector& goalPos) const noexcept = 0;						// return true if a friend is blocking our line to the given goal position
	virtual bool IsFriendInTheWay(CBaseEntity* myFriend, const Vector& goalPos) const noexcept = 0;	// return true if the given friend is blocking our line to the given goal position

	virtual void MoveForward() noexcept = 0;
	virtual void MoveBackward() noexcept = 0;
	virtual void StrafeLeft() noexcept = 0;
	virtual void StrafeRight() noexcept = 0;
	virtual bool Jump() noexcept = 0;
	virtual void Crouch() noexcept = 0;
	virtual void StandUp() noexcept = 0;		// "un-crouch"

	virtual void TrackPath(const Vector& pathGoal, float deltaT) noexcept = 0;																						// move along path by following "pathGoal"
	virtual void StartLadder(const CNavLadder* ladder, enum NavTraverseType how, const Vector* approachPos, const Vector* departPos) noexcept = 0;					// invoked when a ladder is encountered while following a path

	virtual bool TraverseLadder(const CNavLadder* ladder, enum NavTraverseType how, const Vector* approachPos, const Vector* departPos, float deltaT) noexcept = 0;	// traverse given ladder
	virtual bool GetSimpleGroundHeightWithFloor(const Vector* pos, float* height, Vector* normal = nullptr) noexcept = 0;											// find "simple" ground height, treating current nav area as part of the floor

	virtual void Run() noexcept = 0;
	virtual void Walk() noexcept = 0;
	virtual void Stop() noexcept = 0;

	virtual float GetMoveAngle() const noexcept = 0;		// return direction of movement
	virtual float GetFaceAngle() const noexcept = 0;		// return direction of view

	virtual const Vector& GetFeet() const noexcept = 0;		// return position of "feet" - point below centroid of improv at feet level
	virtual const Vector& GetCentroid() const noexcept = 0;
	virtual const Vector& GetEyes() const noexcept = 0;

	virtual bool IsRunning() const noexcept = 0;
	virtual bool IsWalking() const noexcept = 0;
	virtual bool IsStopped() const noexcept = 0;

	virtual bool IsCrouching() const noexcept = 0;
	virtual bool IsJumping() const noexcept = 0;
	virtual bool IsUsingLadder() const noexcept = 0;
	virtual bool IsOnGround() const noexcept = 0;
	virtual bool IsMoving() const noexcept = 0;				// if true, improv is walking, crawling, running somewhere

	virtual bool CanRun() const noexcept = 0;
	virtual bool CanCrouch() const noexcept = 0;
	virtual bool CanJump() const noexcept = 0;
	virtual bool IsVisible(const Vector& pos, bool testFOV = false) const noexcept = 0;								// return true if improv can see position
	virtual bool IsPlayerLookingAtMe(CBasePlayer* pOther, float cosTolerance = 0.95f) const noexcept = 0;			// return true if 'other' is looking right at me
	virtual CBasePlayer* IsAnyPlayerLookingAtMe(int team = 0, float cosTolerance = 0.95f) const noexcept = 0;		// return player on given team that is looking right at me (team == 0 means any team), NULL otherwise

	virtual CBasePlayer* GetClosestPlayerByTravelDistance(int team = 0, float* range = nullptr) const noexcept = 0;	// return actual travel distance to closest player on given team (team == 0 means any team)
	virtual CNavArea* GetLastKnownArea() const noexcept = 0;

	virtual void OnUpdate(float deltaT) noexcept = 0;	// a less frequent, full update 'tick'
	virtual void OnUpkeep(float deltaT) noexcept = 0;	// a frequent, lightweight update 'tick'
	virtual void OnReset() noexcept = 0;				// reset improv to initial state
	virtual void OnGameEvent(GameEventType event, CBaseEntity* pEntity, CBaseEntity* pOther) noexcept = 0;	// invoked when an event occurs in the game
	virtual void OnTouch(CBaseEntity* pOther) noexcept = 0;													// "other" has touched us
};











// Monitor improv movement and determine if it becomes stuck
export struct CStuckMonitor
{
	constexpr void Reset() noexcept
	{
		m_isStuck = false;
		m_avgVelIndex = 0;
		m_avgVelCount = 0;
	}

	// Test if the improv has become stuck
	void Update(Vector const& vecCenter, bool bUsingLadder) noexcept
	{
		if (m_isStuck)
		{
			// improv is stuck - see if it has moved far enough to be considered unstuck
			constexpr float unstuckRange = 75.0f;
			if ((vecCenter - m_stuckSpot).LengthSquared() > (unstuckRange * unstuckRange))
			{
				// no longer stuck
				Reset();
				//PrintIfWatched( "UN-STUCK\n" );
			}
		}
		else
		{
			// check if improv has become stuck

			// compute average velocity over a short period (for stuck check)
			auto vel = vecCenter - m_lastCentroid;

			// if we are jumping, ignore Z
			//if (improv->IsJumping())
			//	vel.z = 0.0f;

			// ignore Z unless we are on a ladder (which is only Z)
			if (!bUsingLadder)
				vel.z = 0.0f;

			// cannot be Length2D, or will break ladder movement (they are only Z)
			auto const moveDist = (float)vel.Length();

			auto const deltaT = gpGlobals->time - m_lastTime;
			if (deltaT <= 0.0f)
				return;

			m_lastTime = gpGlobals->time;

			// compute current velocity
			m_avgVel[m_avgVelIndex++] = moveDist / deltaT;

			if (m_avgVelIndex == m_avgVel.size())
				m_avgVelIndex = 0;

			if (m_avgVelCount < m_avgVel.size())
			{
				m_avgVelCount++;
			}
			else
			{
				// we have enough samples to know if we're stuck
				auto const avgVel =
					std::ranges::fold_left(m_avgVel, 0.f, std::plus<>{}) / (float)m_avgVelCount;

				// cannot make this velocity too high, or actors will get "stuck" when going down ladders
				auto const stuckVel = bUsingLadder ? 10.0f : 20.0f;

				if (avgVel < stuckVel)
				{
					// note when and where we initially become stuck
					m_stuckTimer.Start();
					m_stuckSpot = vecCenter;
					m_isStuck = true;
				}
			}
		}

		// always need to track this
		m_lastCentroid = vecCenter;
	}

	constexpr bool IsStuck()	const noexcept { return m_isStuck; }
	float GetDuration()			const noexcept { return m_isStuck ? m_stuckTimer.GetElapsedTime() : 0.0f; }

private:
	bool m_isStuck{};				// if true, we are stuck
	Vector m_stuckSpot{};			// the location where we became stuck
	IntervalTimer m_stuckTimer{};	// how long we have been stuck

	std::array<float, 5> m_avgVel{};
	std::size_t m_avgVelIndex{};
	std::size_t m_avgVelCount{};
	Vector m_lastCentroid{};
	float m_lastTime{};
};

// The CNavPathFollower class implements path following behavior
export struct CNavPathFollower
{
	void SetImprov(CImprov* improv) noexcept { m_improv = improv; }
	void SetPath(CNavPath* path) noexcept { m_path = path; }
	void Reset() noexcept
	{
		m_segmentIndex = 1;
		m_isLadderStarted = false;

		m_stuckMonitor.Reset();
	}

	// Move improv along path
	void Update(float deltaT, bool avoidObstacles) noexcept
	{
		if (!m_path || m_path->IsValid() == false)
			return;

		auto const node = (*m_path)[m_segmentIndex];

		if (!node)
		{
			m_improv->OnMoveToFailure(m_path->GetEndpoint(), IImprovEvent::FAIL_INVALID_PATH);
			m_path->Invalidate();
			return;
		}

		// handle ladders
		if (node->ladder)
		{
			const Vector* approachPos = nullptr;
			const Vector* departPos = nullptr;

			if (m_segmentIndex)
				approachPos = &(*m_path)[m_segmentIndex - 1]->pos;

			if (m_segmentIndex < m_path->GetSegmentCount() - 1)
				departPos = &(*m_path)[m_segmentIndex + 1]->pos;

			if (!m_isLadderStarted)
			{
				// set up ladder movement
				m_improv->StartLadder(node->ladder, node->how, approachPos, departPos);
				m_isLadderStarted = true;
			}

			// move improv along ladder
			if (m_improv->TraverseLadder(node->ladder, node->how, approachPos, departPos, deltaT))
			{
				// completed ladder
				m_segmentIndex++;
			}

			return;
		}

		// reset ladder init flag
		m_isLadderStarted = false;

		// Check if we reached the end of the path
		static constexpr float closeRange = 20.0f;
		if ((m_improv->GetFeet() - node->pos).LengthSquared() < (closeRange * closeRange))
		{
			++m_segmentIndex;

			if (m_segmentIndex >= m_path->GetSegmentCount())
			{
				m_improv->OnMoveToSuccess(m_path->GetEndpoint());
				m_path->Invalidate();
				return;
			}
		}

		m_goal = node->pos;

		static constexpr float aheadRange = 300.0f;
		m_segmentIndex = FindPathPoint(aheadRange, &m_goal, &m_behindIndex);
		if (m_segmentIndex >= m_path->GetSegmentCount())
			m_segmentIndex = m_path->GetSegmentCount() - 1;

		bool isApproachingJumpArea = false;

		// Crouching
		if (!m_improv->IsUsingLadder())
		{
			// because hostage crouching is not really supported by the engine,
			// if we are standing in a crouch area, we must crouch to avoid collisions
			if (m_improv->GetLastKnownArea() && (m_improv->GetLastKnownArea()->GetAttributes() & NAV_CROUCH) && !(m_improv->GetLastKnownArea()->GetAttributes() & NAV_JUMP))
			{
				m_improv->Crouch();
			}

			// if we are approaching a crouch area, crouch
			// if there are no crouch areas coming up, stand
			static constexpr float crouchRange = 50.0f;
			bool didCrouch = false;
			for (int i = m_segmentIndex; i < m_path->GetSegmentCount(); i++)
			{
				auto const to = (*m_path)[i]->area;

				// if there is a jump area on the way to the crouch area, don't crouch as it messes up the jump
				if (to->GetAttributes() & NAV_JUMP)
				{
					isApproachingJumpArea = true;
					break;
				}

				Vector close{};
				to->GetClosestPointOnArea(m_improv->GetCentroid(), &close);

				if ((close - m_improv->GetFeet()).Make2D().LengthSquared() > (crouchRange * crouchRange))
					break;

				if (to->GetAttributes() & NAV_CROUCH)
				{
					m_improv->Crouch();
					didCrouch = true;
					break;
				}
			}

			if (!didCrouch && !m_improv->IsJumping())
			{
				// no crouch areas coming up
				m_improv->StandUp();
			}
		}
		// end crouching logic

		if (m_isDebug)
		{
			m_path->Draw();
			UTIL_DrawBeamPoints(m_improv->GetCentroid(), m_goal + Vector(0, 0, StepHeight), 1, 255, 0, 255);
			UTIL_DrawBeamPoints(m_goal + Vector(0, 0, StepHeight), m_improv->GetCentroid(), 1, 255, 0, 255);
		}

		// check if improv becomes stuck
		m_stuckMonitor.Update(m_improv->GetCentroid(), m_improv->IsUsingLadder());

		// if improv has been stuck for too long, give up
		static constexpr float giveUpTime = 2.0f;
		if (m_stuckMonitor.GetDuration() > giveUpTime)
		{
			m_improv->OnMoveToFailure(m_path->GetEndpoint(), IImprovEvent::FAIL_STUCK);
			m_path->Invalidate();
			return;
		}

		// if our goal is high above us, we must have fallen
		if (m_goal.z - m_improv->GetFeet().z > JumpCrouchHeight)
		{
			static constexpr float closeRange = 75.0f;
			Vector2D const to(m_improv->GetFeet().x - m_goal.x, m_improv->GetFeet().y - m_goal.y);

			if (to.LengthSquared() < (closeRange * closeRange))
			{
				// we can't reach the goal position
				// check if we can reach the next node, in case this was a "jump down" situation
				auto const nextNode = (*m_path)[m_behindIndex + 1];
				if (m_behindIndex >= 0 && nextNode)
				{
					if (nextNode->pos.z - m_improv->GetFeet().z > JumpCrouchHeight)
					{
						// the next node is too high, too - we really did fall of the path
						m_improv->OnMoveToFailure(m_path->GetEndpoint(), IImprovEvent::FAIL_FELL_OFF);
						m_path->Invalidate();
						return;
					}
				}
				else
				{
					// fell trying to get to the last node in the path
					m_improv->OnMoveToFailure(m_path->GetEndpoint(), IImprovEvent::FAIL_FELL_OFF);
					m_path->Invalidate();
					return;
				}
			}
		}

		// avoid small obstacles
		if (avoidObstacles && !isApproachingJumpArea && !m_improv->IsJumping() && m_segmentIndex < m_path->GetSegmentCount() - 1)
		{
			FeelerReflexAdjustment(&m_goal);

			// currently, this is only used for hostages, and their collision physics stinks
			// do more feeler checks to avoid short obstacles
			static constexpr float inc = 0.25f;
			for (float t = 0.5f; t < 1.0f; t += inc)
			{
				FeelerReflexAdjustment(&m_goal, t * StepHeight);
			}
		}

		// move improv along path
		m_improv->TrackPath(m_goal, deltaT);
	}
	constexpr void Debug(bool status) noexcept { m_isDebug = status; }					// turn debugging on/off

	constexpr bool IsStuck() const noexcept { return m_stuckMonitor.IsStuck(); }		// return true if improv is stuck
	constexpr void ResetStuck() noexcept { m_stuckMonitor.Reset(); }
	float GetStuckDuration() const noexcept { return m_stuckMonitor.GetDuration(); }	// return how long we've been stuck

	// Do reflex avoidance movements if our "feelers" are touched
	// TODO: Parameterize feeler spacing
	void FeelerReflexAdjustment(Vector* goalPosition, float height = -1.f) const noexcept
	{
		// if we are in a "precise" area, do not do feeler adjustments
		if (m_improv->GetLastKnownArea() && (m_improv->GetLastKnownArea()->GetAttributes() & NAV_PRECISE))
			return;

		auto dir = Vector{
			BotCOS(m_improv->GetMoveAngle()), BotSIN(m_improv->GetMoveAngle()), 0.0f
		}.Normalize();

		Vector lat{ -dir.y, dir.x, 0.0f };

		const float feelerOffset = (m_improv->IsCrouching()) ? 20.0f : 25.0f;	// 15, 20
		static constexpr float feelerLengthRun = 50.0f;	// 100 - too long for tight hallways (cs_747)
		static constexpr float feelerLengthWalk = 30.0f;

		// if obstacle is lower than StepHeight, we'll walk right over it
		const float feelerHeight = (height > 0.0f) ? height : StepHeight + 0.1f;
		float feelerLength = (m_improv->IsRunning()) ? feelerLengthRun : feelerLengthWalk;

		feelerLength = (m_improv->IsCrouching()) ? 20.0f : feelerLength;

		// Feelers must follow floor slope
		float ground{};
		Vector normal{};
		if (m_improv->GetSimpleGroundHeightWithFloor(&m_improv->GetEyes(), &ground, &normal) == false)
			return;

		// get forward vector along floor
		dir = CrossProduct(lat, normal);

		// correct the sideways vector
		lat = CrossProduct(dir, normal);

		Vector feet = m_improv->GetFeet();
		feet.z += feelerHeight;

		auto from = feet + feelerOffset * lat;
		auto to = from + feelerLength * dir;

		auto const leftClear = IsWalkableTraceLineClear(from, to, WALK_THRU_DOORS | WALK_THRU_BREAKABLES);

		// draw debug beams
		if (m_isDebug)
		{
			if (leftClear)
				UTIL_DrawBeamPoints(from, to, 1, 0, 255, 0);
			else
				UTIL_DrawBeamPoints(from, to, 1, 255, 0, 0);
		}

		from = feet - feelerOffset * lat;
		to = from + feelerLength * dir;

		bool rightClear = IsWalkableTraceLineClear(from, to, WALK_THRU_DOORS | WALK_THRU_BREAKABLES);

		// draw debug beams
		if (m_isDebug)
		{
			if (rightClear)
				UTIL_DrawBeamPoints(from, to, 1, 0, 255, 0);
			else
				UTIL_DrawBeamPoints(from, to, 1, 255, 0, 0);
		}

		auto const avoidRange = (m_improv->IsCrouching()) ? 150.0f : 300.0f;

		if (!rightClear)
		{
			if (leftClear)
			{
				// right hit, left clear - veer left
				*goalPosition += avoidRange * lat;
				//*goalPosition = m_improv->GetFeet() + avoidRange * lat;
				//m_improv->StrafeLeft();
			}
		}
		else if (!leftClear)
		{
			// right clear, left hit - veer right
			*goalPosition -= avoidRange * lat;
			//*goalPosition = m_improv->GetFeet() - avoidRange * lat;
			//m_improv->StrafeRight();
		}
	}

private:
	// Return the closest point to our current position on our current path
	// If "local" is true, only check the portion of the path surrounding m_pathIndex
	int FindOurPositionOnPath(Vector* close, bool local) const noexcept
	{
		auto const& feet = m_improv->GetFeet();
		auto const& eyes = m_improv->GetEyes();
		auto closeDistSq = 1.0e10;
		int closeIndex = -1;
		int start{}, end{};

		if (!m_path->IsValid())
			return -1;

		if (local)
		{
			start = m_segmentIndex - 3;
			if (start < 1)
				start = 1;

			end = m_segmentIndex + 3;
			if (end > m_path->GetSegmentCount())
				end = m_path->GetSegmentCount();
		}
		else
		{
			start = 1;
			end = m_path->GetSegmentCount();
		}

		for (auto i = start; i < end; i++)
		{
			auto const from = &(*m_path)[i - 1]->pos;
			auto const to = &(*m_path)[i]->pos;

			// compute ray along this path segment
			auto along = *to - *from;

			// make it a unit vector along the path
			auto const length = along.Length();
			along = along.Normalize();

			// compute vector from start of segment to our point
			auto const toFeet = feet - *from;

			// find distance of closest point on ray
			auto const closeLength = DotProduct(toFeet, along);

			Vector pos{};
			// constrain point to be on path segment
			if (closeLength <= 0.0f)
				pos = *from;
			else if (closeLength >= length)
				pos = *to;
			else
				pos = *from + closeLength * along;

			auto const distSq = (pos - feet).LengthSquared();

			// keep the closest point so far
			if (distSq < closeDistSq)
			{
				// don't use points we cant see
				auto const probe = pos + Vector(0, 0, HalfHumanHeight);
				if (!IsWalkableTraceLineClear(eyes, probe, WALK_THRU_DOORS | WALK_THRU_BREAKABLES))
					continue;

				// don't use points we cant reach
				//if (!IsStraightLinePathWalkable(&pos))
				//	continue;

				closeDistSq = distSq;
				if (close)
					*close = pos;
				closeIndex = i - 1;
			}
		}

		return closeIndex;
	}

	// Compute a point a fixed distance ahead along our path
	// Returns path index just after point
	int FindPathPoint(float aheadRange, Vector* point, int* prevIndex) const
	{
		// find path index just past aheadRange
		int afterIndex{};

		// finds the closest point on local area of path, and returns the path index just prior to it
		Vector close{};
		int startIndex = FindOurPositionOnPath(&close, true);

		if (prevIndex)
			*prevIndex = startIndex;

		if (startIndex <= 0)
		{
			// went off the end of the path
			// or next point in path is unwalkable (ie: jump-down)
			// keep same point
			return m_segmentIndex;
		}

		// if we are crouching, just follow the path exactly
		if (m_improv->IsCrouching())
		{
			// we want to move to the immediately next point along the path from where we are now
			int index = startIndex + 1;
			if (index >= m_path->GetSegmentCount())
				index = m_path->GetSegmentCount() - 1;

			*point = (*m_path)[index]->pos;

			// if we are very close to the next point in the path, skip ahead to the next one to avoid wiggling
			// we must do a 2D check here, in case the goal point is floating in space due to jump down, etc
			static constexpr float closeEpsilon = 20.0f;
			while ((*point - close).Make2D().LengthSquared() < (closeEpsilon * closeEpsilon))
			{
				index++;

				if (index >= m_path->GetSegmentCount())
				{
					index = m_path->GetSegmentCount() - 1;
					break;
				}

				*point = (*m_path)[index]->pos;
			}

			return index;
		}

		// make sure we use a node a minimum distance ahead of us, to avoid wiggling
		while (startIndex < m_path->GetSegmentCount() - 1)
		{
			Vector pos = (*m_path)[startIndex + 1]->pos;

			// we must do a 2D check here, in case the goal point is floating in space due to jump down, etc
			static constexpr float closeEpsilon = 20.0f;
			if ((pos - close).Make2D().LengthSquared() < (closeEpsilon * closeEpsilon))
			{
				++startIndex;
			}
			else
			{
				break;
			}
		}

		// if we hit a ladder or jump area, must stop (dont use ladder behind us)
		if (startIndex > m_segmentIndex
			&& startIndex < m_path->GetSegmentCount()
			&& ((*m_path)[startIndex]->ladder || ((*m_path)[startIndex]->area->GetAttributes() & NAV_JUMP))
			)
		{
			*point = (*m_path)[startIndex]->pos;
			return startIndex;
		}

		// we need the point just *ahead* of us
		if (++startIndex >= m_path->GetSegmentCount())
			startIndex = m_path->GetSegmentCount() - 1;

		// if we hit a ladder or jump area, must stop
		if (startIndex < m_path->GetSegmentCount()
			&& ((*m_path)[startIndex]->ladder || ((*m_path)[startIndex]->area->GetAttributes() & NAV_JUMP))
			)
		{
			*point = (*m_path)[startIndex]->pos;
			return startIndex;
		}

		// note direction of path segment we are standing on
		auto const initDir =
			((*m_path)[startIndex]->pos - (*m_path)[startIndex - 1]->pos).Normalize();

		auto const& feet = m_improv->GetFeet();
		auto const& eyes = m_improv->GetEyes();
		double rangeSoFar = 0;

		// this flag is true if our ahead point is visible
		auto visible = true;

		auto prevDir = initDir;

		// step along the path until we pass aheadRange
		bool isCorner = false;
		int i{ startIndex };
		for (; i < m_path->GetSegmentCount(); ++i)
		{
			auto const pos = (*m_path)[i]->pos;
			auto const to = pos - (*m_path)[i - 1]->pos;
			auto const dir = to.Normalize();

			// don't allow path to double-back from our starting direction (going upstairs, down curved passages, etc)
			if (DotProduct(dir, initDir) < 0.0) // -0.25
			{
				--i;
				break;
			}

			// if the path turns a corner, we want to move towards the corner, not into the wall/stairs/etc
			if (DotProduct(dir, prevDir) < 0.5)
			{
				isCorner = true;
				--i;
				break;
			}
			prevDir = dir;

			// don't use points we cant see
			auto const probe = pos + Vector(0, 0, HalfHumanHeight);
			if (!IsWalkableTraceLineClear(eyes, probe, WALK_THRU_BREAKABLES))
			{
				// presumably, the previous point is visible, so we will interpolate
				visible = false;
				break;
			}

			// if we encounter a ladder or jump area, we must stop
			if (i < m_path->GetSegmentCount()
				&& ((*m_path)[i]->ladder || (*m_path)[i]->area->GetAttributes() & NAV_JUMP))
			{
				break;
			}

			// Check straight-line path from our current position to this position
			// Test for un-jumpable height change, or unrecoverable fall
			//if (!IsStraightLinePathWalkable(&pos))
			//{
			//	--i;
			//	break;
			//}

			auto const along = (i == startIndex) ? (pos - feet) : (pos - (*m_path)[i - 1]->pos);
			rangeSoFar += along.Length2D();

			// stop if we have gone farther than aheadRange
			if (rangeSoFar >= aheadRange)
				break;
		}

		if (i < startIndex)
			afterIndex = startIndex;
		else if (i < m_path->GetSegmentCount())
			afterIndex = i;
		else
			afterIndex = m_path->GetSegmentCount() - 1;

		// compute point on the path at aheadRange
		if (afterIndex == 0)
		{
			*point = (*m_path)[0]->pos;
		}
		else
		{
			// interpolate point along path segment
			auto const afterPoint = &(*m_path)[afterIndex]->pos;
			auto const beforePoint = &(*m_path)[afterIndex - 1]->pos;

			auto const to = *afterPoint - *beforePoint;
			auto const length = to.Length2D();

			auto t = 1.0f - ((rangeSoFar - aheadRange) / length);

			if (t < 0.0)
				t = 0.0;
			else if (t > 1.0)
				t = 1.0;

			*point = *beforePoint + t * to;

			// if afterPoint wasn't visible, slide point backwards towards beforePoint until it is
			if (!visible)
			{
				static constexpr auto sightStepSize = 25.0;
				auto const dt = sightStepSize / length;

				auto const probe = *point + Vector(0, 0, HalfHumanHeight);
				while (t > 0.0f && !IsWalkableTraceLineClear(eyes, probe, WALK_THRU_BREAKABLES))
				{
					t -= dt;
					*point = *beforePoint + t * to;
				}

				if (t <= 0.0f)
					*point = *beforePoint;
			}
		}

		// if position found is too close to us, or behind us, force it farther down the path so we don't stop and wiggle
		if (!isCorner)
		{
			static constexpr float epsilon = 50.0f;
			auto const centroid{ m_improv->GetCentroid().Make2D() };
			auto toPoint{ point->Make2D() - centroid };

			if (DotProduct(toPoint, initDir.Make2D()) < 0.0f || toPoint.LengthSquared() < (epsilon * epsilon))
			{
				int i{ startIndex };
				for (; i < m_path->GetSegmentCount(); i++)
				{
					toPoint.x = (*m_path)[i]->pos.x - centroid.x;
					toPoint.y = (*m_path)[i]->pos.y - centroid.y;

					if ((*m_path)[i]->ladder || ((*m_path)[i]->area->GetAttributes() & NAV_JUMP) || toPoint.LengthSquared() > (epsilon * epsilon))
					{
						*point = (*m_path)[i]->pos;
						startIndex = i;
						break;
					}
				}

				if (i == m_path->GetSegmentCount())
				{
					*point = m_path->GetEndpoint();
					startIndex = m_path->GetSegmentCount() - 1;
				}
			}
		}

		// m_pathIndex should always be the next point on the path, even if we're not moving directly towards it
		if (startIndex < m_path->GetSegmentCount())
			return startIndex;

		return m_path->GetSegmentCount() - 1;
	}

	CImprov* m_improv{};		// who is doing the path following
	CNavPath* m_path{};			// the path being followed
	int m_segmentIndex{};		// the point on the path the improv is moving towards
	int m_behindIndex{};		// index of the node on the path just behind us
	Vector m_goal{};			// last computed follow goal
	bool m_isLadderStarted{};
	bool m_isDebug{};
	CStuckMonitor m_stuckMonitor{};
};
