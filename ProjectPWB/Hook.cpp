#include <stdio.h>

#ifdef __INTELLISENSE__
#include <ranges>
#endif

import std;
import hlsdk;

import CBase;
import FileSystem;
import Hook;
import Plugin;

import UtlHook;



qboolean __fastcall OrpheuF_DefaultDeploy(
	CBasePlayerWeapon* pWeapon, std::uintptr_t edx,
	const char* pszViewModel, const char* pszWeaponModel, int iAnim, const char* szAnimExt,
	qboolean skiplocal) noexcept
{
	if (gWorldModelRpl.contains(pszWeaponModel))
	{
		g_engfuncs.pfnSetModel(pWeapon->edict(), THE_BPW_MODEL);

		pWeapon->pev->effects = EF_LIGHT;
		pWeapon->pev->body = gWorldModelRpl.at(pszWeaponModel);
		pWeapon->pev->aiment = pWeapon->m_pPlayer->edict();
		pWeapon->pev->movetype = MOVETYPE_FOLLOW;

		pszWeaponModel = "";
	}

	return HookInfo::DefaultDeploy(pWeapon, edx, pszViewModel, pszWeaponModel, iAnim, szAnimExt, skiplocal);
}

inline std::map<std::string_view, void(__fastcall*)(CBasePlayerItem*, std::uintptr_t, qboolean)> gOrgHolsterFn{};

void __fastcall HamF_Item_Holster(CBasePlayerItem* pWeapon, std::uintptr_t edx, qboolean skiplocal) noexcept
{
	std::string_view const szClassName{ STRING(pWeapon->pev->classname) };
	gOrgHolsterFn[szClassName](pWeapon, edx, skiplocal);

	g_engfuncs.pfnSetModel(pWeapon->edict(), THE_BPW_MODEL);

	pWeapon->pev->effects = EF_LIGHT;
	pWeapon->pev->body = gBackModelRpl.at(szClassName);
	pWeapon->pev->aiment = pWeapon->m_pPlayer->edict();
	pWeapon->pev->movetype = MOVETYPE_FOLLOW;

	// If this weapon is being dropped, these values will soon being reset.
	// check CWeaponBox::PackWeapon().
}

inline constexpr std::string_view WEAPON_CLASSNAMES[] =
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

void PrecacheModelInfo() noexcept
{
	auto f = FileSystem::StandardOpen(THE_BPW_MODEL, "rb");
	if (!f)
		return;

	fseek(f, 0, SEEK_END);
	auto const iSize = (std::size_t)ftell(f);
	auto const pbuf = new char[iSize + 1] {};
	fseek(f, 0, SEEK_SET);
	fread(pbuf, sizeof(char), iSize, f);

	auto const phdr = (studiohdr_t*)pbuf;

	auto const pbp = (mstudiobodyparts_t*)((std::uintptr_t)pbuf + phdr->bodypartindex);
	std::span const bodyparts{ pbp, (size_t)phdr->numbodyparts };
	for (auto&& bps : bodyparts)
		gModelMeshGroupsInfo.emplace_back(BodyEnumInfo_t{ 0, bps.nummodels });

	for (unsigned i = 0; i < phdr->numbodyparts; ++i)
	{
		auto const& bps = pbp[i];
		auto const pmesh = (mstudiomodel_t*)((std::uintptr_t)pbuf + bps.modelindex);
		//std::span const meshes{ pmesh, (size_t)bps.nummodels };

		for (int j = 0; j < bps.nummodels; ++j)
		{
			auto const& mesh = pmesh[j];
			std::string_view const szMeshName{ mesh.name };

			auto const pos = szMeshName.find_last_of('_');
			if (pos == szMeshName.npos)
				continue;

			for (auto& info : gModelMeshGroupsInfo)
				info.m_index = 0;

			gModelMeshGroupsInfo[i].m_index = j;
			std::string_view const szGroupName{ bps.name };
			std::string_view const szWeaponName{ szMeshName.substr(pos + 1) };

			if (szGroupName.starts_with('p') && szGroupName.contains("_weapon"))
			{
				gWorldModelRpl.try_emplace(
					std::format("models/w_{}.mdl", szWeaponName),
					UTIL_CalcBody(gModelMeshGroupsInfo)
				);
				gWorldModelRpl.try_emplace(
					std::format("models/p_{}.mdl", szWeaponName),
					UTIL_CalcBody(gModelMeshGroupsInfo)
				);
			}
			else if (szGroupName.starts_with('b') && szGroupName.contains("_weapon"))
			{
				for (auto&& szClass : WEAPON_CLASSNAMES)
				{
					if (!szClass.ends_with(szWeaponName))
						continue;

					gBackModelRpl.try_emplace(
						szClass,
						UTIL_CalcBody(gModelMeshGroupsInfo)
					);
					break;
				}
			}
		}
	}

#ifdef _DEBUG
	auto& ref1 = gWorldModelRpl;
	auto& ref2 = gBackModelRpl;
#endif

	delete[] pbuf;
	fclose(f);
}

void DeployVftInjection(void) noexcept
{
	static bool bHooked = false;

	[[likely]]
	if (bHooked)
		return;

	for (auto&& szWeaponClass : gBackModelRpl | std::views::keys)
	{
		auto const pEdict = g_engfuncs.pfnCreateNamedEntity(MAKE_STRING_UNSAFE(szWeaponClass.data()));
		auto const vft = UTIL_RetrieveVirtualFunctionTable(pEdict->pvPrivateData);

		UTIL_VirtualTableInjection(vft, VFTIDX_ITEM_HOLSTER, &HamF_Item_Holster, (void**)&gOrgHolsterFn[szWeaponClass]);

		g_engfuncs.pfnRemoveEntity(pEdict);
	}

	bHooked = true;
}
