#include "settings.h"
#include "mapping.h"

#include <string>
#include <fstream>

#include "rfu.h"

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
			const auto eq = line.find('=');
			if (eq != std::string::npos)
			{
				auto key = line.substr(0, eq);
				auto value = line.substr(eq + 1);

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

		file << "UnlockStudio=" << std::to_string(UnlockStudio) << std::endl;

		file << "FPSCapSelection=" << std::to_string(FPSCapSelection) << std::endl;
		file << "FPSCap=" << std::to_string(FPSCap) << std::endl;
		file << "CheckForUpdates=" << std::to_string(CheckForUpdates) << std::endl;
		file << "NonBlockingErrors=" << std::to_string(NonBlockingErrors) << std::endl;
		file << "SilentErrors=" << std::to_string(SilentErrors) << std::endl;

		return true;
	}

	bool Update()
	{
		SetFPSCapExternal(FPSCap);
		return true;
	}
}