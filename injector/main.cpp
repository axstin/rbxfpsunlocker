#include <Windows.h>
#include <iostream>
#include <vector>
#include <TlHelp32.h>

#pragma comment(lib, "Shlwapi.lib")
#include <Shlwapi.h>

std::vector<HANDLE> GetProcessesByImageName(const char* imageName, size_t limit = -1)
{
	std::vector<HANDLE> result;

	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	size_t count = 0;

	if (Process32First(snapshot, &entry) == TRUE)
	{
		while (count < limit && Process32Next(snapshot, &entry) == TRUE)
		{
			if (_stricmp(entry.szExeFile, imageName) == 0)
			{
				result.push_back(OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID));
				count++;
			}
		}
	}

	CloseHandle(snapshot);
	return result;
}

HANDLE GetProcessByImageName(const char* imageName)
{
	return GetProcessesByImageName(imageName, 1)[0];
}

struct ProcessInfo
{
	HANDLE handle;
	std::string name;
	HWND window;
	std::string window_title;

	ProcessInfo(HANDLE handle, const std::string& name = "")
	: handle(handle), name(name), window(NULL), window_title("")
	{
		BOOL result = EnumWindows([](HWND window, LPARAM param) -> BOOL
		{
			ProcessInfo* info = (ProcessInfo*)param;

			DWORD process_id;
			GetWindowThreadProcessId(window, &process_id);

			if (IsWindowVisible(window) && process_id == GetProcessId(info->handle))
			{
				char title[256] = { 0 };
				GetWindowTextA(window, title, sizeof(title));

				info->window = window;
				info->window_title = title;
				return FALSE;
			}

			return TRUE;
		}, (LPARAM)this);
	}
};

std::vector<ProcessInfo> GetRobloxProcesses()
{
	std::vector<ProcessInfo> result;
	for (HANDLE handle : GetProcessesByImageName("RobloxPlayerBeta.exe")) result.emplace_back(ProcessInfo(handle, "RobloxPlayerBeta.exe"));
	for (HANDLE handle : GetProcessesByImageName("RobloxStudioBeta.exe")) result.emplace_back(ProcessInfo(handle, "RobloxStudioBeta.exe"));
	return result;
}

HANDLE GetRobloxProcess()
{
	auto processes = GetRobloxProcesses();

	if (processes.empty())
		return NULL;

	if (processes.size() == 1)
		return processes[0].handle;

	printf("Multiple processes found! Select a process to inject into (%d - %d):\n", 1, processes.size());
	for (int i = 0; i < processes.size(); i++)
	{
		auto& info = processes[i];
		printf("[%d] [%s] %s\n", i + 1, info.name.c_str(), info.window_title.c_str());
	}

	int selection;

	while (true)
	{
		printf("\n>");
		std::cin >> selection;

		if (std::cin.fail())
		{
			std::cin.clear();
			std::cin.ignore(std::cin.rdbuf()->in_avail());
			printf("Invalid input, try again\n");
			continue;
		}

		if (selection < 1 || selection > processes.size())
		{
			printf("Please enter a number between %d and %d\n", 1, processes.size());
			continue;
		}

		break;
	}

	return processes[selection - 1].handle;
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

int main()
{
	SetConsoleTitle("rbxfpsunlocker Injector");

	printf("Waiting for Roblox...\n");

	HANDLE process;

	do
	{
		Sleep(100);
		process = GetRobloxProcess();
	} while (!process);

	printf("Found Roblox...\n");

	char filepath[MAX_PATH];
	memset(filepath, 0, MAX_PATH);
	GetFullPathName("rbxfpsunlocker.dll", MAX_PATH, filepath, NULL);

	if (!PathFileExists(filepath))
	{
		printf("\nERROR: failed to get path to rbxfpsunlocker.dll\n");
		pause();
		return 0;
	}

	printf("Injecting %s...\n", filepath);

	if (!Inject(process, filepath))
	{
		printf("\nERROR: failed to inject rbxfpsunlocker.dll\n");
		pause();
		return 0;
	}

	CloseHandle(process);

	printf("\nSuccess! The injector will close in 3 seconds...\n");

	Sleep(3000);

	return 0;
}