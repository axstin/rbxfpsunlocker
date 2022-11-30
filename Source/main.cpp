#include <Windows.h>
#include <iostream>
#include <vector>
#include <codecvt>
#include <unordered_map>
#include <chrono>
#include <TlHelp32.h>

#pragma comment(lib, "Shlwapi.lib")
#include <Shlwapi.h>

#include "ui.h"
#include "settings.h"
#include "rfu.h"
#include "procutil.h"
#include "sigscan.h"

HANDLE SingletonMutex;

std::vector<HANDLE> GetRobloxProcesses(bool include_client = true, bool include_studio = true)
{
	std::vector<HANDLE> result;
	if (include_client)
	{
		for (HANDLE handle : ProcUtil::GetProcessesByImageName("RobloxPlayerBeta.exe"))
		{
			// Roblox has a security daemon process that runs under the same name as the client (as of 3/2/22 update). Don't unlock it.
			BOOL debugged = FALSE;
			CheckRemoteDebuggerPresent(handle, &debugged);
			if (!debugged) result.emplace_back(handle);
		}
		for (HANDLE handle : ProcUtil::GetProcessesByImageName("Windows10Universal.exe")) result.emplace_back(handle);
	}
	if (include_studio) for (HANDLE handle : ProcUtil::GetProcessesByImageName("RobloxStudioBeta.exe")) result.emplace_back(handle);
	return result;
}

HANDLE GetRobloxProcess()
{
	auto processes = GetRobloxProcesses();

	if (processes.empty())
		return NULL;

	if (processes.size() == 1)
		return processes[0];

	printf("Multiple processes found! Select a process to inject into (%u - %zu):\n", 1, processes.size());
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
			printf("Please enter a number between %u and %zu\n", 1, processes.size());
			continue;
		}

		break;
	}

	return processes[selection - 1];
}

void NotifyError(const char* title, const char* error)
{
	if (Settings::SilentErrors || Settings::NonBlockingErrors)
	{
		// lol
		HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFO info{};
		GetConsoleScreenBufferInfo(console, &info);

		WORD color = (info.wAttributes & 0xFF00) | FOREGROUND_RED | FOREGROUND_INTENSITY;
		SetConsoleTextAttribute(console, color);

		printf("[ERROR] %s\n", error);

		SetConsoleTextAttribute(console, info.wAttributes);

		if (!Settings::SilentErrors)
		{
			UI::SetConsoleVisible(true);
		}
	}
	else
	{
		MessageBoxA(UI::Window, error, title, MB_OK);
	}
}

struct RobloxProcess
{
	HANDLE handle = NULL;
	ProcUtil::ModuleInfo main_module{};
	const void *ts_ptr = nullptr; // task scheduler pointer
	const void *fd_ptr = nullptr; // frame delay pointer

	int retries_left = 0;

	bool Attach(HANDLE process, int retry_count)
	{
		handle = process;
		retries_left = retry_count;

		if (!BlockingLoadModuleInfo())
		{
			NotifyError("rbxfpsunlocker Error", "Failed to get process base! Restart Roblox FPS Unlocker or, if you are on a 64-bit operating system, make sure you are using the 64-bit version of Roblox FPS Unlocker.");
			retries_left = -1;
			return false;
		}
		else
		{
			printf("[%p] Process base: %p (size %zu)\n", handle, main_module.base, main_module.size);

			// Small windows exist where we can attach to Roblox's security daemon while it isn't being debugged (see GetRobloxProcesses)
			// As a secondary measure, check module size (daemon is about 1MB, client is about 80MB)
			if (main_module.size < 1024 * 1024 * 10)
			{
				printf("[%p] Ignoring security daemon process\n", handle);
				retries_left = -1;
				return false;
			}

			Tick();

			return ts_ptr != nullptr && fd_ptr != nullptr;
		}
	}

	bool BlockingLoadModuleInfo()
	{
		int tries = 5;
		int wait_time = 100;

		printf("[%p] Finding process base...\n", handle);

		while (true)
		{
			ProcUtil::ProcessInfo info = ProcUtil::ProcessInfo(handle);

			if (info.module.base != nullptr)
			{
				main_module = info.module;
				return true;
			}

			if (tries--)
			{
				printf("[%p] Retrying in %dms...\n", handle, wait_time);
				Sleep(wait_time);
				wait_time *= 2;
			} else
			{
				return false;
			}
		}
	}

	const void *FindTaskScheduler() const
	{
		try
		{
			const auto start = (const uint8_t *)main_module.base;
			const auto end = start + main_module.size;

			if (ProcUtil::IsProcess64Bit(handle))
			{
				// 40 53 48 83 EC 20 0F B6 D9 E8 ?? ?? ?? ?? 86 58 04 48 83 C4 20 5B C3
				if (auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x40\x53\x48\x83\xEC\x20\x0F\xB6\xD9\xE8\x00\x00\x00\x00\x86\x58\x04\x48\x83\xC4\x20\x5B\xC3", "xxxxxxxxxx????xxxxxxxxx", start, end))
				{
					auto gts_fn = result + 14 + ProcUtil::Read<int32_t>(handle, result + 10);

					printf("[%p] GetTaskScheduler (sig studio): %p\n", handle, gts_fn);

					uint8_t buffer[0x100];
					if (ProcUtil::Read(handle, gts_fn, buffer, sizeof(buffer)))
					{
						if (auto inst = sigscan::scan("\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4\x28", "xxx????xxxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
						{
							const uint8_t *remote = gts_fn + (inst - buffer);
							return remote + 7 + *(int32_t *)(inst + 3);
						}
					}
				}
			} else
			{
				// 55 8B EC 83 E4 F8 83 EC 08 E8 ?? ?? ?? ?? 8D 0C 24
				if (auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x55\x8B\xEC\x83\xE4\xF8\x83\xEC\x08\xE8\xDE\xAD\xBE\xEF\x8D\x0C\x24", "xxxxxxxxxx????xxx", start, end))
				{
					auto gts_fn = result + 14 + ProcUtil::Read<int32_t>(handle, result + 10);

					printf("[%p] GetTaskScheduler (sig ltcg): %p\n", handle, gts_fn);

					uint8_t buffer[0x100];
					if (ProcUtil::Read(handle, gts_fn, buffer, sizeof(buffer)))
					{
						if (auto inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
						{
							//printf("[%p] Inst: %p\n", process, gts_fn + (inst - buffer));
							return (const void *)(*(uint32_t *)(inst + 1));
						}
					}
				}
				// 55 8B EC 83 EC 10 56 E8 ?? ?? ?? ?? 8B F0 8D 45 F0
				else if (auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x55\x8B\xEC\x83\xEC\x10\x56\xE8\x00\x00\x00\x00\x8B\xF0\x8D\x45\xF0", "xxxxxxxx????xxxxx", start, end))
				{
					auto gts_fn = result + 12 + ProcUtil::Read<int32_t>(handle, result + 8);

					printf("[%p] GetTaskScheduler (sig non-ltcg): %p\n", handle, gts_fn);

					uint8_t buffer[0x100];
					if (ProcUtil::Read(handle, gts_fn, buffer, sizeof(buffer)))
					{
						if (auto inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
						{
							//printf("[%p] Inst: %p\n", process, gts_fn + (inst - buffer));
							return (const void *)(*(uint32_t *)(inst + 1));
						}
					}
				}
				// 55 8B EC 83 E4 F8 83 EC 14 56 E8 ?? ?? ?? ?? 8D 4C 24 10
				else if (auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x55\x8B\xEC\x83\xE4\xF8\x83\xEC\x14\x56\xE8\x00\x00\x00\x00\x8D\x4C\x24\x10", "xxxxxxxxxxx????xxxx", start, end))
				{
					auto gts_fn = result + 15 + ProcUtil::Read<int32_t>(handle, result + 11);

					printf("[%p] GetTaskScheduler (sig uwp): %p\n", handle, gts_fn);

					uint8_t buffer[0x100];
					if (ProcUtil::Read(handle, gts_fn, buffer, sizeof(buffer)))
					{
						if (auto inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
						{
							return (const void *)(*(uint32_t *)(inst + 1));
						}
					}
				}
			}
		}
		catch (ProcUtil::WindowsException &e)
		{
		}

		return nullptr;
	}

	size_t FindTaskSchedulerFrameDelayOffset(const void *scheduler) const
	{
		const size_t search_offset = 0x100; // ProcUtil::IsProcess64Bit(process) ? 0x200 : 0x100;

		uint8_t buffer[0x100];
		if (!ProcUtil::Read(handle, (const uint8_t *)scheduler + search_offset, buffer, sizeof(buffer)))
			return -1;

		/* Find the frame delay variable inside TaskScheduler (ugly, but it should survive updates unless the variable is removed or shifted)
		   (variable was at +0x150 (32-bit) and +0x180 (studio 64-bit) as of 2/13/2020) */
		for (int i = 0; i < sizeof(buffer) - sizeof(double); i += 4)
		{
			static const double frame_delay = 1.0 / 60.0;
			double difference = *(double *)(buffer + i) - frame_delay;
			difference = difference < 0 ? -difference : difference;
			if (difference < std::numeric_limits<double>::epsilon()) return search_offset + i;
		}

		return -1;
	}

	void Tick()
	{
		if (retries_left < 0)
			return; // we tried

		if (!ts_ptr)
		{
			const auto start_time = std::chrono::steady_clock::now();
			ts_ptr = FindTaskScheduler();

			if (!ts_ptr)
			{
				if (retries_left-- <= 0)
					NotifyError("rbxfpsunlocker Error", "Unable to find TaskScheduler! This is probably due to a Roblox update-- watch the github for any patches or a fix.");
				return;
			}
			else
			{
				const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
				printf("[%p] Found TaskScheduler address in %lldms\n", handle, elapsed);
			}
		}

		if (ts_ptr && !fd_ptr)
		{
			try
			{
				if (auto scheduler = (const uint8_t *)(ProcUtil::ReadPointer(handle, ts_ptr)))
				{
					printf("[%p] Scheduler: %p\n", handle, scheduler);

					size_t delay_offset = FindTaskSchedulerFrameDelayOffset(scheduler);
					if (delay_offset == -1)
					{
						if (retries_left-- <= 0)
							NotifyError("rbxfpsunlocker Error", "Variable scan failed! Make sure your framerate is at ~60.0 FPS (press Shift+F5 in-game) before using Roblox FPS Unlocker.");
						return;
					}

					printf("[%p] Frame delay offset: %zu (0x%zx)\n", handle, delay_offset, delay_offset);

					fd_ptr = scheduler + delay_offset;

					SetFPSCap(Settings::FPSCap);
				}
				else
				{
					printf("[%p] *ts_ptr == nullptr\n", handle);
				}
			}
			catch (ProcUtil::WindowsException& e)
			{
				printf("[%p] RobloxProcess::Tick failed: %s (%d)\n", handle, e.what(), e.GetLastError());
				if (retries_left-- <= 0)
					NotifyError("rbxfpsunlocker Error", "An exception occurred while performing the variable scan.");
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

void pause()
{
	printf("Press enter to continue . . .");
	getchar();
}

DWORD WINAPI WatchThread(LPVOID)
{
	printf("Watch thread started\n");

	while (1)
	{
		auto processes = GetRobloxProcesses(Settings::UnlockClient, Settings::UnlockStudio);

		for (auto& process : processes)
		{
			DWORD id = GetProcessId(process);

			if (AttachedProcesses.find(id) == AttachedProcesses.end())
			{
				printf("Injecting into new process %p (pid %d)\n", process, id);
				RobloxProcess roblox_process;

				roblox_process.Attach(process, 3);

				AttachedProcesses[id] = roblox_process;

				printf("New size: %zu\n", AttachedProcesses.size());
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
				printf("Purging dead process %p (pid %d, code %X)\n", process, GetProcessId(process), code);
				it = AttachedProcesses.erase(it);
				CloseHandle(process);
				printf("New size: %zu\n", AttachedProcesses.size());
			}
			else
			{
				it->second.Tick();
				it++;
			}
		}

		UI::AttachedProcessesCount = AttachedProcesses.size();

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
		printf("Attaching...\n");

		if (!RobloxProcess().Attach(process, 0))
		{
			printf("\nERROR: unable to attach to process\n");
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
			if (!Settings::QuickStart)
				UI::ToggleConsole();
			else
				UI::CreateHiddenConsole();

			if (Settings::CheckForUpdates)
			{
				printf("Checking for updates...\n");
				if (CheckForUpdates()) return 0;
			}

			if (!Settings::QuickStart)
			{
				printf("Minimizing to system tray in 2 seconds...\n");
				Sleep(2000);
				UI::ToggleConsole();
			}

			return UI::Start(hInstance, WatchThread);
		}
	}
} 
