module;

#include <assert.h>
#include <stdio.h>

export module Nav;

import std;
import hlsdk;

import CBase;
import Query;

import UtlRandom;

import :Const;

using std::FILE;




export enum EWalkThrough
{
	WALK_THRU_DOORS = 0x01,
	WALK_THRU_BREAKABLES = 0x02,

	WALK_THRU_EVERYTHING = (WALK_THRU_DOORS | WALK_THRU_BREAKABLES),
};

export enum NavErrorType
{
	NAV_OK,
	NAV_CANT_ACCESS_FILE,
	NAV_INVALID_FILE,
	NAV_BAD_FILE_VERSION,
	NAV_CORRUPT_DATA,
};

export inline constexpr float HalfHumanWidth = 16.0f;
export inline constexpr float HalfHumanHeight = 36.0f;
export inline constexpr float HumanHeight = 72.0f;

export inline bool IsEntityWalkable(entvars_t* pev, unsigned int flags) noexcept
{
	// if we hit a door, assume its walkable because it will open when we touch it
	if (FClassnameIs(pev, "func_door") || FClassnameIs(pev, "func_door_rotating"))
		return (flags & WALK_THRU_DOORS) ? true : false;

	// if we hit a breakable object, assume its walkable because we will shoot it when we touch it
	else if (FClassnameIs(pev, "func_breakable") && pev->takedamage == DAMAGE_YES)
		return (flags & WALK_THRU_BREAKABLES) ? true : false;

	return false;
}

export bool GetGroundHeight(const Vector* pos, float* height, Vector* normal = nullptr) noexcept
{
	Vector to;
	to.x = pos->x;
	to.y = pos->y;
	to.z = pos->z - 9999.9f;

	float offset;
	Vector from;
	TraceResult result;
	edict_t* ignore = nullptr;
	float ground = 0.0f;

	static constexpr float maxOffset = 100.0f;
	static constexpr float inc = 10.0f;
	static constexpr int MAX_GROUND_LAYERS = 16;

	struct GroundLayerInfo
	{
		float ground{};
		Vector normal{};

	} layer[MAX_GROUND_LAYERS];

	int layerCount = 0;
	for (offset = 1.0f; offset < maxOffset; offset += inc)
	{
		from = *pos + Vector(0, 0, offset);

		g_engfuncs.pfnTraceLine(from, to, ignore_monsters | dont_ignore_glass, ignore, &result);

		if (result.flFraction != 1.0f && result.pHit)
		{
			// ignoring any entities that we can walk through
			if (IsEntityWalkable(&result.pHit->v, WALK_THRU_DOORS | WALK_THRU_BREAKABLES))
			{
				ignore = result.pHit;
				continue;
			}
		}

		if (!result.fStartSolid)
		{
			if (layerCount == 0 || result.vecEndPos.z > layer[layerCount - 1].ground)
			{
				layer[layerCount].ground = result.vecEndPos.z;
				layer[layerCount].normal = result.vecPlaneNormal;
				layerCount++;

				if (layerCount == MAX_GROUND_LAYERS)
					break;
			}
		}
	}

	if (layerCount == 0)
		return false;

	int i;
	for (i = 0; i < layerCount - 1; i++)
	{
		if (layer[i + 1].ground - layer[i].ground >= HalfHumanHeight)
			break;
	}

	*height = layer[i].ground;

	if (normal)
	{
		*normal = layer[i].normal;
	}

	return true;
}


export enum NavDirType
{
	NORTH = 0,
	EAST,
	SOUTH,
	WEST,

	NUM_DIRECTIONS
};

export constexpr void AddDirectionVector(Vector* v, NavDirType dir, float amount) noexcept
{
	switch (dir)
	{
	case NORTH:
		v->y -= amount;
		return;
	case SOUTH:
		v->y += amount;
		return;
	case EAST:
		v->x += amount;
		return;
	case WEST:
		v->x -= amount;
		return;
	}
}

export constexpr void DirectionToVector2D(NavDirType dir, Vector2D* v) noexcept
{
	switch (dir)
	{
	case NORTH:
		v->x = 0.0f;
		v->y = -1.0f;
		break;
	case SOUTH:
		v->x = 0.0f;
		v->y = 1.0f;
		break;
	case EAST:
		v->x = 1.0f;
		v->y = 0.0f;
		break;
	case WEST:
		v->x = -1.0f;
		v->y = 0.0f;
		break;
	}
}

export constexpr NavDirType DirectionLeft(NavDirType dir) noexcept
{
	switch (dir)
	{
	case NORTH:
		return WEST;
	case SOUTH:
		return EAST;
	case EAST:
		return NORTH;
	case WEST:
		return SOUTH;
	}

	return NORTH;
}

export constexpr NavDirType DirectionRight(NavDirType dir) noexcept
{
	switch (dir)
	{
	case NORTH:
		return EAST;
	case SOUTH:
		return WEST;
	case EAST:
		return SOUTH;
	case WEST:
		return NORTH;
	}

	return NORTH;
}



export inline constexpr float GenerationStepSize = 25.f;  // (30) was 20, but bots can't always fit
export inline constexpr float StepHeight = 18.0f; // if delta Z is greater than this, we have to jump to get up
export inline constexpr float JumpHeight = 41.8f; // if delta Z is less than this, we can jump up on it
export inline constexpr float JumpCrouchHeight = 58.0f; // (48) if delta Z is less than or equal to this, we can jumpcrouch up on it

export inline constexpr std::array<NavDirType, NUM_DIRECTIONS> Opposite = { SOUTH, WEST, NORTH, EAST };


export using NavLadderList = std::list<class CNavLadder*>;
export using NavAreaList = std::list<class CNavArea*>;
export using HidingSpotList = std::list<class HidingSpot*>;

export extern "C++" inline NavLadderList TheNavLadderList{};
export extern "C++" inline HidingSpotList TheHidingSpotList{};
export extern "C++" inline NavAreaList TheNavAreaList{};

export class CNavNode
{
public:
	CNavNode(const Vector* pos, const Vector* normal, CNavNode* parent = nullptr) noexcept
	{
		m_pos = *pos;
		m_normal = *normal;

		static unsigned int nextID = 1;
		m_id = nextID++;

		for (int i = 0; i < NUM_DIRECTIONS; i++)
			m_to[i] = nullptr;

		m_visited = 0;
		m_parent = parent;

		m_next = m_list;
		m_list = this;
		m_listLength++;

		m_isCovered = false;
		m_area = nullptr;

		m_attributeFlags = 0;
	}

	// return navigation node at the position, or NULL if none exists
	static const CNavNode* GetNode(const Vector* pos) noexcept
	{
		static constexpr float tolerance = 0.45f * GenerationStepSize;

		for (const CNavNode* node = m_list; node; node = node->m_next)
		{
			auto const dx = std::abs(node->m_pos.x - pos->x);
			auto const dy = std::abs(node->m_pos.y - pos->y);
			auto const dz = std::abs(node->m_pos.z - pos->z);

			if (dx < tolerance && dy < tolerance && dz < tolerance)
				return node;
		}

		return nullptr;
	}

	// get navigation node connected in given direction, or NULL if cant go that way
	CNavNode* GetConnectedNode(NavDirType dir) const noexcept
	{
		return m_to[dir];
	}

	const Vector* GetPosition() const noexcept
	{
		return &m_pos;
	}
	const Vector* GetNormal() const noexcept { return &m_normal; }
	unsigned int GetID() const noexcept { return m_id; }

	static CNavNode* GetFirst() noexcept { return m_list; }
	static unsigned int GetListLength() noexcept { return m_listLength; }

	CNavNode* GetNext() noexcept { return m_next; }

	// create a connection FROM this node TO the given node, in the given direction
	void ConnectTo(CNavNode* node, NavDirType dir) noexcept
	{
		m_to[dir] = node;
	}
	CNavNode* GetParent() const noexcept
	{
		return m_parent;
	}

	// mark the given direction as having been visited
	void MarkAsVisited(NavDirType dir) noexcept
	{
		m_visited |= (1 << dir);
	}
	// return TRUE if the given direction has already been searched
	bool HasVisited(NavDirType dir) const noexcept
	{
		if (m_visited & (1 << dir))
			return true;

		return false;
	}
	// node is bidirectionally linked to another node in the given direction
	bool IsBiLinked(NavDirType dir) const noexcept
	{
		if (m_to[dir] && m_to[dir]->m_to[Opposite[dir]] == this)
			return true;

		return false;
	}
	// node is the NW corner of a bi-linked quad of nodes
	bool IsClosedCell() const noexcept
	{
		if (IsBiLinked(SOUTH) && IsBiLinked(EAST) && m_to[EAST]->IsBiLinked(SOUTH) && m_to[SOUTH]->IsBiLinked(EAST)
			&& m_to[EAST]->m_to[SOUTH] == m_to[SOUTH]->m_to[EAST])
			return true;

		return false;
	}

	void Cover() noexcept { m_isCovered = true; }			// #PF_TODO: Should pass in area that is covering
	bool IsCovered() const noexcept { return m_isCovered; }	// return true if this node has been covered by an area

	// assign the given area to this node
	void AssignArea(CNavArea* area) noexcept
	{
		m_area = area;
	}
	// return associated area
	CNavArea* GetArea() const noexcept
	{
		return m_area;
	}

	void SetAttributes(unsigned char bits) noexcept { m_attributeFlags = bits; }
	unsigned char GetAttributes() const noexcept { return m_attributeFlags; }

private:
	friend void DestroyNavigationMap() noexcept;

	Vector m_pos{};						// position of this node in the world
	Vector m_normal{};					// surface normal at this location
	std::array<CNavNode*, NUM_DIRECTIONS> m_to{};	// links to north, south, east, and west. NULL if no link
	unsigned int m_id{};					// unique ID of this node
	unsigned char m_attributeFlags{};		// set of attribute bit flags (see NavAttributeType)

	static inline CNavNode* m_list{};			// the master list of all nodes for this map
	static inline size_t m_listLength{};

	CNavNode* m_next{};					// next link in master list

	// below are only needed when generating
	// flags for automatic node generation. If direction bit is clear, that direction hasn't been explored yet.
	unsigned char m_visited{};
	CNavNode* m_parent{};			// the node prior to this in the search, which we pop back to when this node's search is done (a stack)
	qboolean m_isCovered{};			// true when this node is "covered" by a CNavArea
	CNavArea* m_area{};			// the area this node is contained within
};

export class CNavLadder
{
public:
	CNavLadder() noexcept
	{
		m_topForwardArea = nullptr;
		m_topRightArea = nullptr;
		m_topLeftArea = nullptr;
		m_topBehindArea = nullptr;
		m_bottomArea = nullptr;
		m_entity = nullptr;
	}

	Vector m_top{};
	Vector m_bottom{};
	float m_length{};
	NavDirType m_dir{};
	Vector2D m_dirVector{};
	CBaseEntity* m_entity{};

	CNavArea* m_topForwardArea{};
	CNavArea* m_topLeftArea{};
	CNavArea* m_topRightArea{};
	CNavArea* m_topBehindArea{};
	CNavArea* m_bottomArea{};

	bool m_isDangling{};

	void OnDestroyNotify(CNavArea* dead) noexcept
	{
		if (dead == m_topForwardArea)
			m_topForwardArea = nullptr;

		if (dead == m_topLeftArea)
			m_topLeftArea = nullptr;

		if (dead == m_topRightArea)
			m_topRightArea = nullptr;

		if (dead == m_topBehindArea)
			m_topBehindArea = nullptr;

		if (dead == m_bottomArea)
			m_bottomArea = nullptr;
	}
};

export class HidingSpot
{
public:
	HidingSpot() noexcept
	{
		m_pos = Vector(0, 0, 0);
		m_id = 0;
		m_flags = 0;

		TheHidingSpotList.push_back(this);
	}
	HidingSpot(const Vector* pos, unsigned char flags) noexcept
	{
		m_pos = *pos;
		m_id = m_nextID++;
		m_flags = flags;

		TheHidingSpotList.push_back(this);
	}

	enum
	{
		IN_COVER = 0x01,
		GOOD_SNIPER_SPOT = 0x02,
		IDEAL_SNIPER_SPOT = 0x04
	};

	bool HasGoodCover() const noexcept { return (m_flags & IN_COVER) ? true : false; }
	bool IsGoodSniperSpot() const noexcept { return (m_flags & GOOD_SNIPER_SPOT) ? true : false; }
	bool IsIdealSniperSpot() const noexcept { return (m_flags & IDEAL_SNIPER_SPOT) ? true : false; }

	void SetFlags(unsigned char flags) noexcept { m_flags |= flags; }
	unsigned char GetFlags() const noexcept { return m_flags; }

	void Save(FILE* fd, unsigned int version) const noexcept
	{
		std::fwrite(&m_id, sizeof(unsigned int), 1, fd);
		std::fwrite(&m_pos, sizeof(float), 3, fd);
		std::fwrite(&m_flags, sizeof(unsigned char), 1, fd);
	}
	void Load(SteamFile* file, unsigned int version) noexcept
	{
		file->Read(&m_id, sizeof(unsigned int));
		file->Read(&m_pos, 3 * sizeof(float));
		file->Read(&m_flags, sizeof(unsigned char));

		// update next ID to avoid ID collisions by later spots
		if (m_id >= m_nextID) {
			m_nextID = m_id + 1;
		}
	}

	const Vector* GetPosition() const noexcept { return &m_pos; }
	unsigned int GetID() const noexcept { return m_id; }

	void Mark() noexcept { m_marker = m_masterMarker; }
	bool IsMarked() const noexcept { return (m_marker == m_masterMarker) ? true : false; }

	static void ChangeMasterMarker() noexcept { m_masterMarker++; }

private:
	friend void DestroyHidingSpots() noexcept;

	Vector m_pos{};
	unsigned int m_id{};
	unsigned int m_marker{};
	unsigned char m_flags{};

	static inline unsigned int m_nextID{ 1 };
	static inline unsigned int m_masterMarker{ 0 };
};

// Given a HidingSpot ID, return the associated HidingSpot
HidingSpot* GetHidingSpotByID(unsigned int id)
{
	for (auto&& spot : TheHidingSpotList)
	{
		if (spot->GetID() == id)
			return spot;
	}

	return nullptr;
}

bool IsHidingSpotInCover(const Vector* spot) noexcept
{
	int coverCount = 0;
	TraceResult result;

	Vector from = *spot;
	from.z += HalfHumanHeight;

	// if we are crouched underneath something, that counts as good cover
	auto to = from + Vector(0, 0, 20.0f);
	g_engfuncs.pfnTraceLine(from, to, ignore_monsters | dont_ignore_glass, nullptr, &result);
	if (result.flFraction != 1.0f)
		return true;

	constexpr auto coverRange = 100.0f;
	constexpr auto inc = std::numbers::pi / 8.0f;

	for (auto angle = 0.0; angle < 2.0 * std::numbers::pi; angle += inc)
	{
		to = from + Vector(coverRange * std::cos(angle), coverRange * std::sin(angle), HalfHumanHeight);

		g_engfuncs.pfnTraceLine(from, to, ignore_monsters | dont_ignore_glass, nullptr, &result);

		// if traceline hit something, it hit "cover"
		if (result.flFraction != 1.0f)
			coverCount++;
	}

	// if more than half of the circle has no cover, the spot is not "in cover"
	constexpr int halfCover = 8;
	if (coverCount < halfCover)
		return false;

	return true;
}

// Determine how much walkable area we can see from the spot, and how far away we can see.
void ClassifySniperSpot(HidingSpot* spot) noexcept;

inline CNavArea* markedArea = nullptr;
inline CNavArea* lastSelectedArea = nullptr;
inline bool isCreatingNavArea = false;
inline bool isPlacePainting = false;

inline float editTimestamp = 0.0f;
inline float lastDrawTimestamp = 0.0f;

void EditNavAreasReset() noexcept
{
	markedArea = nullptr;
	lastSelectedArea = nullptr;
	isCreatingNavArea = false;
	isPlacePainting = false;

	editTimestamp = 0.0f;
	lastDrawTimestamp = 0.0f;
}

// The CNavAreaGrid is used to efficiently access navigation areas by world position
// Each cell of the grid contains a list of areas that overlap it
// Given a world position, the corresponding grid cell is ( x/cellsize, y/cellsize )
export class CNavAreaGrid
{
public:
	CNavAreaGrid() noexcept
	{
		m_grid = nullptr;
		Reset();
	}
	~CNavAreaGrid() noexcept
	{
		delete[] m_grid;
		m_grid = nullptr;
	}

	// clear the grid to empty
	void Reset() noexcept
	{
		if (m_grid)
		{
			delete[] m_grid;
			m_grid = nullptr;
		}

		m_gridSizeX = 0;
		m_gridSizeY = 0;

		// clear the hash table
		for (int i = 0; i < HASH_TABLE_SIZE; i++)
			m_hashTable[i] = nullptr;

		m_areaCount = 0;

		// reset static vars
		EditNavAreasReset();
	}
	// clear and reset the grid to the given extents
	void Initialize(float minX, float maxX, float minY, float maxY) noexcept
	{
		if (m_grid)
			Reset();

		m_minX = minX;
		m_minY = minY;

		m_gridSizeX = int((maxX - minX) / m_cellSize + 1);
		m_gridSizeY = int((maxY - minY) / m_cellSize + 1);

		m_grid = new NavAreaList[m_gridSizeX * m_gridSizeY];
	}
	// add an area to the grid
	void AddNavArea(CNavArea* area) noexcept;
	// remove an area from the grid
	void RemoveNavArea(CNavArea* area) noexcept;
	// return total number of nav areas
	unsigned int GetNavAreaCount() const noexcept { return m_areaCount; }
	// given a position, return the nav area that IsOverlapping and is *immediately* beneath it
	CNavArea* GetNavArea(const Vector* pos, float const beneathLimit = 120.0f) const noexcept;
	CNavArea* GetNavAreaByID(unsigned int id) const noexcept;
	CNavArea* GetNearestNavArea(const Vector* pos, bool anyZ = false) const noexcept;

	bool IsValid() const noexcept
	{
		return m_grid && m_areaCount > 0;
	}
	// return radio chatter place for given coordinate
	Place GetPlace(const Vector* pos) const noexcept;

private:
	static inline constexpr float m_cellSize = 300.f;
	NavAreaList* m_grid{};
	int m_gridSizeX{};
	int m_gridSizeY{};
	float m_minX{};
	float m_minY{};
	unsigned int m_areaCount{};				// total number of nav areas

	static inline constexpr auto HASH_TABLE_SIZE = 256u;
	std::array<CNavArea*, HASH_TABLE_SIZE> m_hashTable{};// hash table to optimize lookup by ID
	inline int ComputeHashKey(unsigned int id) const noexcept	// returns a hash key for the given nav area ID
	{
		return id & 0xFF;
	}

	inline int WorldToGridX(float wx) const noexcept
	{
		auto x = static_cast<int>((wx - m_minX) / m_cellSize);
		if (x < 0)
			x = 0;

		else if (x >= m_gridSizeX)
			x = m_gridSizeX - 1;

		return x;
	}
	inline int WorldToGridY(float wy) const noexcept
	{
		auto y = static_cast<int>((wy - m_minY) / m_cellSize);
		if (y < 0)
			y = 0;
		else if (y >= m_gridSizeY)
			y = m_gridSizeY - 1;

		return y;
	}
};

// The singleton for accessing the grid
export extern "C++" inline CNavAreaGrid TheNavAreaGrid{};




export struct SpotOrder
{
	float t{};
	union
	{
		HidingSpot* spot;
		std::uintptr_t id{};
	};
};

using SpotOrderList = std::list<SpotOrder>;

export struct SpotEncounter
{
	NavConnect from;
	NavDirType fromDir;
	NavConnect to;
	NavDirType toDir;
	Ray path;				// the path segment
	SpotOrderList spotList;	// list of spots to look at, in order of occurrence
};

using SpotEncounterList = std::list<SpotEncounter>;



export class CNavArea
{
public:
	CNavArea(CNavNode* nwNode, CNavNode* neNode, CNavNode* seNode, CNavNode* swNode) noexcept
	{
		Initialize();

		m_extent.lo = *nwNode->GetPosition();
		m_extent.hi = *seNode->GetPosition();

		m_center.x = (m_extent.lo.x + m_extent.hi.x) / 2.0f;
		m_center.y = (m_extent.lo.y + m_extent.hi.y) / 2.0f;
		m_center.z = (m_extent.lo.z + m_extent.hi.z) / 2.0f;

		m_neZ = neNode->GetPosition()->z;
		m_swZ = swNode->GetPosition()->z;

		m_node[NORTH_WEST] = nwNode;
		m_node[NORTH_EAST] = neNode;
		m_node[SOUTH_EAST] = seNode;
		m_node[SOUTH_WEST] = swNode;

		// mark internal nodes as part of this area
		AssignNodes(this);
	}
	CNavArea() noexcept
	{
		Initialize();
	}
	CNavArea(const Vector* corner, const Vector* otherCorner) noexcept
	{
		Initialize();

		if (corner->x < otherCorner->x)
		{
			m_extent.lo.x = corner->x;
			m_extent.hi.x = otherCorner->x;
		}
		else
		{
			m_extent.hi.x = corner->x;
			m_extent.lo.x = otherCorner->x;
		}

		if (corner->y < otherCorner->y)
		{
			m_extent.lo.y = corner->y;
			m_extent.hi.y = otherCorner->y;
		}
		else
		{
			m_extent.hi.y = corner->y;
			m_extent.lo.y = otherCorner->y;
		}

		m_extent.lo.z = corner->z;
		m_extent.hi.z = corner->z;

		m_center.x = (m_extent.lo.x + m_extent.hi.x) / 2.0f;
		m_center.y = (m_extent.lo.y + m_extent.hi.y) / 2.0f;
		m_center.z = (m_extent.lo.z + m_extent.hi.z) / 2.0f;

		m_neZ = corner->z;
		m_swZ = otherCorner->z;
	}
	CNavArea(const Vector* nwCorner, const Vector* neCorner, const Vector* seCorner, const Vector* swCorner) noexcept
	{
		Initialize();

		m_extent.lo = *nwCorner;
		m_extent.hi = *seCorner;

		m_center.x = (m_extent.lo.x + m_extent.hi.x) / 2.0f;
		m_center.y = (m_extent.lo.y + m_extent.hi.y) / 2.0f;
		m_center.z = (m_extent.lo.z + m_extent.hi.z) / 2.0f;

		m_neZ = neCorner->z;
		m_swZ = swCorner->z;
	}

	~CNavArea() noexcept
	{
		// if we are resetting the system, don't bother cleaning up - all areas are being destroyed
		if (m_isReset)
			return;

		// tell the other areas we are going away
		NavAreaList::iterator iter;
		for (iter = TheNavAreaList.begin(); iter != TheNavAreaList.end(); iter++)
		{
			CNavArea* area = (*iter);

			if (area == this)
				continue;

			area->OnDestroyNotify(this);
		}

		// unhook from ladders
		for (int i = 0; i < NUM_LADDER_DIRECTIONS; i++)
		{
			for (NavLadderList::iterator liter = m_ladder[i].begin(); liter != m_ladder[i].end(); liter++)
			{
				CNavLadder* ladder = *liter;
				ladder->OnDestroyNotify(this);
			}
		}

		// remove the area from the grid
		TheNavAreaGrid.RemoveNavArea(this);
	}

	// connect this area to given area in given direction
	void ConnectTo(CNavArea* area, NavDirType dir) noexcept
	{
		assert(area);

		// check if already connected
		for (auto iter = m_connect[dir].begin(); iter != m_connect[dir].end(); ++iter)
		{
			if ((*iter).area == area)
				return;
		}

		NavConnect con;
		con.area = area;
		m_connect[dir].push_back(con);

		//static char *dirName[] = { "NORTH", "EAST", "SOUTH", "WEST" };
		//CONSOLE_ECHO("  Connected area #%d to #%d, %s\n", m_id, area->m_id, dirName[dir]);
	}
	// disconnect this area from given area
	void Disconnect(CNavArea* area) noexcept
	{
		NavConnect connect;
		connect.area = area;

		for (int dir = 0; dir < NUM_DIRECTIONS; ++dir)
			m_connect[dir].remove(connect);
	}

#ifdef CSBOT_ENABLE_SAVE
	void Save(FILE* fp) const noexcept
	{
		fprintf(fp, "v  %f %f %f\n", m_extent.lo.x, m_extent.lo.y, m_extent.lo.z);
		fprintf(fp, "v  %f %f %f\n", m_extent.hi.x, m_extent.lo.y, m_neZ);
		fprintf(fp, "v  %f %f %f\n", m_extent.hi.x, m_extent.hi.y, m_extent.hi.z);
		fprintf(fp, "v  %f %f %f\n", m_extent.lo.x, m_extent.hi.y, m_swZ);

		static int base = 1;
		fprintf(fp, "\n\ng %04dArea%s%s%s%s\n", m_id,
			(GetAttributes() & NAV_CROUCH) ? "CROUCH" : "", (GetAttributes() & NAV_JUMP) ? "JUMP" : "",
			(GetAttributes() & NAV_PRECISE) ? "PRECISE" : "", (GetAttributes() & NAV_NO_JUMP) ? "NO_JUMP" : "");

		fprintf(fp, "f %d %d %d %d\n\n", base, base + 1, base + 2, base + 3);
		base += 4;
	}
	void Save(FILE* fd, unsigned int version) noexcept
	{
		// save ID
		std::fwrite(&m_id, sizeof(unsigned int), 1, fd);

		// save attribute flags
		std::fwrite(&m_attributeFlags, sizeof(unsigned char), 1, fd);

		// save extent of area
		std::fwrite(&m_extent, sizeof(float), 6, fd);

		// save heights of implicit corners
		std::fwrite(&m_neZ, sizeof(float), 1, fd);
		std::fwrite(&m_swZ, sizeof(float), 1, fd);

		// save connections to adjacent areas
		// in the enum order NORTH, EAST, SOUTH, WEST
		for (int d = 0; d < NUM_DIRECTIONS; d++)
		{
			// save number of connections for this direction
			size_t count = m_connect[d].size();
			std::fwrite(&count, sizeof(size_t), 1, fd);

			for (auto& connect : m_connect[d]) {
				std::fwrite(&connect.area->m_id, sizeof(unsigned int), 1, fd);
			}
		}

		// Store hiding spots for this area
		unsigned char count;
		if (m_hidingSpotList.size() > 255)
		{
			count = 255;
			CONSOLE_ECHO("Warning: NavArea #%d: Truncated hiding spot list to 255\n", m_id);
		}
		else
		{
			count = (unsigned char)m_hidingSpotList.size();
		}

		std::fwrite(&count, sizeof(unsigned char), 1, fd);

		// store HidingSpot objects
		unsigned int saveCount = 0;
		for (auto spot : m_hidingSpotList)
		{
			spot->Save(fd, version);

			// overflow check
			if (++saveCount == count)
				break;
		}

		// Save the approach areas for this area
		// save number of approach areas
		std::fwrite(&m_approachCount, sizeof(unsigned char), 1, fd);

#ifdef CSBOT_DEBUG
		if (cv_bot_debug.value > 0.0f)
		{
			CONSOLE_ECHO("  m_approachCount = %d\n", m_approachCount);
		}
#endif

		// save approach area info
		unsigned char type;
		unsigned int zero = 0;
		for (int a = 0; a < m_approachCount; a++)
		{
			if (m_approach[a].here.area)
				std::fwrite(&m_approach[a].here.area->m_id, sizeof(unsigned int), 1, fd);
			else
				std::fwrite(&zero, sizeof(unsigned int), 1, fd);

			if (m_approach[a].prev.area)
				std::fwrite(&m_approach[a].prev.area->m_id, sizeof(unsigned int), 1, fd);
			else
				std::fwrite(&zero, sizeof(unsigned int), 1, fd);

			type = (unsigned char)m_approach[a].prevToHereHow;
			std::fwrite(&type, sizeof(unsigned char), 1, fd);

			if (m_approach[a].next.area)
				std::fwrite(&m_approach[a].next.area->m_id, sizeof(unsigned int), 1, fd);
			else
				std::fwrite(&zero, sizeof(unsigned int), 1, fd);

			type = (unsigned char)m_approach[a].hereToNextHow;
			std::fwrite(&type, sizeof(unsigned char), 1, fd);
		}

		// Save encounter spots for this area
		{
			// save number of encounter paths for this area
			unsigned int count = m_spotEncounterList.size();
			std::fwrite(&count, sizeof(unsigned int), 1, fd);

#ifdef CSBOT_DEBUG
			if (cv_bot_debug.value > 0.0f)
				CONSOLE_ECHO("  m_spotEncounterList.size() = %d\n", count);
#endif

			for (auto& spote : m_spotEncounterList)
			{
				if (spote.from.area)
					std::fwrite(&spote.from.area->m_id, sizeof(unsigned int), 1, fd);
				else
					std::fwrite(&zero, sizeof(unsigned int), 1, fd);

				unsigned char dir = spote.fromDir;
				std::fwrite(&dir, sizeof(unsigned char), 1, fd);

				if (spote.to.area)
					std::fwrite(&spote.to.area->m_id, sizeof(unsigned int), 1, fd);
				else
					std::fwrite(&zero, sizeof(unsigned int), 1, fd);

				dir = spote.toDir;
				std::fwrite(&dir, sizeof(unsigned char), 1, fd);

				// write list of spots along this path
				unsigned char spotCount;
				if (spote.spotList.size() > 255)
				{
					spotCount = 255;
					CONSOLE_ECHO("Warning: NavArea #%d: Truncated encounter spot list to 255\n", m_id);
				}
				else
				{
					spotCount = (unsigned char)spote.spotList.size();
				}
				std::fwrite(&spotCount, sizeof(unsigned char), 1, fd);

				saveCount = 0;
				for (auto& order : spote.spotList)
				{
					// order->spot may be NULL if we've loaded a nav mesh that has been edited but not re-analyzed
					unsigned int id = (order.spot) ? order.spot->GetID() : 0;
					std::fwrite(&id, sizeof(unsigned int), 1, fd);

					unsigned char t = 255 * order.t;
					std::fwrite(&t, sizeof(unsigned char), 1, fd);

					// overflow check
					if (++saveCount == spotCount)
						break;
				}
			}
		}

		// store place dictionary entry
		PlaceDirectory::EntryType entry = placeDirectory.GetEntry(GetPlace());
		std::fwrite(&entry, sizeof(entry), 1, fd);
	}
#endif

	void Load(SteamFile* file, unsigned int version) noexcept
	{
		// load ID
		file->Read(&m_id, sizeof(unsigned int));

		// update nextID to avoid collisions
		if (m_id >= m_nextID)
			m_nextID = m_id + 1;

		// load attribute flags
		file->Read(&m_attributeFlags, sizeof(unsigned char));

		// load extent of area
		file->Read(&m_extent, 6 * sizeof(float));

		m_center.x = (m_extent.lo.x + m_extent.hi.x) / 2.0f;
		m_center.y = (m_extent.lo.y + m_extent.hi.y) / 2.0f;
		m_center.z = (m_extent.lo.z + m_extent.hi.z) / 2.0f;

		// load heights of implicit corners
		file->Read(&m_neZ, sizeof(float));
		file->Read(&m_swZ, sizeof(float));

		// load connections (IDs) to adjacent areas
		// in the enum order NORTH, EAST, SOUTH, WEST
		for (int d = 0; d < NUM_DIRECTIONS; d++)
		{
			// load number of connections for this direction
			unsigned int count;
			file->Read(&count, sizeof(unsigned int));

			for (unsigned int i = 0; i < count; i++)
			{
				NavConnect connect;
				file->Read(&connect.id, sizeof(unsigned int));

				m_connect[d].push_back(connect);
			}
		}

		// Load hiding spots
		// load number of hiding spots
		unsigned char hidingSpotCount = 0;
		file->Read(&hidingSpotCount, sizeof(unsigned char));

		if (version == 1)
		{
			// load simple vector array
			Vector pos;
			for (int h = 0; h < hidingSpotCount; h++)
			{
				file->Read(&pos, 3 * sizeof(float));

				// create new hiding spot and put on master list
				HidingSpot* spot = new HidingSpot(&pos, HidingSpot::IN_COVER);

				m_hidingSpotList.push_back(spot);
			}
		}
		else
		{
			// load HidingSpot objects for this area
			for (int h = 0; h < hidingSpotCount; h++)
			{
				// create new hiding spot and put on master list
				HidingSpot* spot = new HidingSpot;

				spot->Load(file, version);

				m_hidingSpotList.push_back(spot);
			}
		}

		// Load number of approach areas
		file->Read(&m_approachCount, sizeof(unsigned char));

		// load approach area info (IDs)
		unsigned char type = 0;
		for (int a = 0; a < m_approachCount; a++)
		{
			file->Read(&m_approach[a].here.id, sizeof(unsigned int));

			file->Read(&m_approach[a].prev.id, sizeof(unsigned int));
			file->Read(&type, sizeof(unsigned char));
			m_approach[a].prevToHereHow = (NavTraverseType)type;

			file->Read(&m_approach[a].next.id, sizeof(unsigned int));
			file->Read(&type, sizeof(unsigned char));
			m_approach[a].hereToNextHow = (NavTraverseType)type;
		}

		// Load encounter paths for this area
		unsigned int count;
		file->Read(&count, sizeof(unsigned int));

		if (version < 3)
		{
			// old data, read and discard
			for (unsigned int e = 0; e < count; e++)
			{
				SpotEncounter encounter;

				file->Read(&encounter.from.id, sizeof(unsigned int));
				file->Read(&encounter.to.id, sizeof(unsigned int));

				file->Read(&encounter.path.from.x, 3 * sizeof(float));
				file->Read(&encounter.path.to.x, 3 * sizeof(float));

				// read list of spots along this path
				unsigned char spotCount = 0;
				file->Read(&spotCount, sizeof(unsigned char));

				for (int s = 0; s < spotCount; s++)
				{
					Vector pos;
					file->Read(&pos, 3 * sizeof(float));
					file->Read(&pos, sizeof(float));
				}
			}

			return;
		}

		for (unsigned int e = 0; e < count; e++)
		{
			SpotEncounter encounter;

			file->Read(&encounter.from.id, sizeof(unsigned int));

			unsigned char dir = 0;
			file->Read(&dir, sizeof(unsigned char));
			encounter.fromDir = static_cast<NavDirType>(dir);

			file->Read(&encounter.to.id, sizeof(unsigned int));

			file->Read(&dir, sizeof(unsigned char));
			encounter.toDir = static_cast<NavDirType>(dir);

			// read list of spots along this path
			unsigned char spotCount = 0;
			file->Read(&spotCount, sizeof(unsigned char));

			SpotOrder order;
			for (int s = 0; s < spotCount; s++)
			{
				file->Read(&order.id, sizeof(unsigned int));

				unsigned char t = 0;
				file->Read(&t, sizeof(unsigned char));

				order.t = float(t) / 255.0f;

				encounter.spotList.push_back(order);
			}

			m_spotEncounterList.push_back(encounter);
		}

		if (version >= NAV_VERSION)
		{
			// Load Place data
			PlaceDirectory::EntryType entry;
			file->Read(&entry, sizeof(entry));

			// convert entry to actual Place
			SetPlace(placeDirectory.EntryToPlace(entry));
		}
	}
	NavErrorType PostLoad() noexcept
	{
		NavErrorType error = NAV_OK;

		// connect areas together
		for (int d = 0; d < NUM_DIRECTIONS; d++)
		{
			for (auto& connect : m_connect[d])
			{
				auto id = connect.id;
				connect.area = TheNavAreaGrid.GetNavAreaByID(id);
				if (id && !connect.area)
				{
					CONSOLE_ECHO("ERROR: Corrupt navigation data. Cannot connect Navigation Areas.\n");
					error = NAV_CORRUPT_DATA;
				}
			}
		}

		// resolve approach area IDs
		for (int a = 0; a < m_approachCount; a++)
		{
			m_approach[a].here.area = TheNavAreaGrid.GetNavAreaByID(m_approach[a].here.id);
			if (m_approach[a].here.id && !m_approach[a].here.area)
			{
				CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing Approach Area (here).\n");
				error = NAV_CORRUPT_DATA;
			}

			m_approach[a].prev.area = TheNavAreaGrid.GetNavAreaByID(m_approach[a].prev.id);
			if (m_approach[a].prev.id && !m_approach[a].prev.area)
			{
				CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing Approach Area (prev).\n");
				error = NAV_CORRUPT_DATA;
			}

			m_approach[a].next.area = TheNavAreaGrid.GetNavAreaByID(m_approach[a].next.id);
			if (m_approach[a].next.id && !m_approach[a].next.area)
			{
				CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing Approach Area (next).\n");
				error = NAV_CORRUPT_DATA;
			}
		}

		// resolve spot encounter IDs
		for (auto& spote : m_spotEncounterList)
		{
			spote.from.area = TheNavAreaGrid.GetNavAreaByID(spote.from.id);
			if (!spote.from.area)
			{
				CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing \"from\" Navigation Area for Encounter Spot.\n");
				error = NAV_CORRUPT_DATA;
			}

			spote.to.area = TheNavAreaGrid.GetNavAreaByID(spote.to.id);
			if (!spote.to.area)
			{
				CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing \"to\" Navigation Area for Encounter Spot.\n");
				error = NAV_CORRUPT_DATA;
			}

			if (spote.from.area && spote.to.area)
			{
				// compute path
				float halfWidth;
				ComputePortal(spote.to.area, spote.toDir, &spote.path.to, &halfWidth);
				ComputePortal(spote.from.area, spote.fromDir, &spote.path.from, &halfWidth);

				const float eyeHeight = HalfHumanHeight;
				spote.path.from.z = spote.from.area->GetZ(&spote.path.from) + eyeHeight;
				spote.path.to.z = spote.to.area->GetZ(&spote.path.to) + eyeHeight;
			}

			// resolve HidingSpot IDs
			for (auto& order : spote.spotList)
			{
				order.spot = GetHidingSpotByID(order.id);
				if (!order.spot)
				{
					CONSOLE_ECHO("ERROR: Corrupt navigation data. Missing Hiding Spot\n");
					error = NAV_CORRUPT_DATA;
				}
			}
		}

		// build overlap list
		// #PF_TODO: Optimize this
		for (auto area : TheNavAreaList)
		{
			if (area == this)
				continue;

			if (IsOverlapping(area))
				m_overlapList.push_back(area);
		}

		return error;
	}

	unsigned int GetID() const noexcept { return m_id; }
	void SetAttributes(unsigned char bits) noexcept { m_attributeFlags = bits; }
	unsigned char GetAttributes() const noexcept { return m_attributeFlags; }
	void SetPlace(Place place) noexcept { m_place = place; }			// set place descriptor
	Place GetPlace() const noexcept { return m_place; }					// get place descriptor
	
	// return true if 'pos' is within 2D extents of area
	bool IsOverlapping(const Vector* pos) const noexcept
	{
		if (pos->x >= m_extent.lo.x && pos->x <= m_extent.hi.x &&
			pos->y >= m_extent.lo.y && pos->y <= m_extent.hi.y)
			return true;

		return false;
	}
	// return true if 'area' overlaps our 2D extents
	bool IsOverlapping(const CNavArea* area) const noexcept
	{
		if (area->m_extent.lo.x < m_extent.hi.x && area->m_extent.hi.x > m_extent.lo.x &&
			area->m_extent.lo.y < m_extent.hi.y && area->m_extent.hi.y > m_extent.lo.y)
			return true;

		return false;
	}
	// return true if 'area' overlaps our X extent
	bool IsOverlappingX(const CNavArea* area) const noexcept
	{
		if (area->m_extent.lo.x < m_extent.hi.x && area->m_extent.hi.x > m_extent.lo.x)
			return true;

		return false;
	}
	// return true if 'area' overlaps our Y extent
	bool IsOverlappingY(const CNavArea* area) const noexcept
	{
		if (area->m_extent.lo.y < m_extent.hi.y && area->m_extent.hi.y > m_extent.lo.y)
			return true;

		return false;
	}
	// return number of players with given teamID in this area (teamID == 0 means any/all)
	int GetPlayerCount(int teamID = 0, CBasePlayer* pEntIgnore = nullptr) const noexcept
	{
		int nCount = 0;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			CBasePlayer* pPlayer = ent_cast<CBasePlayer*>(i);

			if (pPlayer == pEntIgnore || !pPlayer)
				continue;

			if (pev_valid(pPlayer->pev) != EValidity::Full || pPlayer->IsDormant())
				continue;

			if (!pPlayer->IsPlayer())
				continue;

			if (!pPlayer->IsAlive())
				continue;

			if (teamID == 0/*unassigned*/ || pPlayer->m_iTeam == teamID)
			{
				if (Contains(&pPlayer->pev->origin))
					++nCount;
			}
		}

		return nCount;
	}

	// return Z of area at (x,y) of 'pos'
	float GetZ(const Vector* pos) const noexcept
	{
		float const dx = m_extent.hi.x - m_extent.lo.x;
		float const dy = m_extent.hi.y - m_extent.lo.y;

		// guard against division by zero due to degenerate areas
		if (dx == 0.0f || dy == 0.0f)
			return m_neZ;

		float u = (pos->x - m_extent.lo.x) / dx;
		float v = (pos->y - m_extent.lo.y) / dy;

		// clamp Z values to (x,y) volume
		if (u < 0.0f)
			u = 0.0f;
		else if (u > 1.0f)
			u = 1.0f;

		if (v < 0.0f)
			v = 0.0f;
		else if (v > 1.0f)
			v = 1.0f;

		float const northZ = m_extent.lo.z + u * (m_neZ - m_extent.lo.z);
		float const southZ = m_swZ + u * (m_extent.hi.z - m_swZ);

		return northZ + v * (southZ - northZ);
	}
	// return Z of area at (x,y) of 'pos'
	float GetZ(float x, float y) const noexcept { Vector pos(x, y, 0.0f); return GetZ(&pos); }
	// return true if given point is on or above this area, but no others
	bool Contains(const Vector* pos) const noexcept
	{
		// check 2D overlap
		if (!IsOverlapping(pos))
			return false;

		// the point overlaps us, check that it is above us, but not above any areas that overlap us
		float const ourZ = GetZ(pos);

		// if we are above this point, fail
		if (ourZ > pos->z)
			return false;

		for (NavAreaList::const_iterator iter = m_overlapList.begin(); iter != m_overlapList.end(); iter++)
		{
			const CNavArea* area = (*iter);

			// skip self
			if (area == this)
				continue;

			// check 2D overlap
			if (!area->IsOverlapping(pos))
				continue;

			float const theirZ = area->GetZ(pos);
			if (theirZ > pos->z)
			{
				// they are above the point
				continue;
			}

			if (theirZ > ourZ)
			{
				// we are below an area that is closer underneath the point
				return false;
			}
		}

		return true;
	}

	// return true if this area and given area are approximately co-planar
	bool IsCoplanar(const CNavArea* area) const noexcept
	{
		Vector u, v;

		// compute our unit surface normal
		u.x = m_extent.hi.x - m_extent.lo.x;
		u.y = 0.0f;
		u.z = m_neZ - m_extent.lo.z;

		v.x = 0.0f;
		v.y = m_extent.hi.y - m_extent.lo.y;
		v.z = m_swZ - m_extent.lo.z;

		Vector const normal = CrossProduct(u, v).Normalize();

		// compute their unit surface normal
		u.x = area->m_extent.hi.x - area->m_extent.lo.x;
		u.y = 0.0f;
		u.z = area->m_neZ - area->m_extent.lo.z;

		v.x = 0.0f;
		v.y = area->m_extent.hi.y - area->m_extent.lo.y;
		v.z = area->m_swZ - area->m_extent.lo.z;

		Vector const otherNormal = CrossProduct(u, v).Normalize();

		// can only merge areas that are nearly planar, to ensure areas do not differ from underlying geometry much
		static constexpr float tolerance = 0.99f; // 0.7071f;		// 0.9
		if (DotProduct(normal, otherNormal) > tolerance)
			return true;

		return false;
	}
	// return closest point to 'pos' on this area - returned point in 'close'
	void GetClosestPointOnArea(const Vector* pos, Vector* close) const noexcept
	{
		const Extent* extent = GetExtent();
		if (pos->x < extent->lo.x)
		{
			if (pos->y < extent->lo.y)
			{
				// position is north-west of area
				*close = extent->lo;
			}
			else if (pos->y > extent->hi.y)
			{
				// position is south-west of area
				close->x = extent->lo.x;
				close->y = extent->hi.y;
			}
			else
			{
				// position is west of area
				close->x = extent->lo.x;
				close->y = pos->y;
			}
		}
		else if (pos->x > extent->hi.x)
		{
			if (pos->y < extent->lo.y)
			{
				// position is north-east of area
				close->x = extent->hi.x;
				close->y = extent->lo.y;
			}
			else if (pos->y > extent->hi.y)
			{
				// position is south-east of area
				*close = extent->hi;
			}
			else
			{
				// position is east of area
				close->x = extent->hi.x;
				close->y = pos->y;
			}
		}
		else if (pos->y < extent->lo.y)
		{
			// position is north of area
			close->x = pos->x;
			close->y = extent->lo.y;
		}
		else if (pos->y > extent->hi.y)
		{
			// position is south of area
			close->x = pos->x;
			close->y = extent->hi.y;
		}
		else
		{
			// position is inside of area - it is the 'closest point' to itself
			*close = *pos;
		}

		close->z = GetZ(close);
	}
	// return shortest distance between point and this area
	float GetDistanceSquaredToPoint(const Vector* pos) const noexcept
	{
		const Extent* extent = GetExtent();

		if (pos->x < extent->lo.x)
		{
			if (pos->y < extent->lo.y)
			{
				// position is north-west of area
				return (float)(extent->lo - *pos).LengthSquared();
			}
			else if (pos->y > extent->hi.y)
			{
				// position is south-west of area
				Vector d;
				d.x = extent->lo.x - pos->x;
				d.y = extent->hi.y - pos->y;
				d.z = m_swZ - pos->z;
				return (float)d.LengthSquared();
			}
			else
			{
				// position is west of area
				auto const d = extent->lo.x - pos->x;
				return d * d;
			}
		}
		else if (pos->x > extent->hi.x)
		{
			if (pos->y < extent->lo.y)
			{
				// position is north-east of area
				Vector d;
				d.x = extent->hi.x - pos->x;
				d.y = extent->lo.y - pos->y;
				d.z = m_neZ - pos->z;
				return (float)d.LengthSquared();
			}
			else if (pos->y > extent->hi.y)
			{
				// position is south-east of area
				return (float)(extent->hi - *pos).LengthSquared();
			}
			else
			{
				// position is east of area
				auto const d = pos->z - extent->hi.x;
				return d * d;
			}
		}
		else if (pos->y < extent->lo.y)
		{
			// position is north of area
			auto const d = extent->lo.y - pos->y;
			return d * d;
		}
		else if (pos->y > extent->hi.y)
		{
			// position is south of area
			auto const d = pos->y - extent->hi.y;
			return d * d;
		}
		else
		{
			// position is inside of 2D extent of area - find delta Z
			auto const z = GetZ(pos);
			auto const d = z - pos->z;
			return d * d;
		}
	}
	// return true if this area is badly formed
	bool IsDegenerate() const noexcept
	{
		return (m_extent.lo.x >= m_extent.hi.x || m_extent.lo.y >= m_extent.hi.y);
	}
	// return true if there are no bi-directional links on the given side
	bool IsEdge(NavDirType dir) const noexcept
	{
		for (auto& connect : m_connect[dir])
		{
			if (connect.area->IsConnected(this, Opposite[dir]))
				return false;
		}

		return true;
	}
	// return number of connected areas in given direction
	int GetAdjacentCount(NavDirType dir) const noexcept { return m_connect[dir].size(); }
	// return the i'th adjacent area in the given direction
	CNavArea* GetAdjacentArea(NavDirType dir, int i) const noexcept
	{
		for (auto& con : m_connect[dir])
		{
			if (i == 0)
				return con.area;
			i--;
		}

		return nullptr;
	}
	CNavArea* GetRandomAdjacentArea(NavDirType dir) const noexcept
	{
		auto const count = m_connect[dir].size();
		auto const which = UTIL_Random(0u, count - 1);

		auto it = m_connect[dir].begin();
		std::advance(it, which);
		return it->area;
	}
	const NavConnectList* GetAdjacentList(NavDirType dir) const noexcept { return &m_connect[dir]; }
	// Return true if given area is connected in given direction
	// if dir == NUM_DIRECTIONS, check all directions (direction is unknown)
	// #PF_TODO: Formalize "asymmetric" flag on connections
	bool IsConnected(const CNavArea* area, NavDirType dir) const noexcept

	{
		// we are connected to ourself
		if (area == this)
			return true;

		NavConnectList::const_iterator iter;

		if (dir == NUM_DIRECTIONS)
		{
			// search all directions
			for (int d = 0; d < NUM_DIRECTIONS; d++)
			{
				for (iter = m_connect[d].begin(); iter != m_connect[d].end(); iter++)
				{
					if (area == (*iter).area)
						return true;
				}
			}

			// check ladder connections
			NavLadderList::const_iterator liter;
			for (liter = m_ladder[LADDER_UP].begin(); liter != m_ladder[LADDER_UP].end(); liter++)
			{
				CNavLadder* ladder = *liter;

				if (ladder->m_topBehindArea == area || ladder->m_topForwardArea == area || ladder->m_topLeftArea == area || ladder->m_topRightArea == area)
					return true;
			}

			for (liter = m_ladder[LADDER_DOWN].begin(); liter != m_ladder[LADDER_DOWN].end(); liter++)
			{
				CNavLadder* ladder = *liter;

				if (ladder->m_bottomArea == area)
					return true;
			}
		}
		else
		{
			// check specific direction
			for (iter = m_connect[dir].begin(); iter != m_connect[dir].end(); iter++)
			{
				if (area == (*iter).area)
					return true;
			}
		}

		return false;
	}
	// compute change in height from this area to given area
	float ComputeHeightChange(const CNavArea* area) const noexcept
	{
		auto const ourZ = GetZ(GetCenter());
		auto const areaZ = area->GetZ(area->GetCenter());

		return areaZ - ourZ;
	}

	const NavLadderList* GetLadderList(LadderDirectionType dir) const noexcept { return &m_ladder[dir]; }

	// Compute "portal" between to adjacent areas.
	// Return center of portal opening, and half-width defining sides of portal from center.
	// NOTE: center->z is unset.
	void ComputePortal(const CNavArea* to, NavDirType dir, Vector* center, float* halfWidth) const noexcept
	{
		if (dir == NORTH || dir == SOUTH)
		{
			if (dir == NORTH)
				center->y = m_extent.lo.y;
			else
				center->y = m_extent.hi.y;

			float left = std::max(m_extent.lo.x, to->m_extent.lo.x);
			float right = std::min(m_extent.hi.x, to->m_extent.hi.x);

			// clamp to our extent in case areas are disjoint
			if (left < m_extent.lo.x)
				left = m_extent.lo.x;
			else if (left > m_extent.hi.x)
				left = m_extent.hi.x;

			if (right < m_extent.lo.x)
				right = m_extent.lo.x;
			else if (right > m_extent.hi.x)
				right = m_extent.hi.x;

			center->x = (left + right) / 2.0f;
			*halfWidth = (right - left) / 2.0f;
		}
		else	// EAST or WEST
		{
			if (dir == WEST)
				center->x = m_extent.lo.x;
			else
				center->x = m_extent.hi.x;

			float top = std::max(m_extent.lo.y, to->m_extent.lo.y);
			float bottom = std::min(m_extent.hi.y, to->m_extent.hi.y);

			// clamp to our extent in case areas are disjoint
			if (top < m_extent.lo.y)
				top = m_extent.lo.y;
			else if (top > m_extent.hi.y)
				top = m_extent.hi.y;

			if (bottom < m_extent.lo.y)
				bottom = m_extent.lo.y;
			else if (bottom > m_extent.hi.y)
				bottom = m_extent.hi.y;

			center->y = (top + bottom) / 2.0f;
			*halfWidth = (bottom - top) / 2.0f;
		}
	}
	// Compute closest point within the "portal" between to adjacent areas.
	void ComputeClosestPointInPortal(const CNavArea* to, NavDirType dir, const Vector* fromPos, Vector* closePos) const noexcept
	{
		static constexpr float margin = GenerationStepSize / 2.0f;

		if (dir == NORTH || dir == SOUTH)
		{
			if (dir == NORTH)
				closePos->y = m_extent.lo.y;
			else
				closePos->y = m_extent.hi.y;

			float left = std::max(m_extent.lo.x, to->m_extent.lo.x);
			float right = std::min(m_extent.hi.x, to->m_extent.hi.x);

			// clamp to our extent in case areas are disjoint
			if (left < m_extent.lo.x)
				left = m_extent.lo.x;
			else if (left > m_extent.hi.x)
				left = m_extent.hi.x;

			if (right < m_extent.lo.x)
				right = m_extent.lo.x;
			else if (right > m_extent.hi.x)
				right = m_extent.hi.x;

			// keep margin if against edge
			const float leftMargin = (to->IsEdge(WEST)) ? (left + margin) : left;
			const float rightMargin = (to->IsEdge(EAST)) ? (right - margin) : right;

			// limit x to within portal
			if (fromPos->x < leftMargin)
				closePos->x = leftMargin;
			else if (fromPos->x > rightMargin)
				closePos->x = rightMargin;
			else
				closePos->x = fromPos->x;

		}
		else	// EAST or WEST
		{
			if (dir == WEST)
				closePos->x = m_extent.lo.x;
			else
				closePos->x = m_extent.hi.x;

			float top = std::max(m_extent.lo.y, to->m_extent.lo.y);
			float bottom = std::min(m_extent.hi.y, to->m_extent.hi.y);

			// clamp to our extent in case areas are disjoint
			if (top < m_extent.lo.y)
				top = m_extent.lo.y;
			else if (top > m_extent.hi.y)
				top = m_extent.hi.y;

			if (bottom < m_extent.lo.y)
				bottom = m_extent.lo.y;
			else if (bottom > m_extent.hi.y)
				bottom = m_extent.hi.y;

			// keep margin if against edge
			const float topMargin = (to->IsEdge(NORTH)) ? (top + margin) : top;
			const float bottomMargin = (to->IsEdge(SOUTH)) ? (bottom - margin) : bottom;

			// limit y to within portal
			if (fromPos->y < topMargin)
				closePos->y = topMargin;
			else if (fromPos->y > bottomMargin)
				closePos->y = bottomMargin;
			else
				closePos->y = fromPos->y;
		}
	}
	// Return direction from this area to the given point
	NavDirType ComputeDirection(Vector* point) const noexcept
	{
		if (point->x >= m_extent.lo.x && point->x <= m_extent.hi.x)
		{
			if (point->y < m_extent.lo.y)
				return NORTH;
			else if (point->y > m_extent.hi.y)
				return SOUTH;
		}
		else if (point->y >= m_extent.lo.y && point->y <= m_extent.hi.y)
		{
			if (point->x < m_extent.lo.x)
				return WEST;
			else if (point->x > m_extent.hi.x)
				return EAST;
		}

		// find closest direction
		Vector const to = *point - m_center;

		if (std::fabs(to.x) > std::fabs(to.y))
		{
			if (to.x > 0.0f)
				return EAST;
			return WEST;
		}
		else
		{
			if (to.y > 0.0f)
				return SOUTH;
			return NORTH;
		}

		return NUM_DIRECTIONS;
	}
	// for hunting algorithm
	void SetClearedTimestamp(int teamID) noexcept { m_clearedTimestamp[teamID] = gpGlobals->time; }	// set this area's "clear" timestamp to now
	float GetClearedTimestamp(int teamID) const noexcept { return m_clearedTimestamp[teamID]; }			// get time this area was marked "clear"

	// hiding spots
	const HidingSpotList* GetHidingSpotList() const noexcept { return &m_hidingSpotList; }

	// analyze local area neighborhood to find "hiding spots" in this area - for map learning
	void ComputeHidingSpots() noexcept
	{
		struct
		{
			float lo{}, hi{};
		} extent{};

		// "jump areas" cannot have hiding spots
		if (GetAttributes() & NAV_JUMP)
			return;

		std::array<int, NUM_CORNERS> cornerCount{};
		cornerCount.fill(0);

		static constexpr float cornerSize = 20.0f;

		// for each direction, find extents of adjacent areas along the wall
		for (int d = 0; d < NUM_DIRECTIONS; d++)
		{
			extent.lo = 999999.9f;
			extent.hi = -999999.9f;

			bool const isHoriz = (d == NORTH || d == SOUTH) ? true : false;
			for (auto& connect : m_connect[d])
			{
				// if connection is only one-way, it's a "jump down" connection (ie: a discontinuity that may mean cover)
				// ignore it
				if (connect.area->IsConnected(this, Opposite[static_cast<NavDirType>(d)]) == false)
					continue;

				// ignore jump areas
				if (connect.area->GetAttributes() & NAV_JUMP)
					continue;

				if (isHoriz)
				{
					if (connect.area->m_extent.lo.x < extent.lo)
						extent.lo = connect.area->m_extent.lo.x;

					if (connect.area->m_extent.hi.x > extent.hi)
						extent.hi = connect.area->m_extent.hi.x;
				}
				else
				{
					if (connect.area->m_extent.lo.y < extent.lo)
						extent.lo = connect.area->m_extent.lo.y;

					if (connect.area->m_extent.hi.y > extent.hi)
						extent.hi = connect.area->m_extent.hi.y;
				}
			}

			switch (d)
			{
			case NORTH:
				if (extent.lo - m_extent.lo.x >= cornerSize)
					cornerCount[NORTH_WEST]++;

				if (m_extent.hi.x - extent.hi >= cornerSize)
					cornerCount[NORTH_EAST]++;
				break;

			case SOUTH:
				if (extent.lo - m_extent.lo.x >= cornerSize)
					cornerCount[SOUTH_WEST]++;

				if (m_extent.hi.x - extent.hi >= cornerSize)
					cornerCount[SOUTH_EAST]++;
				break;

			case EAST:
				if (extent.lo - m_extent.lo.y >= cornerSize)
					cornerCount[NORTH_EAST]++;

				if (m_extent.hi.y - extent.hi >= cornerSize)
					cornerCount[SOUTH_EAST]++;
				break;

			case WEST:
				if (extent.lo - m_extent.lo.y >= cornerSize)
					cornerCount[NORTH_WEST]++;

				if (m_extent.hi.y - extent.hi >= cornerSize)
					cornerCount[SOUTH_WEST]++;
				break;
			}
		}

		// if a corner count is 2, then it really is a corner (walls on both sides)
		float offset = 12.5f;

		if (cornerCount[NORTH_WEST] == 2)
		{
			Vector pos = *GetCorner(NORTH_WEST) + Vector(offset, offset, 0.0f);

			m_hidingSpotList.push_back(new HidingSpot(&pos, (IsHidingSpotInCover(&pos)) ? HidingSpot::IN_COVER : 0));
		}
		if (cornerCount[NORTH_EAST] == 2)
		{
			Vector pos = *GetCorner(NORTH_EAST) + Vector(-offset, offset, 0.0f);
			if (!IsHidingSpotCollision(&pos))
				m_hidingSpotList.push_back(new HidingSpot(&pos, (IsHidingSpotInCover(&pos)) ? HidingSpot::IN_COVER : 0));
		}
		if (cornerCount[SOUTH_WEST] == 2)
		{
			Vector pos = *GetCorner(SOUTH_WEST) + Vector(offset, -offset, 0.0f);
			if (!IsHidingSpotCollision(&pos))
				m_hidingSpotList.push_back(new HidingSpot(&pos, (IsHidingSpotInCover(&pos)) ? HidingSpot::IN_COVER : 0));
		}
		if (cornerCount[SOUTH_EAST] == 2)
		{
			Vector pos = *GetCorner(SOUTH_EAST) + Vector(-offset, -offset, 0.0f);
			if (!IsHidingSpotCollision(&pos))
				m_hidingSpotList.push_back(new HidingSpot(&pos, (IsHidingSpotInCover(&pos)) ? HidingSpot::IN_COVER : 0));
		}
	}
	// Analyze local area neighborhood to find "sniper spots" for this area
	void ComputeSniperSpots() noexcept
	{
#ifdef CSBOT_QUICKSAVE
		if (cv_bot_quicksave.value > 0.0f)
			return;
#endif
		std::ranges::for_each(m_hidingSpotList, &ClassifySniperSpot);
	}

	SpotEncounter* GetSpotEncounter(const CNavArea* from, const CNavArea* to);	// given the areas we are moving between, return the spots we will encounter
	void ComputeSpotEncounters();							// compute spot encounter data - for map learning

	// danger
	void IncreaseDanger(int teamID, float amount);				// increase the danger of this area for the given team
	float GetDanger(int teamID) noexcept							// return the danger of this area (decays over time)
	{
		DecayDanger();
		return m_danger[teamID];
	}

	float GetSizeX() const { return m_extent.hi.x - m_extent.lo.x; }
	float GetSizeY() const { return m_extent.hi.y - m_extent.lo.y; }

	const Extent* GetExtent() const { return &m_extent; }
	const Vector* GetCenter() const { return &m_center; }
	const Vector* GetCorner(NavCornerType corner) const;

	// approach areas
	struct ApproachInfo
	{
		NavConnect here;			// the approach area
		NavConnect prev;			// the area just before the approach area on the path
		NavTraverseType prevToHereHow;
		NavConnect next;			// the area just after the approach area on the path
		NavTraverseType hereToNextHow;
	};

	const ApproachInfo* GetApproachInfo(int i) const { return &m_approach[i]; }
	int GetApproachInfoCount() const { return m_approachCount; }
	void ComputeApproachAreas();						// determine the set of "approach areas" - for map learning

	// A* pathfinding algorithm
	static void MakeNewMarker()
	{
		if (++m_masterMarker == 0)
			m_masterMarker = 1;
	}
	void Mark() { m_marker = m_masterMarker; }
	qboolean IsMarked() const { return (m_marker == m_masterMarker) ? true : false; }
	void SetParent(CNavArea* parent, NavTraverseType how = NUM_TRAVERSE_TYPES) { m_parent = parent; m_parentHow = how; }
	CNavArea* GetParent() const { return m_parent; }
	NavTraverseType GetParentHow() const { return m_parentHow; }

	// true if on "open list"
	bool IsOpen() const noexcept
	{
		return (m_openMarker == m_masterMarker) ? true : false;
	}
	// add to open list in decreasing value order
	void AddToOpenList() noexcept
	{
		// mark as being on open list for quick check
		m_openMarker = m_masterMarker;

		// if list is empty, add and return
		if (!m_openList)
		{
			m_openList = this;
			this->m_prevOpen = nullptr;
			this->m_nextOpen = nullptr;
			return;
		}

		// insert self in ascending cost order
		CNavArea* area, * last = nullptr;
		for (area = m_openList; area; area = area->m_nextOpen)
		{
			if (this->GetTotalCost() < area->GetTotalCost())
				break;

			last = area;
		}

		if (area)
		{
			// insert before this area
			this->m_prevOpen = area->m_prevOpen;
			if (this->m_prevOpen)
				this->m_prevOpen->m_nextOpen = this;
			else
				m_openList = this;

			this->m_nextOpen = area;
			area->m_prevOpen = this;
		}
		else
		{
			// append to end of list
			last->m_nextOpen = this;

			this->m_prevOpen = last;
			this->m_nextOpen = nullptr;
		}
	}
	// A smaller value has been found, update this area on the open list
	// #PF_TODO: "bubbling" does unnecessary work, since the order of all other nodes will be unchanged - only this node is altered
	void UpdateOnOpenList() noexcept
	{
		// since value can only decrease, bubble this area up from current spot
		while (m_prevOpen && this->GetTotalCost() < m_prevOpen->GetTotalCost())
		{
			// swap position with predecessor
			CNavArea* other = m_prevOpen;
			CNavArea* before = other->m_prevOpen;
			CNavArea* after = this->m_nextOpen;

			this->m_nextOpen = other;
			this->m_prevOpen = before;

			other->m_prevOpen = this;
			other->m_nextOpen = after;

			if (before)
				before->m_nextOpen = this;
			else
				m_openList = this;

			if (after)
				after->m_prevOpen = other;
		}
	}
	void RemoveFromOpenList() noexcept
	{
		if (m_prevOpen)
			m_prevOpen->m_nextOpen = m_nextOpen;
		else
			m_openList = m_nextOpen;

		if (m_nextOpen)
			m_nextOpen->m_prevOpen = m_prevOpen;

		// zero is an invalid marker
		m_openMarker = 0;
	}
	static bool IsOpenListEmpty() noexcept
	{
		return m_openList ? false : true;
	}
	static CNavArea* PopOpenList() noexcept	// remove and return the first element of the open list
	{
		if (m_openList)
		{
			CNavArea* area = m_openList;

			// disconnect from list
			area->RemoveFromOpenList();
			return area;
		}

		return nullptr;
	}

	bool IsClosed() const noexcept	// true if on "closed list"
	{
		if (IsMarked() && !IsOpen())
			return true;

		return false;
	}
	void AddToClosedList() noexcept	// add to the closed list
	{
		Mark();
	}
	static constexpr void RemoveFromClosedList() noexcept
	{
		// since "closed" is defined as visited (marked) and not on open list, do nothing
	}

	// Clears the open and closed lists for a new search
	static void ClearSearchLists() noexcept
	{
		CNavArea::MakeNewMarker();
		m_openList = nullptr;
	}

	void SetTotalCost(float value) { m_totalCost = value; }
	float GetTotalCost() const { return m_totalCost; }

	void SetCostSoFar(float value) { m_costSoFar = value; }
	float GetCostSoFar() const { return m_costSoFar; }

	// editing
	void Draw(std::uint8_t red, std::uint8_t green, std::uint8_t blue, int duration = 50);							// draw area for debugging & editing
	void DrawConnectedAreas();
	void DrawMarkedCorner(NavCornerType corner, std::uint8_t red, std::uint8_t green, std::uint8_t blue, int duration = 50);
	bool SplitEdit(bool splitAlongX, float splitEdge, CNavArea** outAlpha = nullptr, CNavArea** outBeta = nullptr);	// split this area into two areas at the given edge
	bool MergeEdit(CNavArea* adj);											// merge this area and given adjacent area
	bool SpliceEdit(CNavArea* other);										// create a new area between this area and given area
	void RaiseCorner(NavCornerType corner, int amount);						// raise/lower a corner (or all corners if corner == NUM_CORNERS)

	// ladders
	void AddLadderUp(CNavLadder* ladder) { m_ladder[LADDER_UP].push_back(ladder); }
	void AddLadderDown(CNavLadder* ladder) { m_ladder[LADDER_DOWN].push_back(ladder); }

private:
	friend void ConnectGeneratedAreas();
	friend void MergeGeneratedAreas();
	friend void MarkJumpAreas();
	friend bool SaveNavigationMap(const char* filename);
	friend NavErrorType LoadNavigationMap() noexcept;
	friend void DestroyNavigationMap() noexcept;
	friend void DestroyHidingSpots() noexcept;
	friend void StripNavigationAreas();
	friend class CNavAreaGrid;
	friend class CCSBotManager;

	void Initialize() noexcept			// to keep constructors consistent
	{
		m_marker = 0;
		m_parent = nullptr;
		m_parentHow = GO_NORTH;
		m_attributeFlags = 0;
		m_place = 0;

		for (int i = 0; i < MAX_AREA_TEAMS; i++)
		{
			m_danger[i] = 0.0f;
			m_dangerTimestamp[i] = 0.0f;
			m_clearedTimestamp[i] = 0.0f;
		}

		m_approachCount = 0;

		// set an ID for splitting and other interactive editing - loads will overwrite this
		m_id = m_nextID++;

		m_prevHash = nullptr;
		m_nextHash = nullptr;
	}

	static inline bool m_isReset{ false };	// if true, don't bother cleaning up in destructor since everything is going away
	static inline unsigned int m_nextID{ 1 };// used to allocate unique IDs
	unsigned int m_id;					// unique area ID
	Extent m_extent;					// extents of area in world coords (NOTE: lo.z is not necessarily the minimum Z, but corresponds to Z at point (lo.x, lo.y), etc
	Vector m_center;					// centroid of area
	unsigned char m_attributeFlags;		// set of attribute bit flags (see NavAttributeType)
	Place m_place;						// place descriptor

	// height of the implicit corners
	float m_neZ;
	float m_swZ;

	enum { MAX_AREA_TEAMS = 2 };

	// for hunting
	float m_clearedTimestamp[MAX_AREA_TEAMS];	// time this area was last "cleared" of enemies

	// danger
	float m_danger[MAX_AREA_TEAMS];				// danger of this area, allowing bots to avoid areas where they died in the past - zero is no danger
	float m_dangerTimestamp[MAX_AREA_TEAMS];	// time when danger value was set - used for decaying
	void DecayDanger() noexcept
	{
		// one kill == 1.0, which we will forget about in two minutes
		constexpr float decayRate = 1.0f / 120.0f;

		for (int i = 0; i < MAX_AREA_TEAMS; i++)
		{
			float const deltaT = gpGlobals->time - m_dangerTimestamp[i];
			float const decayAmount = decayRate * deltaT;

			m_danger[i] -= decayAmount;
			if (m_danger[i] < 0.0f)
				m_danger[i] = 0.0f;

			// update timestamp
			m_dangerTimestamp[i] = gpGlobals->time;
		}
	}

	// hiding spots
	HidingSpotList m_hidingSpotList;
	bool IsHidingSpotCollision(const Vector* pos) const;	// returns true if an existing hiding spot is too close to given position

	// encounter spots
	SpotEncounterList m_spotEncounterList;			// list of possible ways to move thru this area, and the spots to look at as we do
	void AddSpotEncounters(const CNavArea* from, NavDirType fromDir, const CNavArea* to, NavDirType toDir);

	// approach areas
	enum { MAX_APPROACH_AREAS = 16 };
	ApproachInfo m_approach[MAX_APPROACH_AREAS];
	unsigned char m_approachCount;

	void Strip();						// remove "analyzed" data from nav area

	// A* pathfinding algorithm
	static inline unsigned int m_masterMarker{ 1 };
	unsigned int m_marker;				// used to flag the area as visited
	CNavArea* m_parent;					// the area just prior to this on in the search path
	NavTraverseType m_parentHow;		// how we get from parent to us
	float m_totalCost;					// the distance so far plus an estimate of the distance left
	float m_costSoFar;					// distance travelled so far

	static inline CNavArea* m_openList{};
	CNavArea* m_nextOpen, * m_prevOpen;		// only valid if m_openMarker == m_masterMarker
	unsigned int m_openMarker;				// if this equals the current marker value, we are on the open list

	// connections to adjacent areas
	NavConnectList m_connect[NUM_DIRECTIONS];			// a list of adjacent areas for each direction
	NavLadderList m_ladder[NUM_LADDER_DIRECTIONS];		// list of ladders leading up and down from this area

	CNavNode* m_node[NUM_CORNERS];						// nav nodes at each corner of the area

	void FinishMerge(CNavArea* adjArea);				// recompute internal data once nodes have been adjusted during merge
	void MergeAdjacentConnections(CNavArea* adjArea);	// for merging with "adjArea" - pick up all of "adjArea"s connections
	void AssignNodes(CNavArea* area);					// assign internal nodes to the given area
	void FinishSplitEdit(CNavArea* newArea, NavDirType ignoreEdge);		// given the portion of the original area, update its internal data

	NavAreaList m_overlapList;							// list of areas that overlap this area
	void OnDestroyNotify(CNavArea* dead) noexcept		// invoked when given area is going away
	{
		NavConnect con;
		con.area = dead;
		for (int d = 0; d < NUM_DIRECTIONS; d++)
			m_connect[d].remove(con);

		m_overlapList.remove(dead);
	}

	CNavArea* m_prevHash, * m_nextHash;					// for hash table in CNavAreaGrid
};


#pragma region CNavAreaGrid
void CNavAreaGrid::AddNavArea(CNavArea* area) noexcept
{
	// add to grid
	const Extent* extent = area->GetExtent();

	int const loX = WorldToGridX(extent->lo.x);
	int const loY = WorldToGridY(extent->lo.y);
	int const hiX = WorldToGridX(extent->hi.x);
	int const hiY = WorldToGridY(extent->hi.y);

	for (int y = loY; y <= hiY; y++)
	{
		for (int x = loX; x <= hiX; x++)
			m_grid[x + y * m_gridSizeX].push_back(const_cast<CNavArea*>(area));
	}

	// add to hash table
	auto const key = ComputeHashKey(area->GetID());

	if (m_hashTable[key])
	{
		// add to head of list in this slot
		area->m_prevHash = nullptr;
		area->m_nextHash = m_hashTable[key];
		m_hashTable[key]->m_prevHash = area;
		m_hashTable[key] = area;
	}
	else
	{
		// first entry in this slot
		m_hashTable[key] = area;
		area->m_nextHash = nullptr;
		area->m_prevHash = nullptr;
	}

	m_areaCount++;
}

void CNavAreaGrid::RemoveNavArea(CNavArea* area) noexcept
{
	// add to grid
	const Extent* extent = area->GetExtent();

	int const loX = WorldToGridX(extent->lo.x);
	int const loY = WorldToGridY(extent->lo.y);
	int const hiX = WorldToGridX(extent->hi.x);
	int const hiY = WorldToGridY(extent->hi.y);

	for (int y = loY; y <= hiY; y++)
	{
		for (int x = loX; x <= hiX; x++)
		{
			m_grid[x + y * m_gridSizeX].remove(area);
		}
	}

	// remove from hash table
	int const key = ComputeHashKey(area->GetID());

	if (area->m_prevHash)
	{
		area->m_prevHash->m_nextHash = area->m_nextHash;
	}
	else
	{
		// area was at start of list
		m_hashTable[key] = area->m_nextHash;

		if (m_hashTable[key])
			m_hashTable[key]->m_prevHash = nullptr;
	}

	if (area->m_nextHash)
	{
		area->m_nextHash->m_prevHash = area->m_prevHash;
	}

	m_areaCount--;
}

CNavArea* CNavAreaGrid::GetNavArea(const Vector* pos, float const beneathLimit) const noexcept
{
	if (!m_grid)
		return nullptr;

	// get list in cell that contains position
	int const x = WorldToGridX(pos->x);
	int const y = WorldToGridY(pos->y);
	NavAreaList* list = &m_grid[x + y * m_gridSizeX];

	// search cell list to find correct area
	CNavArea* use = nullptr;
	float useZ = -99999999.9f;
	Vector const testPos = *pos + Vector(0, 0, 5);

	for (NavAreaList::iterator iter = list->begin(); iter != list->end(); iter++)
	{
		CNavArea* area = (*iter);

		// check if position is within 2D boundaries of this area
		if (area->IsOverlapping(&testPos))
		{
			// project position onto area to get Z
			float z = area->GetZ(&testPos);

			// if area is above us, skip it
			if (z > testPos.z)
				continue;

			// if area is too far below us, skip it
			if (z < pos->z - beneathLimit)
				continue;

			// if area is higher than the one we have, use this instead
			if (z > useZ)
			{
				use = area;
				useZ = z;
			}
		}
	}

	return use;
}

CNavArea* CNavAreaGrid::GetNavAreaByID(unsigned int id) const noexcept
{
	if (id == 0)
		return nullptr;

	int key = ComputeHashKey(id);
	for (CNavArea* area = m_hashTable[key]; area; area = area->m_nextHash)
	{
		if (area->GetID() == id)
			return area;
	}

	return nullptr;
}

CNavArea* CNavAreaGrid::GetNearestNavArea(const Vector* pos, bool anyZ) const noexcept
{
	if (!m_grid)
		return nullptr;

	CNavArea* close = nullptr;
	double closeDistSq = 100000000.0f;

	// quick check
	close = GetNavArea(pos);
	if (close)
		return close;

	// ensure source position is well behaved
	Vector source;
	source.x = pos->x;
	source.y = pos->y;

	if (GetGroundHeight(pos, &source.z) == false)
		return nullptr;

	source.z += HalfHumanHeight;

	// #PF_TODO: Step incrementally using grid for speed

	// find closest nav area
	for (NavAreaList::iterator iter = TheNavAreaList.begin(); iter != TheNavAreaList.end(); iter++)
	{
		CNavArea* area = (*iter);

		Vector areaPos;
		area->GetClosestPointOnArea(&source, &areaPos);

		auto const distSq = (areaPos - source).LengthSquared();

		// keep the closest area
		if (distSq < closeDistSq)
		{
			// check LOS to area
			if (!anyZ)
			{
				TraceResult result;
				g_engfuncs.pfnTraceLine(source, areaPos + Vector(0, 0, HalfHumanHeight), ignore_monsters | ignore_glass, nullptr, &result);
				if (result.flFraction != 1.0f)
					continue;
			}

			closeDistSq = distSq;
			close = area;
		}
	}

	return close;
}

Place CNavAreaGrid::GetPlace(const Vector* pos) const noexcept
{
	CNavArea* area = GetNearestNavArea(pos, true);
	if (area)
	{
		return area->GetPlace();
	}

	return UNDEFINED_PLACE;
}
#pragma endregion CNavAreaGrid

#pragma region MISC
void DestroyHidingSpots() noexcept
{
	// remove all hiding spot references from the nav areas
	for (auto&& area : TheNavAreaList)
		area->m_hidingSpotList.clear();

	HidingSpot::m_nextID = 0;

	// free all the HidingSpots
	for (auto&& spot : TheHidingSpotList)
		delete spot;

	TheHidingSpotList.clear();
}

void DestroyLadders() noexcept
{
	while (!TheNavLadderList.empty())
	{
		CNavLadder* ladder = TheNavLadderList.front();
		TheNavLadderList.pop_front();
		delete ladder;
	}
}

// Free navigation map data
export void DestroyNavigationMap() noexcept
{
	CNavArea::m_isReset = true;

	// remove each element of the list and delete them
	while (!TheNavAreaList.empty())
	{
		CNavArea* area = TheNavAreaList.front();
		TheNavAreaList.pop_front();
		delete area;
	}

	CNavArea::m_isReset = false;

	// destroy ladder representations
	DestroyLadders();

	// destroy all hiding spots
	DestroyHidingSpots();

	// destroy navigation nodes created during map learning
	CNavNode* node, * next;
	for (node = CNavNode::m_list; node; node = next)
	{
		next = node->m_next;
		delete node;
	}

	CNavNode::m_list = nullptr;

	// reset the grid
	TheNavAreaGrid.Reset();
}

void ClassifySniperSpot(HidingSpot* spot) noexcept
{
	// assume we are crouching
	Vector eye = *spot->GetPosition() + Vector(0, 0, HalfHumanHeight);
	Vector walkable;
	TraceResult result;

	Extent sniperExtent;
	double farthestRangeSq = 0;
	constexpr auto minSniperRangeSq = 1000.0 * 1000.0;
	bool found = false;

	for (NavAreaList::iterator iter = TheNavAreaList.begin(); iter != TheNavAreaList.end(); iter++)
	{
		CNavArea* area = (*iter);

		const Extent* extent = area->GetExtent();

		// scan this area
		for (walkable.y = extent->lo.y + GenerationStepSize / 2.0f; walkable.y < extent->hi.y; walkable.y += GenerationStepSize)
		{
			for (walkable.x = extent->lo.x + GenerationStepSize / 2.0f; walkable.x < extent->hi.x; walkable.x += GenerationStepSize)
			{
				walkable.z = area->GetZ(&walkable) + HalfHumanHeight;

				// check line of sight
				g_engfuncs.pfnTraceLine(eye, walkable, ignore_monsters | dont_ignore_glass, nullptr, &result);

				if (result.flFraction == 1.0f && !result.fStartSolid)
				{
					// can see this spot

					// keep track of how far we can see
					auto rangeSq = (eye - walkable).LengthSquared();
					if (rangeSq > farthestRangeSq)
					{
						farthestRangeSq = rangeSq;

						if (rangeSq >= minSniperRangeSq)
						{
							// this is a sniper spot
							// determine how good of a sniper spot it is by keeping track of the snipable area
							if (found)
							{
								if (walkable.x < sniperExtent.lo.x)
									sniperExtent.lo.x = walkable.x;
								if (walkable.x > sniperExtent.hi.x)
									sniperExtent.hi.x = walkable.x;

								if (walkable.y < sniperExtent.lo.y)
									sniperExtent.lo.y = walkable.y;
								if (walkable.y > sniperExtent.hi.y)
									sniperExtent.hi.y = walkable.y;
							}
							else
							{
								sniperExtent.lo = walkable;
								sniperExtent.hi = walkable;
								found = true;
							}
						}
					}
				}
			}
		}
	}

	if (found)
	{
		// if we can see a large snipable area, it is an "ideal" spot
		float snipableArea = sniperExtent.Area();

		const float minIdealSniperArea = 200.0f * 200.0f;
		const float longSniperRangeSq = 1500.0f * 1500.0f;

		if (snipableArea >= minIdealSniperArea || farthestRangeSq >= longSniperRangeSq)
			spot->SetFlags(HidingSpot::IDEAL_SNIPER_SPOT);
		else
			spot->SetFlags(HidingSpot::GOOD_SNIPER_SPOT);
	}
}

inline CNavArea* FindFirstAreaInDirection(const Vector* start, NavDirType dir, float range, float beneathLimit, CBaseEntity* traceIgnore = nullptr, Vector* closePos = nullptr) noexcept
{
	CNavArea* area = nullptr;
	Vector pos = *start;
	auto const end = int((range / GenerationStepSize) + 0.5f);

	for (int i = 1; i <= end; i++)
	{
		AddDirectionVector(&pos, dir, GenerationStepSize);

		// make sure we dont look thru the wall
		TraceResult result;

		if (traceIgnore)
			g_engfuncs.pfnTraceLine(*start, pos, ignore_monsters | dont_ignore_glass, traceIgnore->edict(), &result);
		else
			g_engfuncs.pfnTraceLine(*start, pos, ignore_monsters | dont_ignore_glass, nullptr, &result);

		if (result.flFraction != 1.0f)
			break;

		area = TheNavAreaGrid.GetNavArea(&pos, beneathLimit);

		if (area)
		{
			if (closePos)
			{
				closePos->x = pos.x;
				closePos->y = pos.y;
				closePos->z = area->GetZ(pos.x, pos.y);
			}

			break;
		}
	}

	return area;
}

#pragma endregion MISC

#pragma region NAV_FILE

// For each ladder in the map, create a navigation representation of it.
void BuildLadders() noexcept
{
	// remove any left-over ladders
	DestroyLadders();

	TraceResult result;
	for (CBaseEntity* pEntity :
		Query::all_nonplayer_entities()
		| std::views::filter([](CBaseEntity* e) noexcept { return !strcmp("func_ladder", STRING(e->pev->classname)); })
		)
	{
		CNavLadder* ladder = new CNavLadder;

		// compute top & bottom of ladder
		ladder->m_top.x = (pEntity->pev->absmin.x + pEntity->pev->absmax.x) / 2.0f;
		ladder->m_top.y = (pEntity->pev->absmin.y + pEntity->pev->absmax.y) / 2.0f;
		ladder->m_top.z = pEntity->pev->absmax.z;

		ladder->m_bottom.x = ladder->m_top.x;
		ladder->m_bottom.y = ladder->m_top.y;
		ladder->m_bottom.z = pEntity->pev->absmin.z;

		// determine facing - assumes "normal" runged ladder
		float xSize = pEntity->pev->absmax.x - pEntity->pev->absmin.x;
		float ySize = pEntity->pev->absmax.y - pEntity->pev->absmin.y;

		if (xSize > ySize)
		{
			// ladder is facing north or south - determine which way
			// "pull in" traceline from bottom and top in case ladder abuts floor and/or ceiling
			Vector from = ladder->m_bottom + Vector(0.0f, GenerationStepSize, GenerationStepSize);
			Vector to = ladder->m_top + Vector(0.0f, GenerationStepSize, -GenerationStepSize);

			g_engfuncs.pfnTraceLine(from, to, ignore_monsters | dont_ignore_glass, pEntity->edict(), &result);

			if (result.flFraction != 1.0f || result.fStartSolid)
				ladder->m_dir = NORTH;
			else
				ladder->m_dir = SOUTH;
		}
		else
		{
			// ladder is facing east or west - determine which way
			Vector from = ladder->m_bottom + Vector(GenerationStepSize, 0.0f, GenerationStepSize);
			Vector to = ladder->m_top + Vector(GenerationStepSize, 0.0f, -GenerationStepSize);

			g_engfuncs.pfnTraceLine(from, to, ignore_monsters | dont_ignore_glass, pEntity->edict(), &result);

			if (result.flFraction != 1.0f || result.fStartSolid)
				ladder->m_dir = WEST;
			else
				ladder->m_dir = EAST;
		}

		// adjust top and bottom of ladder to make sure they are reachable
		// (cs_office has a crate right in front of the base of a ladder)
		auto const vecDiff = ladder->m_top - ladder->m_bottom;
		auto const along = vecDiff.Normalize();
		auto const length = (float)vecDiff.Length();

		Vector on, out;
		constexpr float minLadderClearance = 32.0f;

		// adjust bottom to bypass blockages
		constexpr float inc = 10.0f;
		float t{};

		for (t = 0.0f; t <= length; t += inc)
		{
			on = ladder->m_bottom + t * along;

			out = on;
			AddDirectionVector(&out, ladder->m_dir, minLadderClearance);
			g_engfuncs.pfnTraceLine(on, out, ignore_monsters | dont_ignore_glass, pEntity->edict(), &result);

			if (result.flFraction == 1.0f && !result.fStartSolid)
			{
				// found viable ladder bottom
				ladder->m_bottom = on;
				break;
			}
		}

		// adjust top to bypass blockages
		for (t = 0.0f; t <= length; t += inc)
		{
			on = ladder->m_top - t * along;

			out = on;
			AddDirectionVector(&out, ladder->m_dir, minLadderClearance);
			g_engfuncs.pfnTraceLine(on, out, ignore_monsters | dont_ignore_glass, pEntity->edict(), &result);

			if (result.flFraction == 1.0f && !result.fStartSolid)
			{
				// found viable ladder top
				ladder->m_top = on;
				break;
			}
		}

		ladder->m_length = (float)(ladder->m_top - ladder->m_bottom).Length();
		DirectionToVector2D(ladder->m_dir, &ladder->m_dirVector);

		ladder->m_entity = pEntity;
		constexpr float nearLadderRange = 75.0f;

		// Find naviagtion area at bottom of ladder
		// get approximate postion of player on ladder

		Vector center = ladder->m_bottom + Vector(0, 0, GenerationStepSize);
		AddDirectionVector(&center, ladder->m_dir, HalfHumanWidth);

		ladder->m_bottomArea = TheNavAreaGrid.GetNearestNavArea(&center, true);
		if (!ladder->m_bottomArea)
		{
			g_engfuncs.pfnAlertMessage(at_console, "ERROR: Unconnected ladder bottom at (%g, %g, %g)\n", ladder->m_bottom.x, ladder->m_bottom.y, ladder->m_bottom.z);
		}
		else
		{
			// store reference to ladder in the area
			ladder->m_bottomArea->AddLadderUp(ladder);
		}

		// Find adjacent navigation areas at the top of the ladder
		// get approximate postion of player on ladder
		center = ladder->m_top + Vector(0, 0, GenerationStepSize);
		AddDirectionVector(&center, ladder->m_dir, HalfHumanWidth);

		// find "ahead" area
		ladder->m_topForwardArea = FindFirstAreaInDirection(&center, Opposite[ladder->m_dir], nearLadderRange, 120.0f, pEntity);
		if (ladder->m_topForwardArea == ladder->m_bottomArea)
			ladder->m_topForwardArea = nullptr;

		// find "left" area
		ladder->m_topLeftArea = FindFirstAreaInDirection(&center, DirectionLeft(ladder->m_dir), nearLadderRange, 120.0f, pEntity);
		if (ladder->m_topLeftArea == ladder->m_bottomArea)
			ladder->m_topLeftArea = nullptr;

		// find "right" area
		ladder->m_topRightArea = FindFirstAreaInDirection(&center, DirectionRight(ladder->m_dir), nearLadderRange, 120.0f, pEntity);
		if (ladder->m_topRightArea == ladder->m_bottomArea)
			ladder->m_topRightArea = nullptr;

		// find "behind" area - must look farther, since ladder is against the wall away from this area
		ladder->m_topBehindArea = FindFirstAreaInDirection(&center, ladder->m_dir, 2.0f * nearLadderRange, 120.0f, pEntity);
		if (ladder->m_topBehindArea == ladder->m_bottomArea)
			ladder->m_topBehindArea = nullptr;

		// can't include behind area, since it is not used when going up a ladder
		if (!ladder->m_topForwardArea && !ladder->m_topLeftArea && !ladder->m_topRightArea)
			g_engfuncs.pfnAlertMessage(at_console, "ERROR: Unconnected ladder top at (%g, %g, %g)\n", ladder->m_top.x, ladder->m_top.y, ladder->m_top.z);

		// store reference to ladder in the area(s)
		if (ladder->m_topForwardArea)
			ladder->m_topForwardArea->AddLadderDown(ladder);

		if (ladder->m_topLeftArea)
			ladder->m_topLeftArea->AddLadderDown(ladder);

		if (ladder->m_topRightArea)
			ladder->m_topRightArea->AddLadderDown(ladder);

		if (ladder->m_topBehindArea)
			ladder->m_topBehindArea->AddLadderDown(ladder);

		// adjust top of ladder to highest connected area
		float topZ = -99999.9f;
		bool topAdjusted = false;

		CNavArea* topAreaList[NUM_CORNERS];
		topAreaList[NORTH_WEST] = ladder->m_topForwardArea;
		topAreaList[NORTH_EAST] = ladder->m_topLeftArea;
		topAreaList[SOUTH_EAST] = ladder->m_topRightArea;
		topAreaList[SOUTH_WEST] = ladder->m_topBehindArea;

		for (int a = 0; a < NUM_CORNERS; a++)
		{
			CNavArea* topArea = topAreaList[a];
			if (!topArea)
				continue;

			Vector close;
			topArea->GetClosestPointOnArea(&ladder->m_top, &close);
			if (topZ < close.z)
			{
				topZ = close.z;
				topAdjusted = true;
			}
		}

		if (topAdjusted)
			ladder->m_top.z = topZ;

		// Determine whether this ladder is "dangling" or not
		// "Dangling" ladders are too high to go up
		ladder->m_isDangling = false;
		if (ladder->m_bottomArea)
		{
			Vector bottomSpot;
			ladder->m_bottomArea->GetClosestPointOnArea(&ladder->m_bottom, &bottomSpot);
			if (ladder->m_bottom.z - bottomSpot.z > HumanHeight)
				ladder->m_isDangling = true;
		}

		// add ladder to global list
		TheNavLadderList.push_back(ladder);
	}
}

// to help identify nav files
#define NAV_MAGIC_NUMBER     0xFEEDFACE

export NavErrorType LoadNavigationMap() noexcept
{
	// since the navigation map is destroyed on map change,
	// if it exists it has already been loaded for this map
	if (!TheNavAreaList.empty())
		return NAV_OK;

	// nav filename is derived from map filename
	auto const filename = std::format("maps\\{}.nav", STRING(gpGlobals->mapname));

	// free previous navigation map data
	DestroyNavigationMap();
	placeDirectory.Reset();

	CNavArea::m_nextID = 1;

	SteamFile navFile(filename.c_str());

	if (!navFile.IsValid())
		return NAV_CANT_ACCESS_FILE;

	// check magic number
	bool result;
	unsigned int magic;
	result = navFile.Read(&magic, sizeof(unsigned int));
	if (!result || magic != NAV_MAGIC_NUMBER)
	{
		CONSOLE_ECHO("ERROR: Invalid navigation file '%s'.\n", filename);
		return NAV_INVALID_FILE;
	}

	// read file version number
	unsigned int version;
	result = navFile.Read(&version, sizeof(unsigned int));
	if (!result || version > NAV_VERSION)
	{
		CONSOLE_ECHO("ERROR: Unknown navigation file version.\n");
		return NAV_BAD_FILE_VERSION;
	}

	if (version >= 4)
	{
		// get size of source bsp file and verify that the bsp hasn't changed
		unsigned int saveBspSize;
		navFile.Read(&saveBspSize, sizeof(unsigned int));

		// verify size
		auto const bspFilename = std::format("maps\\{}.bsp", STRING(gpGlobals->mapname));
		unsigned int bspSize = (unsigned int)g_engfuncs.pfnGetFileSize(bspFilename.c_str());
		if (bspSize != saveBspSize)
		{
			// this nav file is out of date for this bsp file
			auto const msg = "*** WARNING ***\nThe AI navigation data is from a different version of this map.\nThe CPU players will likely not perform well.\n";
			CONSOLE_ECHO("\n-----------------\n");
			CONSOLE_ECHO(msg);
			CONSOLE_ECHO("-----------------\n\n");
		}
	}

	// load Place directory
	if (version >= NAV_VERSION)
	{
		placeDirectory.Load(&navFile);
	}

	// get number of areas
	unsigned int count;
	result = navFile.Read(&count, sizeof(unsigned int));

	Extent extent;
	extent.lo.x = 9999999999.9f;
	extent.lo.y = 9999999999.9f;
	extent.hi.x = -9999999999.9f;
	extent.hi.y = -9999999999.9f;

	// load the areas and compute total extent
	for (unsigned int i = 0; i < count; i++)
	{
		CNavArea* area = new CNavArea;
		area->Load(&navFile, version);
		TheNavAreaList.push_back(area);

		const Extent* areaExtent = area->GetExtent();

		// check validity of nav area
		if (areaExtent->lo.x >= areaExtent->hi.x || areaExtent->lo.y >= areaExtent->hi.y) {
			CONSOLE_ECHO("WARNING: Degenerate Navigation Area #%d at ( %g, %g, %g )\n", area->GetID(), area->m_center.x, area->m_center.y, area->m_center.z);
		}

		if (areaExtent->lo.x < extent.lo.x)
			extent.lo.x = areaExtent->lo.x;

		if (areaExtent->lo.y < extent.lo.y)
			extent.lo.y = areaExtent->lo.y;

		if (areaExtent->hi.x > extent.hi.x)
			extent.hi.x = areaExtent->hi.x;

		if (areaExtent->hi.y > extent.hi.y)
			extent.hi.y = areaExtent->hi.y;
	}

	// add the areas to the grid
	TheNavAreaGrid.Initialize(extent.lo.x, extent.hi.x, extent.lo.y, extent.hi.y);

	for (auto area : TheNavAreaList) {
		TheNavAreaGrid.AddNavArea(area);
	}

	// allow areas to connect to each other, etc
	for (auto area : TheNavAreaList) {
		area->PostLoad();
	}

#ifdef CSBOT_PHRASE
	// load legacy location file (Places)
	if (version < NAV_VERSION)
	{
		LoadLocationFile(filename);
	}
#endif

	// Set up all the ladders
	BuildLadders();
	return NAV_OK;
}

#pragma endregion NAV_FILE
