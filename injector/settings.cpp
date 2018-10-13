#include "settings.h"
#include "mapping.h"

#include <string>
#include <iostream>
#include <fstream>

FileMapping IPC;

namespace Settings
{
	bool VSyncEnabled = false;
	unsigned char FPSCapSelection = 0;
	double FPSCap = 0.0;

	bool Init()
	{
		IPC.Open(0, "RFUSettingsMap", sizeof(SettingsIPC));
		if (!IPC.IsOpen()) return false;
		if (!Load()) Save();
		UpdateIPC();
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

		file << "VSyncEnabled=" << std::to_string(VSyncEnabled) << std::endl;
		file << "FPSCapSelection=" << std::to_string(FPSCapSelection) << std::endl;
		file << "FPSCap=" << std::to_string(FPSCap) << std::endl;

		return true;
	}

	bool UpdateIPC()
	{
		auto ipc = GetIPC();
		if (!ipc) return false;
		ipc->vsync_enabled = VSyncEnabled;
		ipc->fps_cap = FPSCap;
		return true;
	}

	SettingsIPC* GetIPC()
	{
		return IPC.Get<SettingsIPC *>();
	}
}