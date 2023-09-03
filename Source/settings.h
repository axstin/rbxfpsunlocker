#pragma once

#include <vector>

namespace Settings
{
	enum class UnlockMethodType
	{
		Hybrid,
		MemoryWrite,
		FlagsFile,

		Count
	};

	extern std::vector<double> FPSCapValues;
	extern uint32_t FPSCapSelection;
	extern double FPSCap;
	extern bool UnlockClient;
	extern bool UnlockStudio;
	extern bool CheckForUpdates;
	extern bool AltEnterFix;
	extern bool NonBlockingErrors;
	extern bool SilentErrors;
	extern bool QuickStart;
	extern bool RevertFlagsOnClose;
	extern UnlockMethodType UnlockMethod;

	bool Init();
	bool Load();
	bool Save();
}