import Plugin;

namespace AmxForwards
{
	inline int OnWriteSigonMessages = 0;
}

void DeployForwards() noexcept
{
	using namespace AmxForwards;

	OnWriteSigonMessages = MF_RegisterForward("UranusF_OnWriteSigonMessages", ET_IGNORE);
}
