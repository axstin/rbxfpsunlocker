#include <Windows.h>
#include <iostream>
#include <vector>
#include <codecvt>
#include <TlHelp32.h>

#pragma comment(lib, "Shlwapi.lib")
#include <Shlwapi.h>

#include "ui.h"
#include "settings.h"
#include "version.h"

#define USE_BLACKBONE 1

#if USE_BLACKBONE

#include "BlackBone\Process\Process.h"

#endif

HANDLE SingletonMutex;

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
	auto processes = GetProcessesByImageName(imageName, 1);
	return processes.size() > 0 ? processes[0] : NULL;
}

struct ProcessInfo
{
	HANDLE handle;
	DWORD id;
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

		id = GetProcessId(handle);
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

bool Inject(HANDLE process, const char* dll_name)
{
#if USE_BLACKBONE
	blackbone::Process bbproc;
	bbproc.Attach(GetProcessId(process)); // blackbone invalidates/closes the handle when bbproc goes out of scope so we can't pass 'process'

	// As of 1/25/2019, Roblox scans memory for initially-executable pages beginning with "MZ" (the PE file signature) and likely flags/logs any manually mapped or hidden modules (such as rbxfpsunlocker.dll)
	// Wiping the header should solve this
	auto image = bbproc.mmap().MapImage(blackbone::Utils::AnsiToWstring(dll_name), blackbone::eLoadFlags::WipeHeader);

	if (!image)
	{
		wprintf(L"Blackbone: Manual map failed with status: %X (%s)\n", image.status, blackbone::Utils::GetErrorDescription(image.status).c_str());
		return false;
	}

	printf("Blackbone: Success! Base address: %X\n", image.result()->baseAddress);
	return true;
#else
	LPVOID loadlib = (LPVOID)GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryA");
	LPVOID remotestring = (LPVOID)VirtualAllocEx(process, NULL, strlen(dll_name), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	WriteProcessMemory(process, (LPVOID)remotestring, dll_name, strlen(dll_name), NULL);
	return CreateRemoteThread(process, NULL, NULL, (LPTHREAD_START_ROUTINE)loadlib, (LPVOID)remotestring, NULL, NULL) != NULL;
#endif
}

void pause()
{
	printf("Press any key to continue . . .");
	getchar();
}

std::unordered_map<DWORD, HANDLE> AttachedProcesses;

DWORD WINAPI WatchThread(LPVOID)
{
	char dllpath[MAX_PATH];
	GetFullPathName("rbxfpsunlocker.dll", MAX_PATH, dllpath, NULL);

	if (!PathFileExists(dllpath))
	{
		MessageBoxA(UI::Window, "Unable to get path to rbxfpsunlocker.dll", "Error", MB_OK);
		return 0;
	}

	printf("Watch thread started\n");

	while (1)
	{
		auto processes = GetProcessesByImageName("RobloxPlayerBeta.exe"); // no studio for now

		for (auto& process : processes)
		{
			DWORD id = GetProcessId(process);

			if (AttachedProcesses.find(id) == AttachedProcesses.end())
			{
				printf("Injecting into new process %X (pid %d)\n", process, id);

				if (!Inject(process, dllpath))
				{
					MessageBoxA(UI::Window, "Failed to inject rbxfpsunlocker.dll", "Error", MB_OK);
				}

				AttachedProcesses[id] = process;

				printf("New size: %d\n", AttachedProcesses.size());
			}
			else
			{
				CloseHandle(process);
			}
		}

		for (auto it = AttachedProcesses.begin(); it != AttachedProcesses.end();)
		{
			HANDLE process = it->second;

			DWORD code;
			BOOL result = GetExitCodeProcess(process, &code);

			if (code != STILL_ACTIVE)
			{
				printf("Purging dead process %X (pid %d) (code %d)\n", process, GetProcessId(process), code);
				it = AttachedProcesses.erase(it);
				CloseHandle(process);
				printf("New size: %d\n", AttachedProcesses.size());
			}
			else
			{
				it++;
			}
		}

		UI::AttachedProcessesCount = AttachedProcesses.size();

		if (AttachedProcesses.size() == 1)
		{
			static int last_present_count = 0;
			auto& debug = Settings::GetIPC()->debug;
			double fps = (debug.present_count - last_present_count) / 2.0;

			printf("\rscan: +0x%X, sched: %X, offset: +0x%X, present_count: %d, avg fps: %f", debug.scan_result, debug.scheduler, debug.sfd_offset, debug.present_count, fps);

			last_present_count = debug.present_count;
		}

		Sleep(2000);
	}

	return 0;
}

bool CheckRunning()
{
	SingletonMutex = CreateMutexA(NULL, FALSE, "RFUMutex");

	if (!SingletonMutex)
	{
		MessageBoxA(NULL, "Unable to create mutex", "Error", MB_OK);
		return false;
	}

	return GetLastError() == ERROR_ALREADY_EXISTS;
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	if (!Settings::Init())
	{
		char buffer[64];
		sprintf_s(buffer, "Unable to initiate settings\nGetLastError() = %X", GetLastError());
		MessageBoxA(NULL, buffer, "Error", MB_OK);
		return 0;
	}

	UI::IsConsoleOnly = strstr(lpCmdLine, "--console") != nullptr;

	if (UI::IsConsoleOnly)
	{
		UI::ToggleConsole();

		printf("Waiting for Roblox...\n");

		HANDLE process;

		do
		{
			Sleep(100);
			process = GetRobloxProcess();
		}
		while (!process);

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
	else
	{
		if (CheckRunning())
		{
			MessageBoxA(NULL, "Roblox FPS Unlocker is already running", "Error", MB_OK);
		}
		else
		{
			UI::ToggleConsole();

			printf("Checking for updates...\n");
			if (CheckForUpdates()) return 0;

			printf("Minimizing to system tray in 2 seconds...\n");
			Sleep(2000);

			UI::ToggleConsole();

			return UI::Start(hInstance, WatchThread);
		}
	}
}