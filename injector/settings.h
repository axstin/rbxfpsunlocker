#pragma once

namespace Settings
{
	extern bool VSyncEnabled;
	extern unsigned char FPSCapSelection;
	extern double FPSCap;

	bool Init();
	bool Load();
	bool Save();
	bool UpdateIPC();
}