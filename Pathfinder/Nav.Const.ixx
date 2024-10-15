module;

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

export module Nav:Const;

import std;
import hlsdk;

import CBase;

#pragma region steam_util.h
export class SteamFile
{
public:
	SteamFile(const char* filename) noexcept
	{
		m_fileData = (std::byte*)g_engfuncs.pfnLoadFileForMe(filename, &m_fileDataLength);
		m_cursor = m_fileData;
		m_bytesLeft = m_fileDataLength;
	}
	~SteamFile() noexcept
	{
		if (m_fileData)
		{
			g_engfuncs.pfnFreeFile(m_fileData);
			m_fileData = nullptr;
		}
	}

	bool IsValid() const noexcept { return (m_fileData) ? true : false; }
	bool Read(void* data, int length) noexcept
	{
		if (length > m_bytesLeft || !m_cursor || m_bytesLeft <= 0)
			return false;

		std::byte* readCursor = static_cast<std::byte*>(data);
		for (int i = 0; i < length; i++)
		{
			*readCursor++ = *m_cursor++;
			m_bytesLeft--;
		}

		return true;
	}

private:
	std::byte* m_fileData{};
	int m_fileDataLength{};

	std::byte* m_cursor{};
	int m_bytesLeft{};
};
#pragma endregion steam_util.h

#pragma region bot_util.h
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

void CONSOLE_ECHO(const char* pszMsg, ...) noexcept
{
	va_list argptr;
	static char szStr[1024]{};

	va_start(argptr, pszMsg);
	_vsnprintf(szStr, sizeof(szStr) - 1, pszMsg, argptr);
	va_end(argptr);

	g_engfuncs.pfnServerPrint(szStr);
}

// Return the local player
inline CBasePlayer* UTIL_GetLocalPlayer() noexcept
{
	// no "local player" if this is a dedicated server or a single player game
	if (g_engfuncs.pfnIsDedicatedServer())
	{
		// just try to find any player
		for (int iIndex = 1; iIndex <= gpGlobals->maxClients; iIndex++)
		{
			auto const pPlayer = ent_cast<CBasePlayer*>(iIndex);

			if (!pPlayer || pPlayer->IsDormant())
				continue;

			if (FStrEq(STRING(pPlayer->pev->netname), ""))
				continue;

			if (pPlayer->IsBot())
				continue;

			if (pPlayer->m_iTeam != TEAM_SPECTATOR && pPlayer->m_iTeam != TEAM_CT)
				continue;

			if (pPlayer->m_iJoiningState != JoinState::JOINED)
				continue;

			return pPlayer;
		}

		return nullptr;
	}

	return ent_cast<CBasePlayer*>(1);
}

// Simple class for counting down a short interval of time
export struct CountdownTimer
{
	inline void Reset() noexcept { m_timestamp = gpGlobals->time + m_duration; }

	inline void Start(float duration) noexcept { m_timestamp = gpGlobals->time + duration; m_duration = duration; }
	inline bool HasStarted() const noexcept { return (m_timestamp > 0.0f); }

	inline void Invalidate() noexcept { m_timestamp = -1.0f; }
	inline bool IsElapsed() const noexcept { return (gpGlobals->time > m_timestamp); }

private:
	float m_duration{ 0.f };
	float m_timestamp{ -1.f };
};

// Simple class for tracking intervals of game time
export struct IntervalTimer
{
	inline void Reset() noexcept { m_timestamp = gpGlobals->time; }
	inline void Start() noexcept { m_timestamp = gpGlobals->time; }
	inline void Invalidate() noexcept { m_timestamp = -1.0f; }

	inline bool HasStarted() const noexcept { return (m_timestamp > 0.0f); }

	// if not started, elapsed time is very large
	inline float GetElapsedTime()             const noexcept { return (HasStarted()) ? (gpGlobals->time - m_timestamp) : 99999.9f; }
	inline bool IsLessThen(float duration)    const noexcept { return (gpGlobals->time - m_timestamp < duration) ? true : false; }
	inline bool IsGreaterThen(float duration) const noexcept { return (gpGlobals->time - m_timestamp > duration) ? true : false; }

private:
	float m_timestamp{ -1.f };
};


#pragma endregion bot_util.h

#pragma region Extent
export struct Extent final
{
	Vector lo{};
	Vector hi{};

	constexpr float SizeX() const noexcept { return hi.x - lo.x; }
	constexpr float SizeY() const noexcept { return hi.y - lo.y; }
	constexpr float SizeZ() const noexcept { return hi.z - lo.z; }
	constexpr float Area()  const noexcept { return SizeX() * SizeY(); }

	// return true if 'pos' is inside of this extent
	constexpr bool Contains(const Vector* pos) const noexcept
	{
		return (pos->x >= lo.x && pos->x <= hi.x &&
			pos->y >= lo.y && pos->y <= hi.y &&
			pos->z >= lo.z && pos->z <= hi.z);
	}
};
#pragma endregion Extent

#pragma region Place
// A place is a named group of navigation areas
export using Place = unsigned int;

// ie: "no place"
export inline constexpr Place UNDEFINED_PLACE = 0;
export inline constexpr Place ANY_PLACE = 0xFFFF;

export inline constexpr std::string_view g_rgszDefaultPlaceNames[] =
{
	"BombsiteA",
	"BombsiteB",
	"BombsiteC",
	"Hostages",
	"HostageRescueZone",
	"VipRescueZone",
	"CTSpawn",
	"TSpawn",
	"Bridge",
	"Middle",
	"House",
	"Apartment",
	"Apartments",
	"Market",
	"Sewers",
	"Tunnel",
	"Ducts",
	"Village",
	"Roof",
	"Upstairs",
	"Downstairs",
	"Basement",
	"Crawlspace",
	"Kitchen",
	"Inside",
	"Outside",
	"Tower",
	"WineCellar",
	"Garage",
	"Courtyard",
	"Water",
	"FrontDoor",
	"BackDoor",
	"SideDoor",
	"BackWay",
	"FrontYard",
	"BackYard",
	"SideYard",
	"Lobby",
	"Vault",
	"Elevator",
	"DoubleDoors",
	"SecurityDoors",
	"LongHall",
	"SideHall",
	"FrontHall",
	"BackHall",
	"MainHall",
	"FarSide",
	"Windows",
	"Window",
	"Attic",
	"StorageRoom",
	"ProjectorRoom",
	"MeetingRoom",
	"ConferenceRoom",
	"ComputerRoom",
	"BigOffice",
	"LittleOffice",
	"Dumpster",
	"Airplane",
	"Underground",
	"Bunker",
	"Mines",
	"Front",
	"Back",
	"Rear",
	"Side",
	"Ramp",
	"Underpass",
	"Overpass",
	"Stairs",
	"Ladder",
	"Gate",
	"GateHouse",
	"LoadingDock",
	"GuardHouse",
	"Entrance",
	"VendingMachines",
	"Loft",
	"Balcony",
	"Alley",
	"BackAlley",
	"SideAlley",
	"FrontRoom",
	"BackRoom",
	"SideRoom",
	"Crates",
	"Truck",
	"Bedroom",
	"FamilyRoom",
	"Bathroom",
	"LivingRoom",
	"Den",
	"Office",
	"Atrium",
	"Entryway",
	"Foyer",
	"Stairwell",
	"Fence",
	"Deck",
	"Porch",
	"Patio",
	"Wall"
};

export Place Place_NameToID(const char* name) noexcept
{
	for (Place place = 0; place < std::ssize(g_rgszDefaultPlaceNames); place++)
	{
		if (!_stricmp(g_rgszDefaultPlaceNames[place].data(), name))
			return place + 1;
	}

	return UNDEFINED_PLACE;
}

export auto Place_IDToName(Place place) noexcept -> std::optional<std::string_view>
{
	if (place <= 0 || place > std::ssize(g_rgszDefaultPlaceNames))
		return std::nullopt;

	return g_rgszDefaultPlaceNames[place - 1];
}

#pragma endregion Place

#pragma region WalkingTest

export enum EWalkThrough
{
	WALK_THRU_DOORS = 0x01,
	WALK_THRU_BREAKABLES = 0x02,

	WALK_THRU_EVERYTHING = (WALK_THRU_DOORS | WALK_THRU_BREAKABLES),
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

export bool GetGroundHeight(const Vector& pos, float* height, Vector* normal = nullptr) noexcept
{
	Vector const to{ pos.x, pos.y, pos.z - 9999.f };

	TraceResult result{};
	edict_t* ignore = nullptr;

	static constexpr float maxOffset = 100.0f;
	static constexpr float inc = 10.0f;

	struct GroundLayerInfo
	{
		float ground{};
		Vector normal{};
	};
	std::array<GroundLayerInfo, 16> layer{};

	size_t layerCount = 0;
	for (auto offset = 1.0f; offset < maxOffset; offset += inc)
	{
		auto from = pos + Vector(0, 0, offset);

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
				++layerCount;

				if (layerCount == layer.size())
					break;
			}
		}
	}

	if (layerCount == 0)
		return false;

	size_t i{};
	for (; i < layerCount - 1; i++)
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

export inline constexpr float GenerationStepSize = 25.f;  // (30) was 20, but bots can't always fit
export inline constexpr float StepHeight = 18.0f; // if delta Z is greater than this, we have to jump to get up
export inline constexpr float JumpHeight = 41.8f; // if delta Z is less than this, we can jump up on it
export inline constexpr float JumpCrouchHeight = 58.0f; // (48) if delta Z is less than or equal to this, we can jumpcrouch up on it


#pragma endregion WalkingTest

#pragma region CNavArea

export enum NavCornerType
{
	NORTH_WEST = 0,
	NORTH_EAST,
	SOUTH_EAST,
	SOUTH_WEST,

	NUM_CORNERS
};

export enum NavAttributeType
{
	NAV_CROUCH = 0x01, // must crouch to use this node/area
	NAV_JUMP = 0x02, // must jump to traverse this area
	NAV_PRECISE = 0x04, // do not adjust for obstacles, just move along area
	NAV_NO_JUMP = 0x08, // inhibit discontinuity jumping
};

// Defines possible ways to move from one area to another
export enum NavTraverseType
{
	// NOTE: First 4 directions MUST match NavDirType
	GO_NORTH = 0,
	GO_EAST,
	GO_SOUTH,
	GO_WEST,
	GO_LADDER_UP,
	GO_LADDER_DOWN,
	GO_JUMP,

	NUM_TRAVERSE_TYPES	// Terminus
};

export union NavConnect
{
	std::uintptr_t id{};
	class CNavArea* area;

	bool operator==(const NavConnect& other) const noexcept { return (area == other.area) ? true : false; }
};

export using NavConnectList = std::list<NavConnect>;

export struct Ray
{
	Vector from{};
	Vector to{};
};

export enum LadderDirectionType
{
	LADDER_UP = 0,
	LADDER_DOWN,
	NUM_LADDER_DIRECTIONS
};

export enum RouteType
{
	FASTEST_ROUTE,
	SAFEST_ROUTE,
};

#pragma endregion CNavArea

#pragma region NavDir

export enum NavDirType
{
	NORTH = 0,
	EAST,
	SOUTH,
	WEST,

	NUM_DIRECTIONS
};

export inline constexpr std::array<NavDirType, NUM_DIRECTIONS> Opposite = { SOUTH, WEST, NORTH, EAST };

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


#pragma endregion NavDir

#pragma region nav_file

export enum NavErrorType
{
	NAV_OK,
	NAV_CANT_ACCESS_FILE,
	NAV_INVALID_FILE,
	NAV_BAD_FILE_VERSION,
	NAV_CORRUPT_DATA,
};

// version
// 1 = hiding spots as plain vector array
// 2 = hiding spots as HidingSpot objects
// 3 = Encounter spots use HidingSpot ID's instead of storing vector again
// 4 = Includes size of source bsp file to verify nav data correlation
// ---- Beta Release at V4 -----
// 5 = Added Place info
export inline constexpr auto NAV_VERSION = 5;

// The 'place directory' is used to save and load places from
// nav files in a size-efficient manner that also allows for the
// order of the place ID's to change without invalidating the
// nav files.
//
// The place directory is stored in the nav file as a list of
// place name strings.  Each nav area then contains an index
// into that directory, or zero if no place has been assigned to
// that area.
export class PlaceDirectory
{
public:
	typedef unsigned short EntryType;

	void Reset() noexcept
	{
		m_directory.clear();
	}

	// return true if this place is already in the directory
	bool IsKnown(Place place) const noexcept
	{
		auto it = std::find(m_directory.begin(), m_directory.end(), place);
		return (it != m_directory.end());
	}

	// return the directory entry corresponding to this Place (0 = no entry)
	EntryType GetEntry(Place place) const noexcept
	{
		if (place == UNDEFINED_PLACE)
			return 0;

		auto it = std::find(m_directory.begin(), m_directory.end(), place);
		if (it == m_directory.end())
		{
			assert(false && "PlaceDirectory::GetEntry failure");
			return 0;
		}

		return 1 + (it - m_directory.begin());
	}

	// add the place to the directory if not already known
	void AddPlace(Place place) noexcept
	{
		if (place == UNDEFINED_PLACE)
			return;

		assert(place < 1000);

		if (IsKnown(place))
			return;

		m_directory.push_back(place);
	}

	// given an entry, return the Place
	Place EntryToPlace(EntryType entry) const noexcept
	{
		if (entry == 0 || !m_directory.size())
			return UNDEFINED_PLACE;

		auto const i = entry - 1;
		if (i > std::ssize(m_directory))
		{
			assert(false && "PlaceDirectory::EntryToPlace: Invalid entry");
			return UNDEFINED_PLACE;
		}

		return m_directory[i];
	}

#ifdef CSBOT_ENABLE_SAVE
	// store the directory
	void Save(int fd);
#endif

	// load the directory
	void Load(SteamFile* file) noexcept
	{
		// read number of entries
		EntryType count;
		file->Read(&count, sizeof(EntryType));

		m_directory.reserve(count);

		// read each entry
		char placeName[256]{};
		unsigned short len{};

		for (int i = 0; i < count; i++)
		{
			file->Read(&len, sizeof(unsigned short));
			file->Read(placeName, len);

#ifdef CSBOT_PHRASES
			Place place = TheBotPhrases->NameToID(placeName);
			if (!TheBotPhrases->IsValid() && place == UNDEFINED_PLACE)
				place = TheNavAreaGrid.NameToID(placeName);
#endif
			auto const place = Place_NameToID(placeName);
			AddPlace(place);
		}
	}

private:
	std::vector<Place> m_directory{};
};

export inline PlaceDirectory placeDirectory{};

#ifdef CSBOT_ENABLE_SAVE
bool SaveNavigationMap(const char* filename);
void LoadLocationFile(const char* filename);
#endif

// to help identify nav files
export inline constexpr auto NAV_MAGIC_NUMBER = 0xFEEDFACE;

// Performs a lightweight sanity-check of the specified map's nav mesh
export void SanityCheckNavigationMap(const char* mapName) noexcept
{
	if (!mapName)
	{
		CONSOLE_ECHO("ERROR: navigation file not specified.\n");
		return;
	}

	// nav filename is derived from map filename
	auto const bspFilename = std::format("maps\\{}.bsp", mapName);
	auto const navFilename = std::format("maps\\{}.nav", mapName);

	SteamFile navFile(navFilename.c_str());

	if (!navFile.IsValid())
	{
		CONSOLE_ECHO("ERROR: navigation file %s does not exist.\n", navFilename.c_str());
		return;
	}

	// check magic number
	unsigned int magic{};
	auto result = navFile.Read(&magic, sizeof(unsigned int));
	if (!result || magic != NAV_MAGIC_NUMBER)
	{
		CONSOLE_ECHO("ERROR: Invalid navigation file '%s'.\n", navFilename.c_str());
		return;
	}

	// read file version number
	unsigned int version{};
	result = navFile.Read(&version, sizeof(unsigned int));
	if (!result || version > NAV_VERSION)
	{
		CONSOLE_ECHO("ERROR: Unknown version in navigation file %s.\n", navFilename.c_str());
		return;
	}

	if (version >= 4)
	{
		// get size of source bsp file and verify that the bsp hasn't changed
		unsigned int saveBspSize{};
		navFile.Read(&saveBspSize, sizeof(unsigned int));

		// verify size
		if (saveBspSize == 0)
		{
			CONSOLE_ECHO("ERROR: No map corresponds to navigation file %s.\n", navFilename.c_str());
			return;
		}

		auto const bspSize = (unsigned int)g_engfuncs.pfnGetFileSize(bspFilename.c_str());
		if (bspSize != saveBspSize)
		{
			// this nav file is out of date for this bsp file
			CONSOLE_ECHO("ERROR: Out-of-date navigation data in navigation file %s.\n", navFilename.c_str());
			return;
		}
	}

	CONSOLE_ECHO("navigation file %s passes the sanity check.\n", navFilename.c_str());
}


#pragma endregion nav_file
