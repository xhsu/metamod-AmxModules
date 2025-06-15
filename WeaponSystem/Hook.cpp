import std;
import hlsdk;

import Prefab;
import Uranus;

import Hook;


// CsWpn.cpp
struct G18C;
extern template void LINK_ENTITY_TO_CLASS<G18C>(entvars_t* pev) noexcept;


PFN_ENTITYINIT __cdecl OrpheuF_GetDispatch(char const* pszClassName) noexcept
{
	std::string_view const szClassname{ pszClassName };

	if (szClassname == "weapon_glock18")
		return &LINK_ENTITY_TO_CLASS<G18C>;

	return HookInfo::GetDispatch(pszClassName);
}

void DeployInlineHooks() noexcept
{
	HookInfo::GetDispatch.ApplyOn(HW::GetDispatch::pfn);
}

void RestoreInlineHooks() noexcept
{
	HookInfo::GetDispatch.UndoPatch();
}
