export module Nav:Node;

import std;
import hlsdk;

impor :Const;

// LUNA: if enabled, do remember to uncomment memory free in DestroyNavigationMap().

export class CNavNode final
{
public:
	// Gurentee that all instances will be put into our list.
	static CNavNode* Create(const Vector& pos, const Vector& normal, CNavNode* parent = nullptr) noexcept
	{
		auto& node = m_list.emplace_front(pos, normal, parent);
		return &node;
	}
	// The ownership of this resource belongs to our list.
	// LUNA: No idea how to protect it under private.
	CNavNode(const Vector& pos, const Vector& normal, CNavNode* parent = nullptr) noexcept
		: m_pos{ pos }, m_normal{ normal }, m_parent{ parent }
	{
		static unsigned int nextID = 1;
		m_id = nextID++;
	}

	// return navigation node at the position, or NULL if none exists
	static const CNavNode* GetNode(const Vector& pos) noexcept
	{
		static constexpr float tolerance = 0.45f * GenerationStepSize;

		for (auto&& node : m_list)
		{
			auto const dx = std::abs(node.m_pos.x - pos.x);
			auto const dy = std::abs(node.m_pos.y - pos.y);
			auto const dz = std::abs(node.m_pos.z - pos.z);

			if (dx < tolerance && dy < tolerance && dz < tolerance)
				return &node;
		}

		return nullptr;
	}

	// get navigation node connected in given direction, or NULL if cant go that way
	CNavNode* GetConnectedNode(NavDirType dir) const noexcept
	{
		return m_to[dir];
	}

	auto GetPosition() const noexcept -> Vector const& { return m_pos; }
	auto GetNormal() const noexcept -> Vector const& { return m_normal; }
	unsigned int GetID() const noexcept { return m_id; }

	// create a connection FROM this node TO the given node, in the given direction
	void ConnectTo(CNavNode* node, NavDirType dir) noexcept { m_to[dir] = node; }
	CNavNode* GetParent() const noexcept { return m_parent; }

	// mark the given direction as having been visited
	void MarkAsVisited(NavDirType dir) noexcept { m_visited |= (1 << dir); }
	// return TRUE if the given direction has already been searched
	bool HasVisited(NavDirType dir) const noexcept { return m_visited & (1 << dir); }
	// node is bidirectionally linked to another node in the given direction
	bool IsBiLinked(NavDirType dir) const noexcept { return m_to[dir] && m_to[dir]->m_to[Opposite[dir]] == this; }
	// node is the NW corner of a bi-linked quad of nodes
	bool IsClosedCell() const noexcept
	{
		if (IsBiLinked(SOUTH) && IsBiLinked(EAST)
			&& m_to[EAST]->IsBiLinked(SOUTH) && m_to[SOUTH]->IsBiLinked(EAST)
			&& m_to[EAST]->m_to[SOUTH] == m_to[SOUTH]->m_to[EAST])
		{
			return true;
		}

		return false;
	}

	void Cover() noexcept { m_isCovered = true; }			// #PF_TODO: Should pass in area that is covering
	bool IsCovered() const noexcept { return m_isCovered; }	// return true if this node has been covered by an area

	// assign the given area to this node
	void AssignArea(CNavArea* area) noexcept { m_area = area; }
	// return associated area
	CNavArea* GetArea() const noexcept { return m_area; }

	void SetAttributes(unsigned char bits) noexcept { m_attributeFlags = bits; }
	unsigned char GetAttributes() const noexcept { return m_attributeFlags; }

	static inline std::forward_list<CNavNode> m_list{};	// the master list of all nodes for this map

private:
	friend void DestroyNavigationMap() noexcept;

	Vector m_pos{};						// position of this node in the world
	Vector m_normal{};					// surface normal at this location
	std::array<CNavNode*, NUM_DIRECTIONS> m_to{};	// links to north, south, east, and west. NULL if no link
	unsigned int m_id{};					// unique ID of this node
	unsigned char m_attributeFlags{};		// set of attribute bit flags (see NavAttributeType)

	// below are only needed when generating
	// flags for automatic node generation. If direction bit is clear, that direction hasn't been explored yet.
	unsigned char m_visited{};
	CNavNode* m_parent{};			// the node prior to this in the search, which we pop back to when this node's search is done (a stack)
	qboolean m_isCovered{};			// true when this node is "covered" by a CNavArea
	CNavArea* m_area{};			// the area this node is contained within
};
