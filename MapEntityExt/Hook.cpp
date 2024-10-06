import std;

import Forwards;
import Hook;
import Plugin;
import Prefab;
import Uranus;


struct CBaseCustom : Prefab_t
{
	void Spawn() noexcept override
	{
		//g_engfuncs.pfnServerPrint(std::format("Spawning: {}\n", STRING(pev->classname)).c_str());
		//pev->flags |= FL_KILLME;

		auto const it = gRegisterEntitySpawn.find(STRING(pev->classname));
		if (it == gRegisterEntitySpawn.end())
			return;

		cell const iEntity = this->entindex();
		for (auto&& iAmxFwd : it->second)
		{
			MF_ExecuteForward(iAmxFwd, iEntity);
		}
	}

	void KeyValue(KeyValueData* pkvd) noexcept override
	{
		pkvd->fHandled = true;	// default value, like everyone else.

		auto const it = gRegisterEntityKvd.find(STRING(pev->classname));
		if (it == gRegisterEntityKvd.end())
			return;

		cell iMax = 0;
		cell const iEntity = this->entindex();
		for (auto&& iAmxFwd : it->second)
		{
			iMax = std::max(
				iMax,
				MF_ExecuteForward(iAmxFwd, iEntity, pkvd->szKeyName, pkvd->szValue)
			);
		}

		pkvd->fHandled = iMax;
	}
};

PFN_ENTITYINIT __cdecl OrpheuF_GetDispatch(char const* pszClassName) noexcept
{
	if (gRegisterEntityKvd.contains(pszClassName)
		|| gRegisterEntitySpawn.contains(pszClassName))
	{
		return &LINK_ENTITY_TO_CLASS<CBaseCustom>;
	}

	return HookInfo::GetDispatch(pszClassName);
}
