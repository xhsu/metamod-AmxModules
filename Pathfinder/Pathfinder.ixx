/*
* Day-Night Survival Dev Team
* File Creation: 10 Oct 2024
*
* Programmer: Luna the Reborn
* Consultant: Crsky
*/

module;

#include <assert.h>

export module Pathfinder;

import std;
import hlsdk;

import CBase;
import Nav;

import UtlRandom;



// Find path from startArea to goalArea via an A* search, using supplied cost heuristic.
// If cost functor returns -1 for an area, that area is considered a dead end.
// This doesn't actually build a path, but the path is defined by following parent
// pointers back from goalArea to startArea.
// If 'closestArea' is non-NULL, the closest area to the goal is returned (useful if the path fails).
// If 'goalArea' is NULL, will compute a path as close as possible to 'goalPos'.
// If 'goalPos' is NULL, will use the center of 'goalArea' as the goal position.
// Returns true if a path exists.
template <typename CostFunctor>
bool NavAreaBuildPath(CNavArea* startArea, CNavArea* goalArea, const Vector* goalPos, CostFunctor& costFunc, CNavArea** closestArea = nullptr) noexcept
{
	if (closestArea)
		*closestArea = nullptr;

	if (!startArea)
		return false;

	// If goalArea is NULL, this function will return the closest area to the goal.
	// However, if there is also no goal, we can't do anything.
	if (!goalArea && !goalPos)
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
	Vector actualGoalPos = (goalPos != nullptr) ? (*goalPos) : (*goalArea->GetCenter());

	// start search
	CNavArea::ClearSearchLists();

	// compute estimate of path length
	// TODO: Cost might work as "manhattan distance"
	startArea->SetTotalCost((float)(*startArea->GetCenter() - actualGoalPos).Length());

	auto initCost = costFunc(startArea, nullptr, nullptr);
	if (initCost < 0.0f)
		return false;

	startArea->SetCostSoFar(initCost);
	startArea->AddToOpenList();

	// keep track of the area we visit that is closest to the goal
	if (closestArea)
		*closestArea = startArea;

	float closestAreaDist = startArea->GetTotalCost();

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
		int dir = NORTH;
		const NavConnectList* floorList = area->GetAdjacentList(NORTH);
		auto floorIter = floorList->begin();

		bool ladderUp = true;
		const NavLadderList* ladderList = nullptr;
		NavLadderList::const_iterator ladderIter;
		enum
		{
			AHEAD = 0,
			LEFT,
			RIGHT,
			BEHIND,
			NUM_TOP_DIRECTIONS
		};

		int ladderTopDir;
		while (true)
		{
			CNavArea* newArea = nullptr;
			NavTraverseType how;
			const CNavLadder* ladder = nullptr;

			// Get next adjacent area - either on floor or via ladder
			if (searchFloor)
			{
				// if exhausted adjacent connections in current direction, begin checking next direction
				if (floorIter == floorList->end())
				{
					dir++;

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
				floorIter++;

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

			auto newCostSoFar = costFunc(newArea, area, ladder);

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
				float const newCostRemaining = (float)(*newArea->GetCenter() - actualGoalPos).Length();

				// track closest area to goal in case path fails
				if (closestArea && newCostRemaining < closestAreaDist)
				{
					*closestArea = newArea;
					closestAreaDist = newCostRemaining;
				}

				newArea->SetParent(area, how);
				newArea->SetCostSoFar(newCostSoFar);
				newArea->SetTotalCost(newCostSoFar + newCostRemaining);

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


// Functor used with NavAreaBuildPath()
struct PathCost
{
	static float GetApproximateFallDamage(float height) noexcept
	{
		// empirically discovered height values
		constexpr float slope = 0.2178f;
		constexpr float intercept = 26.0f;

		auto const damage = slope * height - intercept;

		if (damage < 0.0f)
			return 0.0f;

		return damage;
	}

	float operator()(CNavArea* area, CNavArea* fromArea, const CNavLadder* ladder, int iTeam = 1, float flHealth = 9999.f, float flAggression = 75.f) const noexcept
	{
		const float baseDangerFactor = 100.0f;

		// respond to the danger modulated by our aggression (even super-aggressives pay SOME attention to danger)
		float dangerFactor = (1.0f - (0.95f * flAggression)) * baseDangerFactor;

		if (fromArea == nullptr)
		{
			if (m_route == FASTEST_ROUTE)
				return 0.0f;

			// first area in path, cost is just danger
			return dangerFactor * area->GetDanger(iTeam - 1);
		}
		else if ((fromArea->GetAttributes() & NAV_JUMP) && (area->GetAttributes() & NAV_JUMP))
		{
			// cannot actually walk in jump areas - disallow moving from jump area to jump area
			return -1.0f;
		}
		else
		{
			// compute distance from previous area to this area
			float dist;
			if (ladder)
			{
				// ladders are slow to use
				const float ladderPenalty = 1.0f;
				dist = ladderPenalty * ladder->m_length;

				// if we are currently escorting hostages, avoid ladders (hostages are confused by them)
				//if (m_bot->GetHostageEscortCount())
				//	dist *= 100.0f;
			}
			else
			{
				dist = (float)(*area->GetCenter() - *fromArea->GetCenter()).Length();
			}

			// compute distance travelled along path so far
			float cost = dist + fromArea->GetCostSoFar();

#ifdef CSBOT_ZOMBIE
			// zombies ignore all path penalties
			if (cv_bot_zombie.value > 0.0f)
				return cost;
#endif
			// add cost of "jump down" pain unless we're jumping into water
			if (!area->IsConnected(fromArea, NUM_DIRECTIONS))
			{
				// this is a "jump down" (one way drop) transition - estimate damage we will take to traverse it
				float fallDistance = -fromArea->ComputeHeightChange(area);

				// if it's a drop-down ladder, estimate height from the bottom of the ladder to the lower area
				//if (ladder && ladder->m_bottom.z < fromArea->GetCenter()->z && ladder->m_bottom.z > area->GetCenter()->z)
				//{
				//	fallDistance = ladder->m_bottom.z - area->GetCenter()->z;
				//}

				float fallDamage = GetApproximateFallDamage(fallDistance);

				if (fallDamage > 0.0f)
				{
					// if the fall would kill us, don't use it
					const float deathFallMargin = 10.0f;
					if (fallDamage + deathFallMargin >= flHealth)
						return -1.0f;

					// if we need to get there in a hurry, ignore minor pain
					const float painTolerance = 15.0f * flAggression + 10.0f;
					if (m_route != FASTEST_ROUTE || fallDamage > painTolerance)
					{
						// cost is proportional to how much it hurts when we fall
						// 10 points - not a big deal, 50 points - ouch!
						cost += 100.0f * fallDamage * fallDamage;
					}
				}
			}

			// if this is a "crouch" area, add penalty
			if (area->GetAttributes() & NAV_CROUCH)
			{
				// these areas are very slow to move through
				auto crouchPenalty = (m_route == FASTEST_ROUTE) ? 20.0f : 5.0f;

#ifdef CSBOT_HOSTAGE
				// avoid crouch areas if we are rescuing hostages
				if (m_bot->GetHostageEscortCount())
				{
					crouchPenalty *= 3.0f;
				}
#endif
				cost += crouchPenalty * dist;
			}

			// if this is a "jump" area, add penalty
			if (area->GetAttributes() & NAV_JUMP)
			{
				// jumping can slow you down
				//const float jumpPenalty = (m_route == FASTEST_ROUTE) ? 100.0f : 0.5f;
				constexpr float jumpPenalty = 2.0f;
				cost += jumpPenalty * dist;
			}

			if (m_route == SAFEST_ROUTE)
			{
				// add in the danger of this path - danger is per unit length travelled
				cost += dist * dangerFactor * area->GetDanger(iTeam - 1);
			}

#ifdef CSBOT_ATTACKING
			if (!m_bot->IsAttacking())
			{
				// add in cost of teammates in the way
				// approximate density of teammates based on area
				float size = (area->GetSizeX() + area->GetSizeY()) / 2.0f;

				// degenerate check
				if (size >= 1.0f)
				{
					// cost is proportional to the density of teammates in this area
					constexpr float costPerFriendPerUnit = 50000.0f;
					cost += costPerFriendPerUnit * float(area->GetPlayerCount(m_bot->m_iTeam, m_bot)) / size;
				}
			}
#endif
			return cost;
		}

		return 0.0f;
	}

	//	CCSBot* m_bot;
	RouteType m_route{ SAFEST_ROUTE };
};

export struct Pathfinder
{
	// Compute shortest path to goal position via A* algorithm
	// If 'goalArea' is NULL, path will get as close as it can.
	bool ComputePath(CNavArea* goalArea, const Vector* goal, RouteType route) noexcept
	{
		// Throttle re-pathing
//		if (!m_repathTimer.IsElapsed())
//			return false;

		// randomize to distribute CPU load
//		m_repathTimer.Start(UTIL_Random(0.4f, 0.6f));

		DestroyPath();

		CNavArea* startArea = m_lastKnownArea;
		if (!startArea)
			return false;

		// note final specific position
		Vector pathEndPosition;

		if (!goal && !goalArea)
			return false;

		if (!goal)
			pathEndPosition = *goalArea->GetCenter();
		else
			pathEndPosition = *goal;

		// make sure path end position is on the ground
		if (goalArea)
			pathEndPosition.z = goalArea->GetZ(&pathEndPosition);
		else
			GetGroundHeight(&pathEndPosition, &pathEndPosition.z);

		// if we are already in the goal area, build trivial path
		if (startArea == goalArea)
		{
			BuildTrivialPath(&pathEndPosition);
			return true;
		}

		// Compute shortest path to goal
		CNavArea* closestArea = nullptr;
		PathCost pathCost(route);
		bool pathToGoalExists = NavAreaBuildPath(startArea, goalArea, goal, pathCost, &closestArea);

		CNavArea* effectiveGoalArea = (pathToGoalExists) ? goalArea : closestArea;

		// Build path by following parent links
		// get count
		size_t count = 0;
		CNavArea* area;
		for (area = effectiveGoalArea; area; area = area->GetParent())
		{
			count++;
		}

		// save room for endpoint
		if (count > m_path.max_size() - 1)
			count = m_path.max_size() - 1;

		if (count == 0)
			return false;

		if (count == 1)
		{
			BuildTrivialPath(&pathEndPosition);
			return true;
		}

		// build path
		m_pathLength = count;
		for (area = effectiveGoalArea; count && area; area = area->GetParent())
		{
			count--;
			m_path[count].area = area;
			m_path[count].how = area->GetParentHow();
		}

		// compute path positions
		if (ComputePathPositions() == false)
		{
			PrintIfWatched("Error building path\n");
			DestroyPath();
			return false;
		}

		if (!goal)
		{
			switch (m_path[m_pathLength - 1].how)
			{
			case GO_NORTH:
			case GO_SOUTH:
				pathEndPosition.x = m_path[m_pathLength - 1].pos.x;
				pathEndPosition.y = effectiveGoalArea->GetCenter()->y;
				break;

			case GO_EAST:
			case GO_WEST:
				pathEndPosition.x = effectiveGoalArea->GetCenter()->x;
				pathEndPosition.y = m_path[m_pathLength - 1].pos.y;
				break;
			}

			GetGroundHeight(&pathEndPosition, &pathEndPosition.z);
		}

		// append path end position
		m_path[m_pathLength].area = effectiveGoalArea;
		m_path[m_pathLength].pos = pathEndPosition;
		m_path[m_pathLength].ladder = nullptr;
		m_path[m_pathLength].how = NUM_TRAVERSE_TYPES;
		m_pathLength++;

		// do movement setup
		m_pathIndex = 1;
		m_areaEnteredTimestamp = gpGlobals->time;
		m_spotEncounter = nullptr;
		m_goalPosition = m_path[1].pos;

		if (m_path[1].ladder)
			SetupLadderMovement();
		else
			m_pathLadder = nullptr;

		return true;
	}

	inline void DestroyPath() noexcept
	{
		m_pathLength = 0;
		m_pathLadder = nullptr;
	}

	// Build trivial path to goal, assuming we are already in the same area
	void BuildTrivialPath(const Vector* goal) noexcept
	{
		m_pathIndex = 1;
		m_pathLength = 2;

		m_path[0].area = m_lastKnownArea;
		m_path[0].pos = pev->origin;
		m_path[0].pos.z = m_lastKnownArea->GetZ(&pev->origin);
		m_path[0].ladder = nullptr;
		m_path[0].how = NUM_TRAVERSE_TYPES;

		m_path[1].area = m_lastKnownArea;
		m_path[1].pos = *goal;
		m_path[1].pos.z = m_lastKnownArea->GetZ(goal);
		m_path[1].ladder = nullptr;
		m_path[1].how = NUM_TRAVERSE_TYPES;

		m_areaEnteredTimestamp = gpGlobals->time;
		m_spotEncounter = nullptr;
		m_pathLadder = nullptr;

		m_goalPosition = *goal;
	}

	// Determine actual path positions bot will move between along the path
	bool ComputePathPositions() noexcept
	{
		if (m_pathLength == 0)
			return false;

		// start in first area's center
		m_path[0].pos = *m_path[0].area->GetCenter();
		m_path[0].ladder = nullptr;
		m_path[0].how = NUM_TRAVERSE_TYPES;

		for (auto i = 1u; i < m_pathLength; i++)
		{
			const ConnectInfo* from = &m_path[i - 1];
			ConnectInfo* to = &m_path[i];

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
				to->pos.z = from->area->GetZ(&to->pos);

				// if this is a "jump down" connection, we must insert an additional point on the path
				if (to->area->IsConnected(from->area, NUM_DIRECTIONS) == false)
				{
					// this is a "jump down" link
					// compute direction of path just prior to "jump down"
					Vector2D dir;
					DirectionToVector2D((NavDirType)to->how, &dir);

					// shift top of "jump down" out a bit to "get over the ledge"
					const float pushDist = 25.0f; // 75.0f;
					to->pos.x += pushDist * dir.x;
					to->pos.y += pushDist * dir.y;

					// insert a duplicate node to represent the bottom of the fall
					if (m_pathLength < m_path.max_size() - 1)
					{
						// copy nodes down
						for (auto j = m_pathLength; j > i; j--)
							m_path[j] = m_path[j - 1];

						// path is one node longer
						m_pathLength++;

						// move index ahead into the new node we just duplicated
						i++;

						m_path[i].pos.x = to->pos.x + pushDist * dir.x;
						m_path[i].pos.y = to->pos.y + pushDist * dir.y;

						// put this one at the bottom of the fall
						m_path[i].pos.z = to->area->GetZ(&m_path[i].pos);
					}
				}
			}
			// to get to next area, must go up a ladder
			else if (to->how == GO_LADDER_UP)
			{
				// find our ladder
				const NavLadderList* list = from->area->GetLadderList(LADDER_UP);
				NavLadderList::const_iterator iter;
				for (iter = list->begin(); iter != list->end(); iter++)
				{
					CNavLadder* ladder = (*iter);

					// can't use "behind" area when ascending...
					if (ladder->m_topForwardArea == to->area || ladder->m_topLeftArea == to->area || ladder->m_topRightArea == to->area)
					{
						to->ladder = ladder;
						to->pos = ladder->m_bottom;
						AddDirectionVector(&to->pos, ladder->m_dir, HalfHumanWidth * 2.0f);
						break;
					}
				}

				if (iter == list->end())
				{
					PrintIfWatched("ERROR: Can't find ladder in path\n");
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
						AddDirectionVector(&to->pos, Opposite[ladder->m_dir], HalfHumanWidth * 2.0f);
						break;
					}
				}

				if (iter == list->end())
				{
					PrintIfWatched("ERROR: Can't find ladder in path\n");
					return false;
				}
			}
		}

		return true;
	}

	// If next step of path uses a ladder, prepare to traverse it
	void SetupLadderMovement()
	{
	}

	static void PrintIfWatched(char const* format, ...) noexcept
	{
	}

	// Members

	entvars_t* pev{};

	CNavArea* m_currentArea{};					// the nav area we are standing on
	CNavArea* m_lastKnownArea{};				// the last area we were in

	struct ConnectInfo
	{
		CNavArea* area{};			// the area along the path
		NavTraverseType how{};		// how to enter this area from the previous one
		Vector pos{};				// our movement goal position at this point in the path
		const CNavLadder* ladder{};	// if "how" refers to a ladder, this is it
	};
	std::array<ConnectInfo, 256> m_path{};
	size_t m_pathLength{};
	size_t m_pathIndex{};
	float m_areaEnteredTimestamp{};
	SpotEncounter* m_spotEncounter{};				// the spots we will encounter as we move thru our current area
	Vector m_goalPosition{};
	const CNavLadder* m_pathLadder{};				// the ladder we need to use to reach the next area

	CountdownTimer m_repathTimer{};				// must have elapsed before bot can pathfind again
};
