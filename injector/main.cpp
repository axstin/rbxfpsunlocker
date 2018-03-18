#include <Windows.h>
#include <iostream>
#include <TlHelp32.h>

#pragma comment(lib, "Shlwapi.lib")
#include <Shlwapi.h>

HANDLE GetProcessByImageName(const char* imageName)
{
	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (Process32First(snapshot, &entry) == TRUE)
	{
		while (Process32Next(snapshot, &entry) == TRUE)
		{
			if (_stricmp(entry.szExeFile, imageName) == 0)
			{
				CloseHandle(snapshot);
				return OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);
			}
		}
	}

	CloseHandle(snapshot);
	return 0;
}

HANDLE Inject(HANDLE process, const char* dll_name)
{
	LPVOID loadlib = (LPVOID)GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryA");
	LPVOID remotestring = (LPVOID)VirtualAllocEx(process, NULL, strlen(dll_name), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	WriteProcessMemory(process, (LPVOID)remotestring, dll_name, strlen(dll_name), NULL);
	return CreateRemoteThread(process, NULL, NULL, (LPTHREAD_START_ROUTINE)loadlib, (LPVOID)remotestring, NULL, NULL);
}

void pause()
{
	printf("Press any key to continue . . .");
	getchar();
}

#define TITLE			"rbxfpsunlocker Injector"
#define PROCESS			"RobloxPlayerBeta.exe"
#define APPLICATION		"ROBLOX"
#define TARGET			"rbxfpsunlocker.dll"
#define WAITINTERVAL	100
#define WAITFORWINDOW	FALSE

int main()
{
	SetConsoleTitle(TITLE);

	printf("Waiting for " APPLICATION "...\n");

	HANDLE process;

	do
	{
		Sleep(WAITINTERVAL);
		process = GetProcessByImageName(PROCESS);
	} while (!process);

#if WAITFORWINDOW
	while (true)
	{
		HWND window = FindWindowA(NULL, APPLICATION);

		if (window && IsWindowVisible(window))
			break;

		Sleep(WAITINTERVAL);
	}

	Sleep(500);
#endif

	printf("Found " APPLICATION "...\n");

	char filepath[MAX_PATH];
	memset(filepath, 0, MAX_PATH);
	GetFullPathName(TARGET, MAX_PATH, filepath, NULL);

	if (!PathFileExists(filepath))
	{
		printf("\nERROR: failed to get path to " TARGET "\n");
		pause();
		return 0;
	}

	printf("Injecting %s...\n", filepath);

	if (!Inject(process, filepath))
	{
		printf("\nERROR: failed to inject " TARGET "\n");
		pause();
		return 0;
	}

	CloseHandle(process);

	printf("\nSuccess! The injector will close in 3 seconds...\n");

	Sleep(3000);

	return 0;
}