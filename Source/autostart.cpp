#include <Windows.h>
#include <iostream>
#include <string>
#include <Lmcons.h>
#include <filesystem>

bool MoveFileToStartup() {
	// get path of current executable & name of executable (in case if user decides to name executable something random)
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	// get system username
	char username[UNLEN + 1];
	DWORD username_len = UNLEN + 1;
	GetUserName(username, &username_len);
	std::string destination_directory = "C:\\Users\\" + std::string(username) + "\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
	std::string destination_file = destination_directory + "\\rbxfpsunlocker.exe";
	// sys32 to prevent prompt on startup
	if (std::filesystem::current_path().string() != destination_directory && std::filesystem::current_path().string() != std::string("C:\\WINDOWS\\system32"))
	{
		if (MessageBoxA(NULL, "Would you like to move Roblox FPS Unlocker to the system start up folder?", "Autostart Confirmation", MB_YESNO | MB_ICONINFORMATION) == IDYES)
		{
			MessageBoxA(NULL, "Suuccessfully copied Roblox FPS Unlocker to start up. You may now delete the original file.", "Success", MB_OK | MB_ICONINFORMATION);
			// copy the file to startup folder
			CopyFile(path, destination_file.c_str(), false);
			// run the copied file
			ShellExecute(NULL, "open", destination_file.c_str(), NULL, destination_directory.c_str(), SW_SHOWDEFAULT);
			return true;
		}

	}
	return false;
}
