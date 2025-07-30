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
		map<string_view, double, RES_INTERNAL_sv_iless_t> m_TranscriptedAudio{};
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
	}

	// For resource packer script.
	export inline void LogToFile() noexcept
	{
		char szGameDir[32]{};
		g_engfuncs.pfnGetGameDir(szGameDir);

		std::filesystem::path LogFilePath = szGameDir;
		LogFilePath /= "addons/metamod/logs/WSIV_Resources.log";
		auto const LogFolder = LogFilePath.parent_path();

		if (!std::filesystem::exists(LogFolder))
			std::filesystem::create_directories(LogFolder);

		auto const szLogFolder = std::filesystem::absolute(LogFolder).u8string();

		if (auto f = std::fopen(LogFilePath.u8string().c_str(), "wt"); f)
		{
			std::print(f, "{}\n", szGameDir);

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
				auto const szRelativeToLog = std::filesystem::relative(szAbsPath, LogFolder).u8string();
				//auto const szFormattedAbsPath = std::filesystem::absolute(szAbsPath.data()).u8string();
				std::print(f, "{}\\{}\n",
					szLogFolder, szRelativeToLog
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

		// Must compare the char[] with another char[], sv with another sv.
		// Or you will encounter \0 problem with string_view.

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
			Manager::Get().m_TranscriptedAudio[szRelativePath] = Wave::Length(szAbsPath.data());
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

	export void Clear() noexcept
	{
		Manager::Get().m_Record.clear();
		Manager::Get().m_TranscriptedStudio.clear();
		Manager::Get().m_TranscriptedSprites.clear();
		Manager::Get().m_TranscriptedAudio.clear();
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
		Add(const char(&rgcName)[N]) noexcept : m_pszName{ rgcName }, m_iLength{ N - 1 }
		{
			// Assume that this is a reference to array on heap, a.k.a. global variable.
			if (auto const it = Manager::Get().m_Record.find(rgcName); it != Manager::Get().m_Record.end())
			{
				m_Index = it->second;
				return;
			}

			Manager::Get().m_Initializers.emplace_back(
				[&]() noexcept {
					m_Index = Precache(m_pszName);
				}
			);
		}

		const char* m_pszName{};
		size_t m_iLength{};
		int32_t m_Index{};

		inline operator int32_t() const noexcept { return m_Index; }
		inline operator const char* () const noexcept { return m_pszName; }
		inline operator string_view () const noexcept { return string_view{ m_pszName, m_iLength }; }
	};
}
