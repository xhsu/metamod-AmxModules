export module Decal;

export inline constexpr auto CSDK_DECAL_VERSION = 20250619L;

import std;
import hlsdk;

using std::array;
using std::vector;
using std::move_only_function;

namespace Decal
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

	export inline void RetrieveIndices() noexcept
	{
		for (auto&& fn : Manager::Get().m_Initializers)
			fn();
	}

	export struct decal_t final
	{
		template <size_t N> decal_t(const char(&sz)[N]) : m_pszName{ sz }
		{
			Manager::Get().m_Initializers.emplace_back(
				std::bind_front(&decal_t::Initialize, this)
			);
		}

		const char* m_pszName{};
		int m_Index{};

		inline void Initialize(void) noexcept { m_Index = g_engfuncs.pfnDecalIndex(m_pszName); }

		template <std::integral T>
		inline operator T() const noexcept { return static_cast<T>(this->m_Index); }
	};

	export inline array GUNSHOT =
	{
		decal_t{ "{shot1" },
		decal_t{ "{shot2" },
		decal_t{ "{shot3" },
		decal_t{ "{shot4" },
		decal_t{ "{shot5" },
	};

	export inline array BIGSHOT =
	{
		decal_t{ "{bigshot1" },
		decal_t{ "{bigshot2" },
		decal_t{ "{bigshot3" },
		decal_t{ "{bigshot4" },
		decal_t{ "{bigshot5" },
	};

	export inline array SCORCH =
	{
		decal_t{ "{scorch1" },
		decal_t{ "{scorch2" },
		decal_t{ "{scorch3" },
	};

	export inline array SMALL_SCORCH =
	{
		decal_t{ "{smscorch1" },
		decal_t{ "{smscorch2" },
		decal_t{ "{smscorch3" },
	};
}
