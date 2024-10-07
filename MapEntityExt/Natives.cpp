
import std;
import hlsdk;

import Hook;
import Plugin;
import Forwards;


// native MEE_RegisterEntitySpawn(const szClassName[], const szSpawnCallback[]);
static cell Native_RegisterEntitySpawn(AMX* amx, cell* params) noexcept
{
	int dummy{};
	auto const pszClassName = MF_GetAmxString(amx, params[1], 0, &dummy);
	auto const iszDup = g_engfuncs.pfnAllocString(pszClassName);		// thus it's safe to use string_view
	std::string_view const szClassName{ STRING(iszDup), (std::size_t)dummy};

	auto const pszAmxFunc = MF_GetAmxString(amx, params[2], 0, &dummy);
	std::string_view const szAmxFunc{ pszAmxFunc, (std::size_t)dummy };

	auto& rgiForwards = gRegisterEntitySpawn[szClassName];
	rgiForwards.emplace_back(
		MF_RegisterSPForwardByName(amx, pszAmxFunc, FP_CELL, FP_DONE)
	);

	if (!rgiForwards.back())
	{
		MF_LogError(amx, AMX_ERR_NATIVE, "Function not found (%s, %s)", pszClassName, pszAmxFunc);
		return 0;
	}

	return rgiForwards.back();
}

// native MEE_RegisterEntityKvd(const szClassName[], const szKvdCallback[]);
static cell Native_RegisterEntityKvd(AMX* amx, cell* params) noexcept
{
	int dummy{};
	auto const pszClassName = MF_GetAmxString(amx, params[1], 0, &dummy);
	auto const iszDup = g_engfuncs.pfnAllocString(pszClassName);		// thus it's safe to use string_view
	std::string_view const szClassName{ STRING(iszDup), (std::size_t)dummy};

	auto const pszAmxFunc = MF_GetAmxString(amx, params[2], 0, &dummy);
	std::string_view const szAmxFunc{ pszAmxFunc, (std::size_t)dummy };

	auto& rgiForwards = gRegisterEntityKvd[szClassName];
	rgiForwards.emplace_back(
		MF_RegisterSPForwardByName(amx, pszAmxFunc, FP_CELL, FP_STRING, FP_STRING, FP_DONE)
	);

	if (!rgiForwards.back())
	{
		MF_LogError(amx, AMX_ERR_NATIVE, "Function not found (%s, %s)", pszClassName, pszAmxFunc);
		return 0;
	}

	return rgiForwards.back();
}

// native MEE_ReplaceLinkedClass(const szOriginalClassName[], const szDestClassName[]);
static cell Native_ReplaceLinkedClass(AMX* amx, cell* params) noexcept
{
	int dummy{};
	auto const pszOrgClassName = MF_GetAmxString(amx, params[1], 0, &dummy);
	auto const pszDestClassName = MF_GetAmxString(amx, params[2], 0, &dummy);

	std::string szOrgClassName{ pszOrgClassName }, szDestClassName{ pszDestClassName };

	auto [it, bAdded] =
		gLinkedEntityReplace.try_emplace(std::move(szOrgClassName), std::move(szDestClassName));

	if (!bAdded)
	{
		MF_LogError(amx, AMX_ERR_NATIVE, "Entity class '%s' had been registered to replace with '%s'.", it->first.c_str(), it->second.c_str());
		return false;
	}

	return true;
}

void DeployNatives() noexcept
{
	static constexpr AMX_NATIVE_INFO rgAmxNativeInfo[] =
	{
		{ "MEE_RegisterEntitySpawn",	&Native_RegisterEntitySpawn },
		{ "MEE_RegisterEntityKvd",		&Native_RegisterEntityKvd },

		{ nullptr, nullptr },
	};

	MF_AddNatives(rgAmxNativeInfo);
}
