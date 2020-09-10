#include "settings.h"
#include "rfu.h"

#include <string>
#include <iostream>
#include <fstream>

namespace Settings
{
	bool VSyncEnabled = false;
	unsigned char FPSCapSelection = 0;
	double FPSCap = 0.0;
	bool UnlockClient = true;
	bool UnlockStudio = false;
	bool CheckForUpdates = true;
	bool NonBlockingErrors = true;
	bool SilentErrors = false;
	bool QuickStart = false;

	bool Init()
	{
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
					else if (key == "UnlockClient")
						UnlockClient = std::stoi(value) != 0;
					else if (key == "UnlockStudio")
						UnlockStudio = std::stoi(value) != 0;
					else if (key == "CheckForUpdates")
						CheckForUpdates = std::stoi(value) != 0;
					else if (key == "NonBlockingErrors")
						NonBlockingErrors = std::stoi(value) != 0;
					else if (key == "SilentErrors")
						SilentErrors = std::stoi(value) != 0;
					else if (key == "QuickStart")
						QuickStart = std::stoi(value) != 0;
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

		file << "UnlockClient=" << std::to_string(UnlockClient) << std::endl;
		file << "UnlockStudio=" << std::to_string(UnlockStudio) << std::endl;
		file << "FPSCapSelection=" << std::to_string(FPSCapSelection) << std::endl;
		file << "FPSCap=" << std::to_string(FPSCap) << std::endl;
		file << "CheckForUpdates=" << std::to_string(CheckForUpdates) << std::endl;
		file << "NonBlockingErrors=" << std::to_string(NonBlockingErrors) << std::endl;
		file << "SilentErrors=" << std::to_string(SilentErrors) << std::endl;
		file << "QuickStart=" << std::to_string(QuickStart) << std::endl;

		return true;
	}

	void Update()
	{
		SetFPSCapExternal(FPSCap);
	}
}