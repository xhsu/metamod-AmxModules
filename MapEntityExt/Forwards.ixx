export module Forwards;

import Plugin;

export namespace Forwards
{
	inline std::int32_t OnKeyValue = 0;
}

export void DeployForwards() noexcept
{
	using namespace Forwards;

	OnKeyValue = MF_RegisterForward("MEEF_KeyValue", ET_IGNORE, FP_CELL, FP_CELL, FP_DONE);	// ent, pkvd
}
