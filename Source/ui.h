#pragma once

#include <Windows.h>
#include <string_view>

namespace UI
{
	void CreateHiddenConsole();
	void SetConsoleVisible(bool visible);
	bool ToggleConsole();
	int Start(HINSTANCE instance, LPTHREAD_START_ROUTINE watchthread);
	int Message(std::string_view message, std::string_view title = "rbxfpsunlocker", unsigned int option = MB_OK | MB_ICONINFORMATION);

	extern HWND Window;
	extern int AttachedProcessesCount;
	extern bool IsConsoleOnly;
}