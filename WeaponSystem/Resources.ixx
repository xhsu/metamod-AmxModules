module;

#ifdef __INTELLISENSE__
#include <algorithm>
#include <ranges>
#endif

#include <assert.h>

export module Resources;

import std;
import hlsdk;

import FileSystem;
import Models;
import Platform;
import Sprite;
import Wave;

using std::map;
using std::move_only_function;
using std::set;
using std::string;
using std::string_view;
using std::vector;

using std::int32_t;

using namespace std::literals;

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

struct RES_INTERNAL_sv_iless_t final
{
	struct nocase_compare
	{
		static bool operator()(char c1, char c2) noexcept
		{
			return std::tolower(c1) < std::tolower(c2);
		}
	};

	static bool operator()(std::string_view const& lhs, std::string_view const& rhs) noexcept
	{
		return std::ranges::lexicographical_compare(lhs, rhs, nocase_compare{});
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
		map<string_view, int32_t, RES_INTERNAL_sv_iless_t> m_Record{};
		map<string_view, TranscriptedStudio, RES_INTERNAL_sv_iless_t> m_TranscriptedStudio{};
		map<string_view, double, RES_INTERNAL_sv_iless_t> m_SoundLength{};
		map<string_view, TranscriptedSprite, RES_INTERNAL_sv_iless_t> m_TranscriptedSprites{};
	};

	export bool Transcript(string_view szRelativePath) noexcept
	{
		if (std::ranges::ends_with(szRelativePath, ".mdl"sv, CaseIgnoredCmp{}))
		{
			auto buf = FileSystem::LoadBinaryFile(szRelativePath.data());
			auto&& [iter, bNewEntry]
				= Manager::Get().m_TranscriptedStudio.try_emplace(
					szRelativePath,
					buf.get()
				);

			[[maybe_unused]] auto& StudioInfo = iter->second;

			return bNewEntry;
		}
		else if (std::ranges::ends_with(szRelativePath, ".spr"sv, CaseIgnoredCmp{}))
		{
			if (auto f = FileSystem::FOpen(szRelativePath.data(), "rb"); f != nullptr)
			{
				auto const [it, bNew] = Manager::Get().m_TranscriptedSprites.try_emplace(
					szRelativePath,
					f
				);
				std::fclose(f);

				return bNew;
			}

			return false;
		}
		else
		{
			UTIL_Terminate("Cannot transcript '%s'", szRelativePath.data());
		}
	}

	export [[nodiscard]] auto GetStudioTranscription(string_view szRelativePath) noexcept -> TranscriptedStudio const*
	{
		auto& Lib = Manager::Get().m_TranscriptedStudio;

		if (auto const it = Lib.find(szRelativePath); it != Lib.cend())
			return std::addressof(it->second);

		return nullptr;
	}

	export [[nodiscard]] auto GetSpriteTranscription(string_view szRelativePath) noexcept -> TranscriptedSprite const*
	{
		auto& Lib = Manager::Get().m_TranscriptedSprites;

		if (auto const it = Lib.find(szRelativePath); it != Lib.cend())
			return std::addressof(it->second);

		return nullptr;
	}

	export inline void PrecacheEverything() noexcept
	{
		for (auto&& fn : Manager::Get().m_Initializers)
			fn();

		char szGameDir[32]{};
		g_engfuncs.pfnGetGameDir(szGameDir);

		std::filesystem::path szLogPath = szGameDir;
		szLogPath /= "addons/metamod/logs/WSIV_Resources.log";

		if (!std::filesystem::exists(szLogPath.parent_path()))
			std::filesystem::create_directories(szLogPath.parent_path());

		if (auto f = std::fopen(szLogPath.u8string().c_str(), "wt"); f)
		{
			char szRelPath[256]{};
			for (auto&& szKey : Manager::Get().m_Record | std::views::keys)
			{
				if (std::ranges::ends_with(szKey, ".wav"sv, CaseIgnoredCmp{}))
				{
					auto const res = std::format_to_n(szRelPath, sizeof(szRelPath) - 1, "sound/{}", szKey);
					szRelPath[res.size] = '\0';
				}
				else
				{
					std::strncpy(szRelPath, szKey.data(), szKey.length());
					szRelPath[szKey.length()] = '\0';
				}

				auto const szAbsPath = FileSystem::GetAbsolutePath(szRelPath);
				auto const szFormattedAbsPath = std::filesystem::absolute(szAbsPath.data()).u8string();
				std::print(f, "{}{}\n",
					szFormattedAbsPath,
					FileSystem::m_pObject->FileExists(szRelPath) ? "" : "[MISSING]"
				);
			}

			std::fclose(f);
		}
	}

	export int Precache(string_view szRelativePath) noexcept
	{
		if (auto const it = Manager::Get().m_Record.find(szRelativePath);
			it != Manager::Get().m_Record.end())
		{
			return it->second;
		}

		// will encounter \0 problem with string_view.

		if (std::ranges::ends_with(szRelativePath, ".mdl"sv, CaseIgnoredCmp{}))
		{
			Manager::Get().m_Record[szRelativePath] = g_engfuncs.pfnPrecacheModel(szRelativePath.data());
			Transcript(szRelativePath);
		}
		else if (std::ranges::ends_with(szRelativePath, ".spr"sv, CaseIgnoredCmp{}))
		{
			Manager::Get().m_Record[szRelativePath] = g_engfuncs.pfnPrecacheModel(szRelativePath.data());
			Transcript(szRelativePath);
		}
		else if (std::ranges::ends_with(szRelativePath, ".wav"sv, CaseIgnoredCmp{}))
		{
			Manager::Get().m_Record[szRelativePath] = g_engfuncs.pfnPrecacheSound(szRelativePath.data());

			char szPath[256]{};
			std::format_to_n(szPath, sizeof(szPath) - 1, "sound/{}", szRelativePath);

			auto const szAbsPath = FileSystem::GetAbsolutePath(szPath);
			Manager::Get().m_SoundLength[szRelativePath] = Wave::Length(szAbsPath.data());
		}
		else if (std::ranges::ends_with(szRelativePath, ".sc"sv, CaseIgnoredCmp{}))
		{
			Manager::Get().m_Record[szRelativePath] = g_engfuncs.pfnPrecacheEvent(1, szRelativePath.data()); // #INVESTIGATE what does the first argument mean?
		}
		else [[unlikely]]
		{
			Manager::Get().m_Record[szRelativePath] = g_engfuncs.pfnPrecacheGeneric(szRelativePath.data());

			assert((
				std::ranges::ends_with(szRelativePath, ".txt"sv, CaseIgnoredCmp{})
				|| std::ranges::ends_with(szRelativePath, ".tga"sv, CaseIgnoredCmp{})
				|| std::ranges::ends_with(szRelativePath, ".res"sv, CaseIgnoredCmp{})
			));
		}

		return Manager::Get().m_Record[szRelativePath];
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
					[&]() noexcept {
						m_Index = Manager::Get().m_Record[m_pszName] = g_engfuncs.pfnPrecacheModel(m_pszName);
						Transcript(m_pszName);
					}
				);
			}
			else if (std::ranges::ends_with(rgcName, ".spr", CaseIgnoredCmp{}))
			{
				Manager::Get().m_Initializers.emplace_back(
					[&, rgcName]() noexcept {
						m_Index = Manager::Get().m_Record[m_pszName] = g_engfuncs.pfnPrecacheModel(rgcName);
						Transcript(rgcName);
					}
				);
			}
			else if (std::ranges::ends_with(rgcName, ".wav", CaseIgnoredCmp{}))
			{
				Manager::Get().m_Initializers.emplace_back(
					[&]() noexcept {
						m_Index = Manager::Get().m_Record[m_pszName] = g_engfuncs.pfnPrecacheSound(m_pszName);

						char szPath[256]{};
						std::format_to_n(szPath, sizeof(szPath) - 1, "sound/{}", m_pszName);

						auto const szAbsPath = FileSystem::GetAbsolutePath(szPath);
						Manager::Get().m_SoundLength[m_pszName] = Wave::Length(szAbsPath.data());
					}
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
		inline operator string_view () const noexcept { return m_pszName; }
	};
}
