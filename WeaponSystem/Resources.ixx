module;

#ifdef __INTELLISENSE__
#include <algorithm>
#include <ranges>
#endif

#include <assert.h>

export module Resources;

import std;
import hlsdk;

import Models;
import Sprite;
import Wave;

using std::move_only_function;
using std::set;
using std::string;
using std::string_view;
using std::unordered_set;
using std::vector;
using std::unordered_map;

using std::int32_t;

struct CaseIgnoredCmp final
{
	static constexpr bool operator() (char lhs, char rhs) noexcept
	{
		if (((lhs | rhs) & 0b1000'0000) == 0b1000'0000)	// UTF-8
			return lhs == rhs;

		if (lhs >= 'A' && lhs <= 'Z')
			lhs ^= 0b0010'0000;
		if (rhs >= 'A' && rhs <= 'Z')
			rhs ^= 0b0010'0000;

		return lhs == rhs;
	}

	static constexpr bool operator() (string_view lhs, string_view rhs) noexcept
	{
		return std::ranges::equal(lhs, rhs, CaseIgnoredCmp{});
	}

	using is_transparent = int;
};

static_assert(CaseIgnoredCmp{}("models/matoilet.mdl", "Models/Matoilet.MDL"));

namespace Resource
{
	struct Manager final
	{
		inline static Manager& Get() noexcept
		{
			static Manager Instance{};
			return Instance;
		}

		vector<move_only_function<void() noexcept>> m_Initializers{};
		unordered_map<string_view, int32_t, std::hash<string_view>, CaseIgnoredCmp> m_Record{};
		//unordered_set<string_view, std::hash<string_view>, CaseIgnoredCmp> m_Record{};
	};

	export inline void Precache() noexcept
	{
		for (auto&& fn : Manager::Get().m_Initializers)
			fn();
	}

#ifdef _DEBUG
	export inline auto Debug_ViewManager() noexcept -> Manager const&
	{
		return Manager::Get();
	}
#endif

	// Must be in global scope!
	// And make it non-const!
	// #MSVC_BUG_PARAMETER_PACK_ON_C_ARR
	export struct Add final
	{
		template <size_t N>
		Add(const char(&rgcName)[N]) noexcept : m_pszName{ rgcName }
		{
			// Assume that this is a reference to array on heap, a.k.a. global variable.
			if (auto const it = Manager::Get().m_Record.find(rgcName); it != Manager::Get().m_Record.end())
			{
				m_Index = it->second;
				return;
			}

			// Must compare the char[] with another char[],
			// Or you will encounter \0 problem with string_view.

			if (std::ranges::ends_with(rgcName, ".mdl", CaseIgnoredCmp{}))
			{
				Manager::Get().m_Initializers.emplace_back(
					[&]() noexcept { m_Index = Manager::Get().m_Record[m_pszName] = g_engfuncs.pfnPrecacheModel(m_pszName); }
				);
			}
			else if (std::ranges::ends_with(rgcName, ".spr", CaseIgnoredCmp{}))
			{
				Manager::Get().m_Initializers.emplace_back(
					[&, rgcName]() noexcept {
						m_Index = Manager::Get().m_Record[m_pszName] = g_engfuncs.pfnPrecacheModel(rgcName);
						GoldSrc::SpriteInfo.Add(rgcName);
					}
				);
			}
			else if (std::ranges::ends_with(rgcName, ".wav", CaseIgnoredCmp{}))
			{
				Manager::Get().m_Initializers.emplace_back(
					[&]() noexcept { m_Index = Manager::Get().m_Record[m_pszName] = g_engfuncs.pfnPrecacheSound(m_pszName); }
				);
			}
			else if (std::ranges::ends_with(rgcName, ".sc", CaseIgnoredCmp{}))
			{
				Manager::Get().m_Initializers.emplace_back(
					[&]() noexcept { m_Index = Manager::Get().m_Record[m_pszName] = g_engfuncs.pfnPrecacheEvent(1, m_pszName); }	// #INVESTIGATE what does the first argument mean?
				);
			}
			else [[unlikely]]
			{
				Manager::Get().m_Initializers.emplace_back(
					[&]() noexcept { m_Index = Manager::Get().m_Record[m_pszName] = g_engfuncs.pfnPrecacheGeneric(m_pszName); }
				);

				assert((
					std::ranges::ends_with(rgcName, ".txt", CaseIgnoredCmp{})
					|| std::ranges::ends_with(rgcName, ".tga", CaseIgnoredCmp{})
					|| std::ranges::ends_with(rgcName, ".res", CaseIgnoredCmp{})
				));
			}
		}

		const char* m_pszName{};
		int32_t m_Index{};

		inline operator int32_t() const noexcept { return m_Index; }
		inline operator const char* () const noexcept { return m_pszName; }
	};
}
