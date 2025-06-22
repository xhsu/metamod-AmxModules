module;

#ifdef __INTELLISENSE__
#include <algorithm>
#endif

#include <assert.h>

export module Resources;

import std;
import hlsdk;

import Models;
import Sprite;
import Wave;

using std::move_only_function;
using std::string_view;
using std::vector;

using std::int32_t;

struct CaseIgnoredCmp final
{
	static constexpr bool operator() (char lhs, char rhs) noexcept
	{
		if (((lhs | rhs) & 0b1000'0000) == 0b1000'0000)	// UTF-8
			return lhs == rhs;

		if (lhs >= 'A' && rhs <= 'Z')
			lhs ^= 0b0010'0000;
		if (rhs >= 'A' && rhs <= 'Z')
			rhs ^= 0b0010'0000;

		return lhs == rhs;
	}

	using is_transparent = int;
};

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
	};

	export inline void Precache() noexcept
	{
		for (auto&& fn : Manager::Get().m_Initializers)
			fn();
	}

	// Must be in global scope!
	// And make it non-const!
	// #MSVC_BUG_PARAMETER_PACK_ON_C_ARR
	export struct Add final
	{
		template <size_t N>
		Add(const char(&rgcName)[N]) noexcept : m_pszName{ rgcName }
		{
			// Must compare the char[] with another char[],
			// Or you will encounter \0 problem with string_view.

			if (std::ranges::ends_with(rgcName, ".mdl", CaseIgnoredCmp{}))
			{
				Manager::Get().m_Initializers.emplace_back(
					[&]() noexcept { m_Index = g_engfuncs.pfnPrecacheModel(m_pszName); }
				);
			}
			else if (std::ranges::ends_with(rgcName, ".spr", CaseIgnoredCmp{}))
			{
				Manager::Get().m_Initializers.emplace_back(
					[&, rgcName]() noexcept {
						m_Index = g_engfuncs.pfnPrecacheModel(rgcName);
						GoldSrc::SpriteInfo.Add(rgcName);
					}
				);
			}
			else if (std::ranges::ends_with(rgcName, ".wav", CaseIgnoredCmp{}))
			{
				Manager::Get().m_Initializers.emplace_back(
					[&]() noexcept { m_Index = g_engfuncs.pfnPrecacheSound(m_pszName); }
				);
			}
			else [[unlikely]]
			{
				Manager::Get().m_Initializers.emplace_back(
					[&]() noexcept { m_Index = g_engfuncs.pfnPrecacheGeneric(m_pszName); }
				);

				assert(false);
			}
		}

		const char* m_pszName{};
		int32_t m_Index{};

		inline operator int32_t() const noexcept { return m_Index; }
		inline operator const char* () const noexcept { return m_pszName; }
	};
}
