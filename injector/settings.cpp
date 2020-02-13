#include "settings.h"
#include "mapping.h"
#include "rfu.h"

#include <string>
#include <iostream>
#include <fstream>

FileMapping IPC;

namespace Settings
{
	bool VSyncEnabled = false;
	unsigned char FPSCapSelection = 0;
	double FPSCap = 0.0;
	bool UnlockStudio = false;
	bool CheckForUpdates = true;
	bool NonBlockingErrors = true;
	bool SilentErrors = false;

	bool Init()
	{
#ifndef RFU_NO_DLL
		IPC.Open(0, "RFUSettingsMap", sizeof(SettingsIPC));
		if (!IPC.IsOpen()) return false;
#endif
		if (!Load()) Save();
		Update();
		return true;
	}

	// very basic settings parser/writer

	bool Load()
	{
		std::ifstream file("settings");
		if (!file.is_open()) return false;

		printf("Loading settings from file...\n");

		std::string line;

		while (std::getline(file, line))
		{
			size_t eq = line.find('=');
			if (eq != std::string::npos)
			{
				std::string key = line.substr(0, eq);
				std::string value = line.substr(eq + 1);

				try
				{
					if (key == "VSyncEnabled")
						VSyncEnabled = std::stoi(value) != 0;
					else if (key == "FPSCapSelection")
						FPSCapSelection = std::stoi(value);
					else if (key == "FPSCap")
						FPSCap = std::stod(value);
					else if (key == "UnlockStudio")
						UnlockStudio = std::stoi(value) != 0;
					else if (key == "CheckForUpdates")
						CheckForUpdates = std::stoi(value) != 0;
					else if (key == "NonBlockingErrors")
						NonBlockingErrors = std::stoi(value) != 0;
					else if (key == "SilentErrors")
						SilentErrors = std::stoi(value) != 0;
				}
				catch (std::exception& e)
				{
					// catch string conversion errors
				}
			}
		}

		return true;
	}

	bool Save()
	{
		std::ofstream file("settings");
		if (!file.is_open()) return false;

		printf("Saving settings to file...\n");

#ifndef RFU_NO_DLL
		file << "VSyncEnabled=" << std::to_string(VSyncEnabled) << std::endl;
#else
		file << "UnlockStudio=" << std::to_string(UnlockStudio) << std::endl;
#endif

		file << "FPSCapSelection=" << std::to_string(FPSCapSelection) << std::endl;
		file << "FPSCap=" << std::to_string(FPSCap) << std::endl;
		file << "CheckForUpdates=" << std::to_string(CheckForUpdates) << std::endl;
		file << "NonBlockingErrors=" << std::to_string(NonBlockingErrors) << std::endl;
		file << "SilentErrors=" << std::to_string(SilentErrors) << std::endl;

		return true;
	}

	bool Update()
	{
#ifndef RFU_NO_DLL
		auto ipc = GetIPC();
		if (!ipc) return false;
		ipc->vsync_enabled = VSyncEnabled;
		ipc->fps_cap = FPSCap;
#else
		SetFPSCapExternal(FPSCap);
#endif
		return true;
	}

#ifndef RFU_NO_DLL
	SettingsIPC* GetIPC()
	{
		return IPC.Get<SettingsIPC *>();
	}
#endif
}