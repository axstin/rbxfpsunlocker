#pragma once

#include <Windows.h>

namespace UI
{
	void CreateHiddenConsole();
	void SetConsoleVisible(bool visible);
	bool ToggleConsole();
	int Start(HINSTANCE instance, LPTHREAD_START_ROUTINE watchthread);

	extern HWND Window;
	extern int AttachedProcessesCount;
	extern bool IsConsoleOnly;
}