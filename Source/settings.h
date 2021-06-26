#pragma once

#include <vector>

namespace Settings
{
	extern bool VSyncEnabled;
	extern std::vector<double> FPSCapValues;
	extern uint32_t FPSCapSelection;
	extern double FPSCap;
	extern bool UnlockClient;
	extern bool UnlockStudio;
	extern bool CheckForUpdates;
	extern bool NonBlockingErrors;
	extern bool SilentErrors;
	extern bool QuickStart;

	bool Init();
	bool Load();
	bool Save();

	void Update();
}