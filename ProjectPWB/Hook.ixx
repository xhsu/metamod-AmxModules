export module Hook;

import std;
import hlsdk;

import CBase;
import Plugin;

import UtlHook;
import UtlString;



export inline constexpr std::string_view WEAPON_CLASSNAMES[] =
{
	"",
	"weapon_p228",
	"",
	"weapon_scout",
	"weapon_hegrenade",
	"weapon_xm1014",
	"weapon_c4",
	"weapon_mac10",
	"weapon_aug",
	"weapon_smokegrenade",
	"weapon_elite",
	"weapon_fiveseven",
	"weapon_ump45",
	"weapon_sg550",
	"weapon_galil",
	"weapon_famas",
	"weapon_usp",
	"weapon_glock18",
	"weapon_awp",
	"weapon_mp5navy",
	"weapon_m249",
	"weapon_m3",
	"weapon_m4a1",
	"weapon_tmp",
	"weapon_g3sg1",
	"weapon_flashbang",
	"weapon_deagle",
	"weapon_sg552",
	"weapon_ak47",
	"weapon_knife",
	"weapon_p90"
};

export struct rpl_info_t
{
	std::int32_t m_body{};
	std::string m_model{};
	std::string m_posture{};
	std::int32_t m_seq{};
};

export struct bmdl_info_t
{
	std::int32_t m_body{};
	std::string m_model{};
};

export inline std::vector<BodyEnumInfo_t> gModelMeshGroupsInfo{};
export inline std::map<std::string, rpl_info_t, sv_iless_t> gRplInfo{};	// KEY<model_path>
export inline std::map<std::string_view, bmdl_info_t, std::less<>> gBackModelRpl{};	// KEY<classname>

export inline std::set<CBasePlayerWeapon*, std::less<>> gWpnDeployCheck{};
export inline std::set<CBasePlayerItem*, std::less<>> gWpnHolsterCheck{};

export inline constexpr std::size_t VFTIDX_ITEM_HOLSTER = 67;

extern "C++" qboolean __fastcall OrpheuF_DefaultDeploy(CBasePlayerWeapon* pWeapon, std::uintptr_t, const char* szViewModel, const char* szWeaponModel, int iAnim, const char* szAnimExt, qboolean skiplocal) noexcept;
extern "C++" void ListPwbModels() noexcept;
extern "C++" void DeployVftInjection(void) noexcept;

export namespace HookInfo
{
	inline FunctionHook DefaultDeploy{ &OrpheuF_DefaultDeploy };
}
