
#ifdef __INTELLISENSE__
import std;
#else
import std.compat;	// #MSVC_BUG_STDCOMPAT
#endif
import hlsdk;

import Message;
import Resources;

inline Resource::Add g_SmokeEffect{ "sprites/WSIV/smokeeffect.spr" };

static Vector get_aim_origin_vector(Angles const& ang, Vector const& vec, float fwd, float right, float up) noexcept
{
	auto&& [vecForward, vecRight, vecUp] = ang.AngleVectors();
	return vec + fwd * vecForward + right * vecRight + up * vecUp;
}

namespace Effects::Surface
{
	void Snow(TraceResult const& tr) noexcept	// #TODO_EFX better snow hit smoke
	{
		auto const angles = tr.vecEndPos.VectorAngles();
		auto const origin_end = tr.vecEndPos + tr.vecPlaneNormal;

		std::array const vecOrigins
		{
			get_aim_origin_vector(angles, origin_end, 10.0, 0.0, 10.0),
			get_aim_origin_vector(angles, origin_end, 10.0, 10.0, 0.0),
			get_aim_origin_vector(angles, origin_end, 10.0, 10.0, 10.0),
			get_aim_origin_vector(angles, origin_end, 10.0, 0.0, -10.0),
			get_aim_origin_vector(angles, origin_end, 10.0, -10.0, 0.0),
			get_aim_origin_vector(angles, origin_end, 10.0, -10.0, -10.0),
		};

		for (auto&& vec : vecOrigins)
		{
			MsgPVS(SVC_TEMPENTITY, tr.vecEndPos);
			WriteData(TE_SPRITE);
			WriteData(vec);
			WriteData((uint16_t)g_SmokeEffect);
			WriteData((uint8_t)6);
			WriteData((uint8_t)20);
			MsgEnd();
		}
	}
}
