#include <Windows.h>
#include <iostream>
#include <string>
#include <Lmcons.h>
#include <filesystem>

void MoveFileToStartup() {
    char buffer[256];
    sprintf_s(buffer, "Would you like to move Roblox FPS Unlocker to the system start up folder?");
    if (MessageBoxA(NULL, buffer, "Autostart Confirmation", MB_YESNOCANCEL | MB_ICONINFORMATION) == IDYES)
    {
		// get path of current executable & name of executable (in case if user decides to name executable something random)
		char path[MAX_PATH];
		GetModuleFileNameA(NULL, path, MAX_PATH);
		// get system username
		char username[UNLEN + 1];
		DWORD username_len = UNLEN + 1;
		GetUserName(username, &username_len);
		std::string destinationPath = "C:\\Users\\" + std::string(username) + "\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\rbxfpsunlocker.exe";
		if (path != destinationPath)
		{
			// copy the file to startup folder
			CopyFile(path, destinationPath.c_str(), false);
			// delete old file
			remove(path);
			// run the copied file
			ShellExecute(NULL, "open", destinationPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
			exit(0);
		}

    }
}
