module;

#include <assert.h>

export module Studio;

#ifdef __INTELLISENSE__
import std;
#else
import std.compat;
#endif
import hlsdk;

import Server;
import Uranus;

import UtlHook;


export extern "C++" inline float (*g_pRotationMatrix)[3][4] = nullptr;
export extern "C++" inline float (*g_pBoneTransform)[128][3][4] = nullptr;
extern "C++" inline sv_blending_interface_t** gppSvBlendingAPI = nullptr;
export extern "C++" inline server_studio_api_t* gpSvStudioAPI = {};

export void RetrieveEngineStudio() noexcept
{
	static constexpr auto OFS = 0x101CD74E - 0x101CD690;	// Anniversary, 9980

	g_pBoneTransform =
		UTIL_RetrieveGlobalVariable<float[128][3][4]>(gUranusCollection.pfnHost_InitializeGameDLL, OFS);
	g_pRotationMatrix =
		UTIL_RetrieveGlobalVariable<float[3][4]>(gUranusCollection.pfnHost_InitializeGameDLL, OFS + 5);
	gpSvStudioAPI =
		UTIL_RetrieveGlobalVariable<server_studio_api_t>(gUranusCollection.pfnHost_InitializeGameDLL, OFS + 5 * 2);
	gppSvBlendingAPI =
		UTIL_RetrieveGlobalVariable<sv_blending_interface_t*>(gUranusCollection.pfnHost_InitializeGameDLL, OFS + 5 * 3);

	assert(g_pBoneTransform && g_pRotationMatrix && gpSvStudioAPI && gppSvBlendingAPI);
}

export [[nodiscard]] auto SvBlendingAPI() noexcept -> decltype(*gppSvBlendingAPI)
{
	return *gppSvBlendingAPI;
}

export [[nodiscard]] auto UTIL_GetAttachmentOffset(const char* pszModel, unsigned iAttachment, int iSequence = 0, float flFrame = 0) noexcept -> Vector
{
	auto const iModelIndex = g_engfuncs.pfnModelIndex(pszModel);
	auto const pstudiohdr = gpSvStudioAPI->Mod_Extradata(gpServerVars->models[iModelIndex]);

	if (!pstudiohdr)
		return {};

	if (iAttachment < 0 || iAttachment >= pstudiohdr->numattachments)
		return {}; // invalid attachment

	auto pattachment = (mstudioattachment_t*)((char*)pstudiohdr + pstudiohdr->attachmentindex);
	pattachment += iAttachment;

	//vecAngles[0] = -pEdict->v.angles[0];
	//vecAngles[1] = pEdict->v.angles[1];
	//vecAngles[2] = pEdict->v.angles[2];

	static Angles s_dummyAngles{};
	static std::array<uint8_t, 4> s_dummyControllers{};
	static std::array<uint8_t, 4> s_dummyBlendings{};

	SvBlendingAPI()->SV_StudioSetupBones(
		gpServerVars->models[iModelIndex],
		flFrame,
		iSequence,
		s_dummyAngles,
		g_vecZero,
		s_dummyControllers.data(),
		s_dummyBlendings.data(),
		pattachment->bone,
		nullptr
	);

	auto& BoneTransform = (*g_pBoneTransform)[pattachment->bone];

	return Vector{
		(vec_t)DotProduct(pattachment->org, BoneTransform[0]) + BoneTransform[0][3],
		(vec_t)DotProduct(pattachment->org, BoneTransform[1]) + BoneTransform[1][3],
		(vec_t)DotProduct(pattachment->org, BoneTransform[2]) + BoneTransform[2][3],
	};
}
