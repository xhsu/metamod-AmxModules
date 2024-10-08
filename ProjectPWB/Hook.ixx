export module Hook;

import std;
import hlsdk;

import CBase;
import Plugin;

import UtlHook;
import UtlString;


struct rpl_info_t
{
	std::string_view m_model{};
	decltype(entvars_t::body) m_body{};
};

export inline constexpr char THE_BPW_MODEL[] = "models/bpw_001.mdl";
export inline std::vector<BodyEnumInfo_t> gModelMeshGroupsInfo{};
export inline std::map<std::string, int, std::less<>> gWorldModelRpl{};	// KEY<model_path>, VALUE<BODY>
export inline std::map<std::string_view, int, std::less<>> gBackModelRpl{};	// KEY<classname>, VALUE<BODY>

/*
export inline std::map<std::string_view, std::string_view, sv_iless_t> const gModelRpl
{
	{ "models/p_ak47.mdl",	"models/bpw_001.mdl" },
	{ "models/p_galil.mdl",	"models/bpw_001.mdl" },

	{ "models/w_ak47.mdl",	"models/bpw_001.mdl" },
	{ "models/w_galil.mdl",	"models/bpw_001.mdl" },
};

export inline std::map<std::string_view, int, sv_iless_t> const gModelBody
{
	{ "models/p_ak47.mdl",	UTIL_CalcBody(std::array<BodyEnumInfo_t, 2>{{ {1, 3}, {0, 3}, }}) },
	{ "models/p_galil.mdl",	UTIL_CalcBody(std::array<BodyEnumInfo_t, 2>{{ {2, 3}, {0, 3}, }}) },

	{ "models/w_ak47.mdl",	UTIL_CalcBody(std::array<BodyEnumInfo_t, 2>{{ {1, 3}, {0, 3}, }}) },
	{ "models/w_galil.mdl",	UTIL_CalcBody(std::array<BodyEnumInfo_t, 2>{{ {2, 3}, {0, 3}, }}) },
};

export inline std::map<std::string_view, WeaponIdType, sv_less_t> const gWeaponId
{
	{ "weapon_ak47",	WEAPON_AK47 },
	{ "weapon_galil",	WEAPON_GALIL },
};

export inline std::map<std::string_view, rpl_info_t, sv_less_t> const gBackModel
{
	{ "weapon_ak47",	{ "models/bpw_001.mdl", UTIL_CalcBody(std::array<BodyEnumInfo_t, 2>{{ {0, 3}, {1, 3}, }}) } },
	{ "weapon_galil",	{ "models/bpw_001.mdl", UTIL_CalcBody(std::array<BodyEnumInfo_t, 2>{{ {0, 3}, {2, 3}, }}) } },
};
*/

export inline constexpr std::size_t VFTIDX_ITEM_HOLSTER = 67;

extern "C++" qboolean __fastcall OrpheuF_DefaultDeploy(CBasePlayerWeapon* pWeapon, std::uintptr_t, const char* szViewModel, const char* szWeaponModel, int iAnim, const char* szAnimExt, qboolean skiplocal) noexcept;
extern "C++" void PrecacheModelInfo() noexcept;
extern "C++" void DeployVftInjection(void) noexcept;

export namespace HookInfo
{
	inline FunctionHook DefaultDeploy{ &OrpheuF_DefaultDeploy };
}
