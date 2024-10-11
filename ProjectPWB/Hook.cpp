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
import UtlString;



qboolean __fastcall OrpheuF_DefaultDeploy(
	CBasePlayerWeapon* pWeapon, std::uintptr_t edx,
	const char* pszViewModel, const char* pszWeaponModel, int iAnim, const char* szAnimExt,
	qboolean skiplocal) noexcept
{
	if (gRplInfo.contains(pszWeaponModel))
	{
		auto& info = gRplInfo.at(pszWeaponModel);

		szAnimExt = info.m_posture.c_str();

		pWeapon->pev->sequence = info.m_seq;
		pWeapon->pev->framerate = 1.f;
		pWeapon->pev->animtime = gpGlobals->time;

		g_engfuncs.pfnSetModel(pWeapon->edict(), info.m_model.c_str());

		pWeapon->pev->effects = 0;
		pWeapon->pev->body = info.m_body;
		pWeapon->pev->aiment = pWeapon->m_pPlayer->edict();
		pWeapon->pev->movetype = MOVETYPE_FOLLOW;

		pszWeaponModel = "";

//		g_engfuncs.pfnServerPrint(std::format("fw_AddToFullPack_Post: {}\n", gpGlobals->time).c_str());
		gWpnDeployCheck.emplace(pWeapon);
	}

	return HookInfo::DefaultDeploy(pWeapon, edx, pszViewModel, pszWeaponModel, iAnim, szAnimExt, skiplocal);
}

inline std::map<std::string_view, void(__fastcall*)(CBasePlayerItem*, std::uintptr_t, qboolean)> gOrgHolsterFn{};

void __fastcall HamF_Item_Holster(CBasePlayerItem* pWeapon, std::uintptr_t edx, qboolean skiplocal) noexcept
{
	std::string_view const szClassName{ STRING(pWeapon->pev->classname) };
	gOrgHolsterFn[szClassName](pWeapon, edx, skiplocal);
	auto& info = gBackModelRpl.at(szClassName);

	g_engfuncs.pfnSetModel(pWeapon->edict(), info.m_model.c_str());

	pWeapon->pev->effects = 0;
	pWeapon->pev->body = info.m_body;
	pWeapon->pev->aiment = pWeapon->m_pPlayer->edict();
	pWeapon->pev->movetype = MOVETYPE_FOLLOW;

	gWpnHolsterCheck.emplace(pWeapon);

	// If this weapon is being dropped, these values will soon being reset.
	// check CWeaponBox::PackWeapon().
}

static void ReadModelInfo(std::string_view szModel) noexcept
{
	auto f = FileSystem::StandardOpen(szModel.data(), "rb");
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
			std::string_view const szConfig{ szMeshName.substr(0, pos) };	// format: [b][p][w]_classname
			std::string_view const szWeaponName{ szMeshName.substr(pos + 1) };

			// The group itself must be named _weapon or some sort.
			if (!szGroupName.contains("_weapon"))
				continue;

			if (szConfig.contains('b'))
			{
				for (auto&& szClass : WEAPON_CLASSNAMES)
				{
					if (!szClass.ends_with(szWeaponName))
						continue;

					gBackModelRpl.try_emplace(
						szClass,
						bmdl_info_t{ .m_body{UTIL_CalcBody(gModelMeshGroupsInfo)}, .m_model{szModel} }
					);
					break;
				}
			}
			if (szConfig.contains('p'))
			{
				gRplInfo.try_emplace(
					std::format("models/p_{}.mdl", szWeaponName),
					rpl_info_t{ .m_body{UTIL_CalcBody(gModelMeshGroupsInfo)}, .m_model{szModel}, .m_posture{}, .m_seq{}, }
				);
			}
			if (szConfig.contains('w'))
			{
				gRplInfo.try_emplace(
					std::format("models/w_{}.mdl", szWeaponName),
					rpl_info_t{ .m_body{UTIL_CalcBody(gModelMeshGroupsInfo)}, .m_model{szModel}, .m_posture{}, .m_seq{}, }
				);
			}
		}
	}

	auto const pseq = (mstudioseqdesc_t*)((std::uintptr_t)pbuf + phdr->seqindex);
	for (unsigned i = 0; i < phdr->numseq; ++i)
	{
		auto const& seq = pseq[i];
		auto const rgszTexts = UTIL_Split(seq.label, "_");

		if (rgszTexts.size() < 2)
			continue;

		// Drop the first one, it is for player sequence group.
		for (auto&& szWeaponName : rgszTexts | std::views::drop(1))
		{
			auto const itWMDL = gRplInfo.find(std::format("models/w_{}.mdl", szWeaponName));
			auto const itPMDL = gRplInfo.find(std::format("models/p_{}.mdl", szWeaponName));

			// For all the following P model, the player anim group will be the one written in the first cell
			if (itWMDL != gRplInfo.end())
			{
				itWMDL->second.m_posture = std::string{ rgszTexts.front() };
				itWMDL->second.m_seq = i;
			}
			if (itPMDL != gRplInfo.end())
			{
				itPMDL->second.m_posture = std::string{ rgszTexts.front() };
				itPMDL->second.m_seq = i;
			}
		}
	}

#ifdef _DEBUG
	auto& ref1 = gRplInfo;
	auto& ref3 = gBackModelRpl;
#endif

	delete[] pbuf;
	fclose(f);
}

void ListPwbModels() noexcept
{
	auto& fs = FileSystem::m_pObject;
	std::string szPath{};

	for (int i = 0; i < 100; ++i)
	{
		szPath = std::format("models/bpw_0{:0>2}.mdl", i);
		if (fs->FileExists(szPath.c_str()))
			ReadModelInfo(szPath);
	}
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
