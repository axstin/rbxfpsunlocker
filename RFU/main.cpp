#include <Windows.h>
#include <iostream>
#include <vector>
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

std::vector<HANDLE> GetRobloxProcesses(bool include_studio = true)
{
	std::vector<HANDLE> result;
	for (auto handle : ProcUtil::GetProcessesByImageName(L"RobloxPlayerBeta.exe")) result.emplace_back(handle);
	if (include_studio) for (auto handle : ProcUtil::GetProcessesByImageName(L"RobloxStudioBeta.exe")) result.emplace_back(handle);
	for (auto handle : ProcUtil::GetProcessesByImageName(L"Win10Universal.exe")) result.emplace_back(handle);
	return result;
}

HANDLE GetRobloxProcess()
{
	auto processes = GetRobloxProcesses();

	if (processes.empty())
		return nullptr;

	if (processes.size() == 1)
		return processes[0];

	printf("Multiple processes found! Select a process to inject into (%u - %zu):\n", 1, processes.size());
	for (auto i = 0; i < processes.size(); i++)
	{
		try
		{
			ProcUtil::ProcessInfo info(processes[i], true);
			printf("[%d] [%s] %s\n", i + 1, info.name.c_str(), info.window_title.c_str());
		}
		catch (ProcUtil::WindowsException& e)
		{
			printf("[%d] Invalid process %p (%s, %lX)\n", i + 1, processes[i], e.what(), e.GetLastError());
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

size_t FindTaskSchedulerFrameDelayOffset(HANDLE process, const void *scheduler)
{
	const size_t search_offset = 0x100; // ProcUtil::IsProcess64Bit(process) ? 0x200 : 0x100;

	uint8_t buffer[0x100];
	if (!ProcUtil::Read(process, static_cast<const uint8_t*>(scheduler) + search_offset, buffer, sizeof buffer))
		return -1;

	/* Find the frame delay variable inside TaskScheduler (ugly, but it should survive updates unless the variable is removed or shifted)
	   (variable was at +0x150 (32-bit) and +0x180 (studio 64-bit) as of 2/13/2020) */
	for (auto i = 0; i < sizeof buffer - sizeof(double); i += 4)
	{
		static const auto frame_delay = 1.0 / 60.0;
		auto difference = *reinterpret_cast<double*>(buffer + i) - frame_delay;
		difference = difference < 0 ? -difference : difference;
		if (difference < std::numeric_limits<double>::epsilon()) return search_offset + i;
	}

	return -1;
}

const void *FindTaskScheduler(HANDLE process, const char **error = nullptr)
{
	try
	{
		ProcUtil::ProcessInfo info;

		// TODO: remove this retry code? (see RobloxProcess::Tick)
		auto tries = 5;
		auto wait_time = 100;

		while (true)
		{
			printf("[%p] Init ProcessInfo\n", process);
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

		const auto start = static_cast<const uint8_t*>(info.module.base);
		const auto end = start + info.module.size;

		printf("[%p] Process Base: %p\n", process, start);

		if (ProcUtil::IsProcess64Bit(process))
		{
			printf("[%p] Is 64bit\n", process);
			// 40 53 48 83 EC 20 0F B6 D9 E8 ?? ?? ?? ?? 86 58 04 48 83 C4 20 5B C3
			if (const auto result = static_cast<const uint8_t*>(ProcUtil::ScanProcess(
				process, "\x40\x53\x48\x83\xEC\x20\x0F\xB6\xD9\xE8\x00\x00\x00\x00\x86\x58\x04\x48\x83\xC4\x20\x5B\xC3",
				"xxxxxxxxxx????xxxxxxxxx", start, end)))
			{
				const auto gts_fn = result + 14 + ProcUtil::Read<int32_t>(process, result + 10);

				printf("[%p] GetTaskScheduler: %p\n", process, gts_fn);

				uint8_t buffer[0x100];
				if (ProcUtil::Read(process, gts_fn, buffer, sizeof buffer))
				{
					if (const auto inst = sigscan::scan("\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4\x38", "xxx????xxxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
					{
						const auto remote = gts_fn + (inst - buffer);
						return remote + 7 + *reinterpret_cast<int32_t*>(inst + 3);
					}
				}
			}
		}
		else
		{
			printf("[%p] Is 32bit\n", process);
			if (const auto result = static_cast<const uint8_t*>(ProcUtil::ScanProcess(
				process, "\x55\x8B\xEC\xE8\x00\x00\x00\x00\x8A\x4D\x08\x83\xC0\x04\x86\x08\x5D\xC3", "xxxx????xxxxxxxxxx",
				start, end)))
			{
				const auto gts_fn = result + 8 + ProcUtil::Read<int32_t>(process, result + 4);

				printf("[%p] GetTaskScheduler: %p\n", process, gts_fn);

				uint8_t buffer[0x100];
				if (ProcUtil::Read(process, gts_fn, buffer, sizeof buffer))
				{
					if (const auto inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
					{
						//printf("[%p] Inst: %p\n", process, gts_fn + (inst - buffer));
						return reinterpret_cast<const void*>(*reinterpret_cast<uint32_t*>(inst + 1));
					}
				}
			}
		}
	}
	catch (ProcUtil::WindowsException& e)
	{
		printf("[%p] WindowsException occurred, GetLastError() = %d\n", process, GetLastError());
	}

	return nullptr;
}

void NotifyError(const char* title, const char* error)
{
	if (Settings::SilentErrors || Settings::NonBlockingErrors)
	{
		// lol
		const auto console = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFO info{};
		GetConsoleScreenBufferInfo(console, &info);

		const WORD color = info.wAttributes & 0xFF00 | FOREGROUND_RED | FOREGROUND_INTENSITY;
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
	HANDLE handle = nullptr;
	const void *ts_ptr = nullptr; // task scheduler pointer
	const void *fd_ptr = nullptr; // frame delay pointer

	int retries_left = 0;

	bool Attach(HANDLE process, int retry_count)
	{
		handle = process;
		retries_left = retry_count;

		Tick();

		return ts_ptr != nullptr && fd_ptr != nullptr;
	}

	void Tick()
	{
		if (retries_left < 0) return; // we tried

		if (!ts_ptr)
		{
			const char* error = nullptr;
			ts_ptr = FindTaskScheduler(handle, &error);

			if (!ts_ptr)
			{
				if (error) retries_left = 0; // if FindTaskScheduler returned an error it already retried 5 times. TODO: remove
				if (retries_left-- <= 0)
					NotifyError("rbxfpsunlocker Error", error ? error : "Unable to find TaskScheduler! This is probably due to a Roblox update-- watch the github for any patches or a fix.");
				return;
			}
		}

		if (ts_ptr && !fd_ptr)
		{
			try
			{
				if (const auto scheduler = static_cast<const uint8_t*>(ProcUtil::ReadPointer(handle, ts_ptr)))
				{
					printf("[%p] Scheduler: %p\n", handle, scheduler);

					const auto delay_offset = FindTaskSchedulerFrameDelayOffset(handle, scheduler);
					if (delay_offset == -1)
					{
						if (retries_left-- <= 0)
							NotifyError("rbxfpsunlocker Error", "Variable scan failed! This is probably due to a Roblox update-- watch the github for any patches or a fix.");
						return;
					}

					printf("[%p] Frame Delay Offset: %zu\n", handle, delay_offset);

					fd_ptr = scheduler + delay_offset;

					SetFPSCap(Settings::FPSCap);
				}
			}
			catch (ProcUtil::WindowsException& e)
			{
				printf("[%p] RobloxProcess::Tick failed: %s (%lu)\n", handle, e.what(), e.GetLastError());
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
				static const auto min_frame_delay = 1.0 / 10000.0;
				const auto frame_delay = cap <= 0.0 ? min_frame_delay : 1.0 / cap;

				ProcUtil::Write(handle, fd_ptr, frame_delay);
			}
			catch (ProcUtil::WindowsException &e)
			{
				printf("[%p] RobloxProcess::SetFPSCap failed: %s (%lu)\n", handle, e.what(), e.GetLastError());
			}
		}
	}
};

std::unordered_map<DWORD, RobloxProcess> AttachedProcesses;

void SetFPSCapExternal(const double value)
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

	while (true)
	{
		auto processes = GetRobloxProcesses(Settings::UnlockStudio);

		for (auto& process : processes)
		{
			auto id = GetProcessId(process);

			if (AttachedProcesses.find(id) == AttachedProcesses.end())
			{
				printf("Injecting into new process %p (pid %lu)\n", process, id);
				RobloxProcess roblox_process;

				roblox_process.Attach(process, 2);

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
			const auto process = it->second.handle;

			DWORD code;
			GetExitCodeProcess(process, &code);

			if (code != STILL_ACTIVE)
			{
				printf("Purging dead process %p (pid %lu) (code %lX)\n", process, GetProcessId(process), code);
				it = AttachedProcesses.erase(it);
				CloseHandle(process);
				printf("New size: %zu\n", AttachedProcesses.size());
			}
			else
			{
				it->second.Tick();
				++it;
			}
		}

		UI::AttachedProcessesCount = AttachedProcesses.size();

		Sleep(2000);
	}
}

bool CheckRunning()
{
	SingletonMutex = CreateMutexA(nullptr, FALSE, "RFUMutex");

	if (!SingletonMutex)
	{
		MessageBoxA(nullptr, "Unable to create mutex", "Error", MB_OK);
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
		MessageBoxA(nullptr, buffer, "Error", MB_OK);
		return 0;
	}

	UI::IsConsoleOnly = strstr(lpCmdLine, "--console") != nullptr;
	UI::IsSilent = strstr(lpCmdLine, "--silent") != nullptr;

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
			MessageBoxA(nullptr, "Roblox FPS Unlocker is already running", "Error", MB_OK);
		}
		else
		{
			// if we aren't silent, we need to show the console initially for output
			if (!UI::IsSilent)
			{
				UI::ToggleConsole();
			}

			// check for updates regardless
			if (Settings::CheckForUpdates)
			{
				printf("Checking for updates...\n");
				if (CheckForUpdates()) return 0;
			}

			// and carry on.
			if (!UI::IsSilent) {
				printf("Minimizing to system tray in 2 seconds...\n");
				Sleep(2000);

				UI::ToggleConsole();
			}

			return UI::Start(hInstance, WatchThread);
		}
	}
}