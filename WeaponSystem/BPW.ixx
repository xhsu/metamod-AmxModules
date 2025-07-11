module;

#include <assert.h>

export module BPW;

import std;
import hlsdk;

import UtlString;

import FileSystem;
import Platform;
import Resources;


using namespace std;

extern "C++" inline vector<string> g_rgszCombinedModels{};
extern "C++" inline vector<int> g_rgiCombinedModelIndeces{};

export struct CCombinedModelInfo final
{
	string_view m_szModel{};

	string m_szPlayerAnim{};
	int m_iSequence{ -1 };

	int m_iPModelBodyVal{ -1 };
	int m_iWModelBodyVal{ -1 };
	int m_iBModelBodyVal{ -1 };
};

struct CMaterializedWeaponModelInfoCell final
{
	string_view m_szModel{};

	string m_szPlayerAnim{};
	int m_iSequence{ -1 };

	int m_iBodyVal{ -1 };

	[[nodiscard]] constexpr bool IsValid() const noexcept
	{
		return !m_szModel.empty() && m_iSequence >= 0 && m_iBodyVal >= 0;
	}
};

struct CMaterializedWeaponModelInfo final
{
	CMaterializedWeaponModelInfoCell m_BackModel{};
	CMaterializedWeaponModelInfoCell m_PossessedModel{};
	CMaterializedWeaponModelInfoCell m_WorldModel{};
};

export inline std::map<string, CCombinedModelInfo, sv_iless_t> gCombinedModelInfo;

// Should be executed before Resource::Precache()!
export void PrecacheCombinedModels() noexcept
{
	if (!g_rgszCombinedModels.empty())
	{
		// Just reprecache during server reloading
		g_rgiCombinedModelIndeces.clear();
		for (auto&& szModel : g_rgszCombinedModels)
			g_rgiCombinedModelIndeces.push_back(g_engfuncs.pfnPrecacheModel(szModel.c_str()));

		return;
	}

	for (int i = 0; i < 128; ++i)
	{
		auto szModel = std::format("models/WSIV/bpw_{:0>3}.mdl", i);
		if (FileSystem::m_pObject->FileExists(szModel.c_str()))
			g_rgszCombinedModels.emplace_back(std::move(szModel));
	}

	for (auto&& szModel : g_rgszCombinedModels)
	{
		g_rgiCombinedModelIndeces.push_back(g_engfuncs.pfnPrecacheModel(szModel.c_str()));
		Resource::Transcript(szModel);

		auto const pTranscription = Resource::GetStudioTranscription(szModel);
		assert(pTranscription != nullptr);

		for (int i = 0; i < std::ssize(pTranscription->m_Sequences); ++i)
		{
			auto& Sequence = pTranscription->m_Sequences[i];

			// Label should be in format of "playeranim_weaponName"
			string_view const szLabel{ Sequence.m_szLabel };
			auto const pos = szLabel.find_first_of('_');
			if (pos == szLabel.npos)
				continue;

			string_view const szPlayerAnim = szLabel.substr(0, pos);
			string_view const szClassname = szLabel.substr(pos + 1);

			auto&& [it, bNew] = gCombinedModelInfo.try_emplace(
				string{ szClassname },
				CCombinedModelInfo{ .m_szModel{szModel}, .m_szPlayerAnim{szPlayerAnim}, .m_iSequence{i}, }
			);

			if (!bNew) [[unlikely]]
			{
				UTIL_Terminate(
					"Bad BPW model: \"%s\"\n"
					"You should not split weapon '%s' into multiple BPW model.\n",
					szModel.c_str(),
					it->first.c_str()
				);
			}
		}

		if (pTranscription->m_Parts.size() > 1) [[unlikely]]
		{
			UTIL_Terminate(
				"Bad BPW model: \"%s\"\n"
				"Should only be composed from 1 bodygroup, but %d found.\n",
				szModel.c_str(),
				pTranscription->m_Parts.size()
			);
		}

		for (int i = 0; i < std::ssize(pTranscription->m_Parts[0].m_SubModels); ++i)
		{
			auto& SubModel = pTranscription->m_Parts[0].m_SubModels[i];

			// Label should be in format of "[b][p][w]_weaponName"
			string_view const szLabel{ SubModel.m_szName };
			auto const pos = szLabel.find_first_of('_');
			if (pos == szLabel.npos)
				continue;

			string_view const szConfig = szLabel.substr(0, pos);
			string_view const szWeaponName = szLabel.substr(pos + 1);

			if (auto const it = gCombinedModelInfo.find(szWeaponName);
				it != gCombinedModelInfo.cend())	[[likely]]
			{
				if (szConfig.find_first_not_of("bpwBPW") != szConfig.npos)
				{
					string const szConfig2{ szConfig };	// null-term for c-style formatting.

					UTIL_Terminate(
						"Bad BPW model: \"%s\"\n"
						"Unrecognized config string '%s' found in \"%s\"\n"
						"It must be \"[b][p][w]_[shield_]weaponName\".\n",
						szModel.c_str(),
						szConfig2.c_str(), szLabel.data()
					);
				}

				if (szConfig.contains('b') || szConfig.contains('B'))
					it->second.m_iBModelBodyVal = i;
				if (szConfig.contains('p') || szConfig.contains('P'))
					it->second.m_iPModelBodyVal = i;
				if (szConfig.contains('w') || szConfig.contains('W'))
					it->second.m_iWModelBodyVal = i;
			}
			else
			{
				UTIL_Terminate(
					"Bad BPW model: \"%s\"\n"
					"Unrecognized submodel '%s' found.\n"
					"No corresponding sequence found in the same model.\n",
					szModel.c_str(),
					szWeaponName.data()	// it's the later part of the string, so it will null-term.
				);
			}
		}
	}

	// Verify info
	string szError{};
	for (auto&& [szWeapon, Info] : gCombinedModelInfo)
	{
		if (Info.m_szModel.empty())
			szError += std::format("\"{}\": No model found.\n", szWeapon);
		if (Info.m_szPlayerAnim.empty())
			szError += std::format("\"{}\": Bad player animation set.\n", szWeapon);
		if (Info.m_iSequence < 0)
			szError += std::format("\"{}\": Bad combined model sequence index.\n", szWeapon);
		if (Info.m_iBModelBodyVal < 0)
			szError += std::format("\"{}\": Bad B Model submodel pev->body value.\n", szWeapon);
		if (Info.m_iPModelBodyVal < 0)
			szError += std::format("\"{}\": Bad P Model submodel pev->body value.\n", szWeapon);
		if (Info.m_iWModelBodyVal < 0)
			szError += std::format("\"{}\": Bad W Model submodel pev->body value.\n", szWeapon);
	}

	if (!szError.empty()) [[unlikely]]
	{
		UTIL_Terminate(
			"Bad BPW model info.\n"
			"Problems found: \n"
			"%s",
			szError.c_str()
		);
	}
}
