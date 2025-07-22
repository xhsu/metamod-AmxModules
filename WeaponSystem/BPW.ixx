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

	string_view m_szPlayerAnim{};
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

	char szPath[64]{};
	for (int i = 0; i < 128; ++i)
	{
		auto const res =
			std::format_to_n(
				szPath, sizeof(szPath) - 1,
				"models/WSIV/bpw_{:0>3}.mdl", i
			);
		szPath[res.size] = '\0';

		if (FileSystem::m_pObject->FileExists(szPath))
			g_rgszCombinedModels.emplace_back(&szPath[0], &szPath[res.size]);
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

extern "C++" inline Resource::Add SHIELD_COLLECTION = { "models/Shield/p_shield_001.mdl" };
extern "C++" inline std::vector<BodyEnumInfo_t> g_ShieldModelBodyInfo = {};

export void PrecacheShieldModel() noexcept
{
	Resource::Precache(SHIELD_COLLECTION);

	static bool bPrecached = false;
	if (bPrecached)
		return;

	bPrecached = true;

	auto const pModelTranscript = Resource::GetStudioTranscription(SHIELD_COLLECTION);

	g_ShieldModelBodyInfo.resize(pModelTranscript->m_Parts.size());
	for (int i = 0; i < std::ssize(g_ShieldModelBodyInfo); ++i)
		g_ShieldModelBodyInfo[i].m_total = (int)pModelTranscript->m_Parts[i].m_SubModels.size();
}

int ShieldModelSetup(std::string_view szKey, std::string_view szValue) noexcept
{
	auto const pModelTranscript = Resource::GetStudioTranscription(SHIELD_COLLECTION);

	for (auto&& [iPartIndex, Part] : std::views::enumerate(pModelTranscript->m_Parts))
	{
		if (!sv_icmp_t{}(Part.m_szName, szKey))
			continue;

		for (auto&& [iMeshIndex, Mesh] : std::views::enumerate(Part.m_SubModels))
		{
			if (sv_icmp_t{}(Mesh.m_szName, szValue))
			{
				g_ShieldModelBodyInfo[iPartIndex].m_index = iMeshIndex;
				return UTIL_CalcBody(g_ShieldModelBodyInfo);
			}
		}
	}

	return -1;
}

export [[nodiscard]] auto ShieldGetWeaponInfo(std::string_view szWeapon) noexcept -> std::expected<CMaterializedWeaponModelInfoCell, std::string>
{
	auto const pModelTranscript = Resource::GetStudioTranscription(SHIELD_COLLECTION);

	for (auto&& Seq : pModelTranscript->m_Sequences)
	{
		// Preventing matching with '\0'
		std::string_view const szLabel{ Seq.m_szLabel };
		if (std::ranges::ends_with(szLabel, szWeapon, ch_icmp_t{}))
		{
			auto const pos = szLabel.find_first_of('_');
			if (pos == szLabel.npos)
				return std::unexpected(std::format("Bad sequence name '{}'", szLabel));

			auto const szPlayerAnimGroup = std::format("p_{}", szLabel.substr(0, pos));
			auto const szBody = std::format("p_{}", szLabel.substr(pos + 1));

			int iBodyVal = -1;
			iBodyVal = ShieldModelSetup("shield", szPlayerAnimGroup);
			iBodyVal = ShieldModelSetup("shield_weapons", szBody);

			if (iBodyVal < 0)
				return std::unexpected(std::format("Cannot setup pev->body with info '{}' and '{}'", szPlayerAnimGroup, szBody));

			return CMaterializedWeaponModelInfoCell{
				.m_szModel{ SHIELD_COLLECTION },
				.m_szPlayerAnim{ szLabel.substr(0, pos) },
				.m_iSequence{ Seq.m_index },
				.m_iBodyVal{ iBodyVal },
			};
		}
	}

	return std::unexpected(std::format("No matching info for '{}'", szWeapon));
}
