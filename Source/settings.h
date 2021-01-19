#pragma once

namespace Settings
{
	extern bool VSyncEnabled;
	extern unsigned char FPSCapSelection;
	extern double FPSCap;
	extern bool UnlockClient;
	extern bool UnlockStudio;
	extern bool CheckForUpdates;
	extern bool NonBlockingErrors;
	extern bool SilentErrors;
	extern bool QuickStart;
	extern bool MinimizeToTray;

	bool Init();
	bool Load();
	bool Save();

	void Update();
}