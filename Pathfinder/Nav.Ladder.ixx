export module Nav:Ladder;

import std;
import hlsdk;

import CBase;

import :Const;

class CNavArea;

export struct CNavLadder final
{
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

	void OnNavAreaDestroy(CNavArea* dead) noexcept
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

export using NavLadderList = std::vector<CNavLadder*>;	// not owning!
export extern "C++" inline std::forward_list<CNavLadder> TheNavLadderList{};

extern void BuildLadders() noexcept;
