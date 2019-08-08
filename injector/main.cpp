#include <Windows.h>
#include <iostream>
#include <vector>
#include <codecvt>
#include <unordered_map>
#include <TlHelp32.h>

#pragma comment(lib, "Shlwapi.lib")
#include <Shlwapi.h>

#include "ui.h"
#include "settings.h"
#include "rfu.h"
#include "procutil.h"
#include "../rbxfpsunlocker/sigscan.h"

#ifndef RFU_NO_DLL
#define USE_BLACKBONE
#endif

#ifdef USE_BLACKBONE
#include "BlackBone\Process\Process.h"
#pragma comment(lib, "BlackBone.lib")
#endif

HANDLE SingletonMutex;

std::vector<HANDLE> GetRobloxProcesses(bool include_studio = true)
{
	std::vector<HANDLE> result;
	for (HANDLE handle : ProcUtil::GetProcessesByImageName("RobloxPlayerBeta.exe")) result.emplace_back(handle);
	if (include_studio) for (HANDLE handle : ProcUtil::GetProcessesByImageName("RobloxStudioBeta.exe")) result.emplace_back(handle);
	for (HANDLE handle : ProcUtil::GetProcessesByImageName("Win10Universal.exe")) result.emplace_back(handle);
	return result;
}

HANDLE GetRobloxProcess()
{
	auto processes = GetRobloxProcesses();

	if (processes.empty())
		return NULL;

	if (processes.size() == 1)
		return processes[0];

	printf("Multiple processes found! Select a process to inject into (%d - %d):\n", 1, processes.size());
	for (int i = 0; i < processes.size(); i++)
	{
		try
		{
			ProcUtil::ProcessInfo info(processes[i], true);
			printf("[%d] [%s] %s\n", i + 1, info.name.c_str(), info.window_title.c_str());
		}
		catch (ProcUtil::WindowsException& e)
		{
			printf("[%d] Invalid process %p (%s, %X)\n", i + 1, processes[i], e.what(), e.GetLastError());
		}
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

	return processes[selection - 1];
}

#ifndef RFU_NO_DLL

struct RobloxProcess
{
	HANDLE handle = NULL;

	bool Attach(HANDLE process, const char *dll_name)
	{
		handle = process;

#ifdef USE_BLACKBONE
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
};

std::unordered_map<DWORD, RobloxProcess> AttachedProcesses;

#else

size_t FindTaskSchedulerFrameDelayOffset(HANDLE process, const void *scheduler)
{
	uint8_t buffer[0x100];
	if (!ProcUtil::Read(process, (const uint8_t *)scheduler + 0x200, buffer, sizeof(buffer)))
		return -1;

	/* Find the frame delay variable inside TaskScheduler (ugly, but it should survive updates unless the variable is removed or shifted) (variable was at +0x228 as of 10/11/2018) */
	for (int i = 0; i < sizeof(buffer) - sizeof(double); i += 4)
	{
		static const double frame_delay = 1.0 / 60.0;
		double difference = *(double *)(buffer + i) - frame_delay;
		difference = difference < 0 ? -difference : difference;
		if (difference < 0.004) return 0x200 + i;
	}

	return -1;
}

const void *FindTaskScheduler(HANDLE process, const char **error = nullptr)
{
	try
	{
		ProcUtil::ProcessInfo info;

		int tries = 5;
		int wait_time = 100;

		while (true)
		{
			info = ProcUtil::ProcessInfo(process);
			if (info.module.base != nullptr)
				break;

			if (tries--)
			{
				printf("[%p] Retrying in %dms...\n", process, wait_time);
				Sleep(wait_time);
				wait_time *= 2;
			}
			else
			{
				if (error) *error = "Failed to get process base! Restart Roblox FPS Unlocker or, if you are on a 64-bit operating system, make sure you are using the 64-bit version of Roblox FPS Unlocker.";
				return nullptr;
			}
		}

		auto start = (const uint8_t *)info.module.base;
		auto end = start + info.module.size;

		printf("[%p] Process Base: %p\n", process, start);

		if (ProcUtil::IsProcess64Bit(process))
		{
			// 40 53 48 83 EC 20 0F B6 D9 E8 ?? ?? ?? ?? 86 58 04 48 83 C4 20 5B C3
			if (auto result = (const uint8_t *)ProcUtil::ScanProcess(process, "\x40\x53\x48\x83\xEC\x20\x0F\xB6\xD9\xE8\x00\x00\x00\x00\x86\x58\x04\x48\x83\xC4\x20\x5B\xC3", "xxxxxxxxxx????xxxxxxxxx", start, end))
			{
				auto gts_fn = result + 14 + ProcUtil::Read<int32_t>(process, result + 10);

				printf("[%p] GetTaskScheduler: %p\n", process, gts_fn);

				uint8_t buffer[0x100];
				if (ProcUtil::Read(process, gts_fn, buffer, sizeof(buffer)))
				{
					if (auto inst = sigscan::scan("\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4\x38", "xxx????xxxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
					{
						const uint8_t *remote = gts_fn + (inst - buffer);
						return remote + 7 + *(int32_t *)(inst + 3);
					}
				}
			}
		}
		else
		{
			if (auto result = (const uint8_t *)ProcUtil::ScanProcess(process, "\x55\x8B\xEC\xE8\x00\x00\x00\x00\x8A\x4D\x08\x83\xC0\x04\x86\x08\x5D\xC3", "xxxx????xxxxxxxxxx", start, end))
			{
				auto gts_fn = result + 8 + ProcUtil::Read<int32_t>(process, result + 4);

				printf("[%p] GetTaskScheduler: %p\n", process, gts_fn);

				uint8_t buffer[0x100];
				if (ProcUtil::Read(process, gts_fn, buffer, sizeof(buffer)))
				{
					if (auto inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
					{
						return (const void *)(*(uint32_t *)(inst + 1));
					}
				}
			}
		}
	}
	catch (ProcUtil::WindowsException& e)
	{
	}

	return nullptr;
}

struct RobloxProcess
{
	HANDLE handle = NULL;
	const void *ts_ptr = nullptr; // task scheduler pointer
	const void *fd_ptr = nullptr; // frame delay pointer

	bool Attach(HANDLE process)
	{
		handle = process;

		const char *error = nullptr;
		ts_ptr = FindTaskScheduler(process, &error);

		if (!ts_ptr)
		{
			MessageBoxA(UI::Window, error ? error : "Unable to find TaskScheduler! This is probably due to a Roblox update-- watch the github for any patches or a fix.", "rbxfpsunlocker Error", MB_OK);
			return false;
		}

		Tick();

		return true;
	}

	void Tick()
	{
		if (ts_ptr && !fd_ptr)
		{
			try
			{
				if (auto scheduler = (const uint8_t *)(ProcUtil::ReadPointer(handle, ts_ptr)))
				{
					printf("[%p] Scheduler: %p\n", handle, scheduler);

					size_t delay_offset = FindTaskSchedulerFrameDelayOffset(handle, scheduler);
					if (delay_offset == -1)
					{
						MessageBoxA(UI::Window, "Variable scan failed! This is probably due to a Roblox update-- watch the github for any patches or a fix.", "rbxfpsunlocker Error", MB_OK);
						ts_ptr = nullptr;
						return;
					}

					printf("[%p] Frame Delay Offset: %d\n", handle, delay_offset);

					fd_ptr = scheduler + delay_offset;

					SetFPSCap(Settings::FPSCap);
				}
			}
			catch (ProcUtil::WindowsException& e)
			{
				printf("[%p] RobloxProcess::Tick failed: %s (%d)\n", handle, e.what(), e.GetLastError());
				ts_ptr = nullptr;
			}
		}
	}

	void SetFPSCap(double cap)
	{
		if (fd_ptr)
		{
			try
			{
				static const double min_frame_delay = 1.0 / 10000.0;
				double frame_delay = cap <= 0.0 ? min_frame_delay : 1.0 / cap;

				ProcUtil::Write(handle, fd_ptr, frame_delay);
			}
			catch (ProcUtil::WindowsException &e)
			{
				printf("[%p] RobloxProcess::SetFPSCap failed: %s (%d)\n", handle, e.what(), e.GetLastError());
			}
		}
	}
};

std::unordered_map<DWORD, RobloxProcess> AttachedProcesses;

void SetFPSCapExternal(double value)
{
	for (auto& it : AttachedProcesses)
	{
		it.second.SetFPSCap(value);
	}
}

#endif

void pause()
{
	printf("Press enter to continue . . .");
	getchar();
}

DWORD WINAPI WatchThread(LPVOID)
{
#ifndef RFU_NO_DLL
	char dllpath[MAX_PATH];
	GetFullPathName("rbxfpsunlocker.dll", MAX_PATH, dllpath, NULL);

	if (!PathFileExists(dllpath))
	{
		MessageBoxA(UI::Window, "Unable to get path to rbxfpsunlocker.dll", "Error", MB_OK);
		return 0;
	}
#endif

	printf("Watch thread started\n");

	while (1)
	{
#ifndef RFU_NO_DLL
		auto processes = ProcUtil::GetProcessesByImageName("RobloxPlayerBeta.exe");
#else
		auto processes = GetRobloxProcesses(Settings::UnlockStudio);
#endif

		for (auto& process : processes)
		{
			DWORD id = GetProcessId(process);

			if (AttachedProcesses.find(id) == AttachedProcesses.end())
			{
				printf("Injecting into new process %p (pid %d)\n", process, id);
				RobloxProcess roblox_process;

#ifndef RFU_NO_DLL
				if (!roblox_process.Attach(process, dllpath))
				{
					MessageBoxA(UI::Window, "Failed to inject rbxfpsunlocker.dll", "Error", MB_OK);
				}
#else
				roblox_process.Attach(process);
#endif

				AttachedProcesses[id] = roblox_process;

				printf("New size: %d\n", AttachedProcesses.size());
			}
			else
			{
				CloseHandle(process);
			}
		}

		for (auto it = AttachedProcesses.begin(); it != AttachedProcesses.end();)
		{
			HANDLE process = it->second.handle;

			DWORD code;
			BOOL result = GetExitCodeProcess(process, &code);

			if (code != STILL_ACTIVE)
			{
				printf("Purging dead process %p (pid %d) (code %X)\n", process, GetProcessId(process), code);
				it = AttachedProcesses.erase(it);
				CloseHandle(process);
				printf("New size: %d\n", AttachedProcesses.size());
			}
			else
			{
#ifdef RFU_NO_DLL
				it->second.Tick();
#endif
				it++;
			}
		}

		UI::AttachedProcessesCount = AttachedProcesses.size();

#ifndef RFU_NO_DLL
		if (AttachedProcesses.size() == 1)
		{
			static int last_present_count = 0;
			auto& debug = Settings::GetIPC()->debug;
			double fps = (debug.present_count - last_present_count) / 2.0;

			printf("\rscan: +0x%X, sched: %X, offset: +0x%X, present_count: %d, avg fps: %f", debug.scan_result, debug.scheduler, debug.sfd_offset, debug.present_count, fps);

			last_present_count = debug.present_count;
		}
#endif

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

#ifndef RFU_NO_DLL
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

		if (!RobloxProcess().Attach(process, filepath))
		{
			printf("\nERROR: failed to inject rbxfpsunlocker.dll\n");
			pause();
			return 0;
		}
#else
		printf("Attaching...\n");

		if (!RobloxProcess().Attach(process))
		{
			printf("\nERROR: unable to attach to process\n");
			pause();
			return 0;
		}
#endif

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

			if (Settings::CheckForUpdates)
			{
				printf("Checking for updates...\n");
				if (CheckForUpdates()) return 0;
			}

			printf("Minimizing to system tray in 2 seconds...\n");
			Sleep(2000);

			UI::ToggleConsole();

			return UI::Start(hInstance, WatchThread);
		}
	}
}