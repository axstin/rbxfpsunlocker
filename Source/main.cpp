#include <Windows.h>
#include <iostream>
#include <vector>
#include <codecvt>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <fstream>
#include <mutex>
#include <TlHelp32.h>
#include <winternl.h>
#include <ShlObj.h>

#pragma comment(lib, "Shlwapi.lib")
#include <Shlwapi.h>

#include "ui.h"
#include "settings.h"
#include "rfu.h"
#include "procutil.h"
#include "sigscan.h"
#include "nlohmann.hpp"

#define ROBLOX_BASIC_ACCESS (PROCESS_QUERY_INFORMATION | PROCESS_VM_READ)
#define	ROBLOX_WRITE_ACCESS (PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE)

HANDLE SingletonMutex;

enum class RobloxHandleType
{
	None,
	Client,
	UWP,
	Studio
};

struct RobloxProcessHandle
{
	DWORD id;
	RobloxHandleType type;

	std::shared_ptr<ProcUtil::ScopedHandle> shared_handle;
	bool can_write;

	RobloxProcessHandle(DWORD process_id = 0, RobloxHandleType type = RobloxHandleType::None, bool open = false) : id(process_id), shared_handle(nullptr), type(type), can_write(false)
	{
		if (open) Open();
	};

	bool IsValid() const
	{
		return id != 0;
	}

	HANDLE Handle() const
	{
		return shared_handle ? shared_handle->value : NULL;
	}

	bool IsOpen() const
	{
		return Handle() != NULL;
	}

	bool Open()
	{
		can_write = type == RobloxHandleType::Studio;
		if (HANDLE handle = OpenProcess(can_write ? ROBLOX_WRITE_ACCESS : ROBLOX_BASIC_ACCESS, FALSE, id))
		{
			shared_handle = std::make_shared<ProcUtil::ScopedHandle>(handle);
			return true;
		}
		return false;
	}

	std::filesystem::path FetchPath()
	{
		assert(IsValid());
		auto modules = ProcUtil::GetProcessModules(id, 1);
		if (!modules.empty()) return modules[0].path;
		return {};
	}

	HANDLE CreateWriteHandle() const
	{
		assert(IsOpen());
		HANDLE new_handle = NULL;
		DuplicateHandle(GetCurrentProcess(), Handle(), GetCurrentProcess(), &new_handle, ROBLOX_WRITE_ACCESS, FALSE, NULL);
		return new_handle;
	}

	template <typename T>
	void Write(const void *location, const T &value) const
	{
		assert(IsOpen());
		if (can_write)
		{
			printf("[%u] Writing to %p\n", id, location);
			ProcUtil::Write<T>(Handle(), location, value);
		}
		else
		{
			auto write_handle = CreateWriteHandle();
			if (!write_handle) throw ProcUtil::WindowsException("failed to create write handle");
			printf("[%u] Writing to %p with handle %p\n", id, location, write_handle);
			ProcUtil::Write<T>(write_handle, location, value);
			CloseHandle(write_handle);
		}
	}
};

std::vector<RobloxProcessHandle> GetRobloxProcesses(bool open_all = true, bool include_client = true, bool include_studio = true)
{
	std::vector<RobloxProcessHandle> result;
	if (include_client)
	{
		for (auto pid : ProcUtil::GetProcessIdsByImageName("RobloxPlayerBeta.exe")) result.emplace_back(pid, RobloxHandleType::Client, open_all);
		for (auto pid : ProcUtil::GetProcessIdsByImageName("Windows10Universal.exe")) result.emplace_back(pid, RobloxHandleType::UWP, open_all);
	}
	if (include_studio)
	{
		for (auto pid : ProcUtil::GetProcessIdsByImageName("RobloxStudioBeta.exe")) result.emplace_back(pid, RobloxHandleType::Studio, open_all);
	}
	return result;
}

RobloxProcessHandle GetRobloxProcess()
{
	auto processes = GetRobloxProcesses();

	if (processes.empty())
		return {};

	if (processes.size() == 1)
		return processes[0];

	printf("Multiple processes found! Select a process to inject into (%u - %zu):\n", 1, processes.size());
	for (int i = 0; i < processes.size(); i++)
	{
		try
		{
			ProcUtil::ProcessInfo info(processes[i].Handle(), true);
			printf("[%d] [%s] %s\n", i + 1, info.name.c_str(), info.window_title.c_str());
		}
		catch (ProcUtil::WindowsException& e)
		{
			printf("[%d] Invalid process %p (%s, %X)\n", i + 1, processes[i].Handle(), e.what(), e.GetLastError());
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

std::optional<int> FetchTargetFpsDiskValue(const std::filesystem::path &version_folder, nlohmann::json *object_out = nullptr)
{
	std::ifstream file(version_folder / "ClientSettings" / "ClientAppSettings.json");

	if (file.is_open())
	{
		nlohmann::json object = nlohmann::json::parse(file, nullptr, false);
		if (!object.is_discarded())
		{
			std::optional<int> result{};

			if (object.contains("DFIntTaskSchedulerTargetFps"))
			{
				auto target_fps = object["DFIntTaskSchedulerTargetFps"];
				if (target_fps.is_number_integer())
				{
					result = target_fps.get<int>();
				}
			}

			if (object_out)
				*object_out = std::move(object);

			return result;
		}
	}

	return std::nullopt;
}

bool IsTargetFpsFlagActive(const std::filesystem::path &version_folder)
{
	auto value = FetchTargetFpsDiskValue(version_folder);
	return value.has_value() && *value > 0;
}

void WriteClientAppSettingsFile(const std::filesystem::path &version_folder, int cap, bool prompt)
{
	if (cap == 0) cap = 5588562;

	const auto settings_file_path = version_folder / "ClientSettings" / "ClientAppSettings.json";
	printf("DFIntTaskSchedulerTargetFps update requested for %ls to %d\n", settings_file_path.c_str(), cap);

	nlohmann::json object{};

	// read
	auto current_cap = FetchTargetFpsDiskValue(version_folder, &object);
	if ((current_cap.has_value() && *current_cap == cap) || (!current_cap.has_value() && cap < 0))
	{
		return;
	}

	// update
	object["DFIntTaskSchedulerTargetFps"] = cap;

	// try write
	{
		std::error_code ec{};
		std::filesystem::create_directory(settings_file_path.parent_path(), ec);

		std::ofstream file(settings_file_path);
		if (!file.is_open())
		{
			NotifyError("rbxfpsunlocker Error", "Failed to write ClientAppSettings.json! If running the Windows Store version of Roblox, try running Roblox FPS Unlocker as administrator or using a different unlock method.");
			return;
		}
		file << object.dump(4);
		printf("ClientAppSettings.json updated successfully\n");
	}

	// prompt
	if (prompt)
	{
		char message[512]{};
		sprintf_s(message, "Set DFIntTaskSchedulerTargetFps to %d in %ls\n\nRestarting Roblox may be required for changes to take effect.", cap, settings_file_path.c_str());
		MessageBoxA(UI::Window, message, "rbxfpsunlocker", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
	}
}

bool CheckExecutableFile64Bit(const std::filesystem::path &path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) return false;

	IMAGE_DOS_HEADER dos_header{};
	file.read((char *)&dos_header, sizeof(dos_header));
	if (!file || dos_header.e_magic != 0x5A4D) return false;

	IMAGE_NT_HEADERS32 headers{};
	file.seekg(dos_header.e_lfanew);
	file.read((char *)&headers, sizeof(headers));
	if (!file || headers.Signature != 0x4550) return false;

	return headers.OptionalHeader.Magic == 0x020B;
}

class RobloxProcess
{

	RobloxProcessHandle process{};
	std::filesystem::path version_folder{};

	ProcUtil::ModuleInfo main_module{};
	std::vector<const void *> ts_ptr_candidates; // task scheduler pointer candidates
	const void *fd_ptr = nullptr; // frame delay pointer
	bool use_flags_file = false;
	int retries_left = 0;

	bool BlockingLoadModuleInfo()
	{
		int tries = 6;
		int wait_time = 100;

		printf("[%u] Finding process base...\n", process.id);

		while (true)
		{
			main_module = ProcUtil::GetMainModuleInfo(process.Handle());
			if (main_module.base != nullptr)
				return true;

			if (tries--)
			{
				printf("[%u] Retrying in %dms...\n", process.id, wait_time);
				Sleep(wait_time);
				wait_time *= 2;
			} else
			{
				return false;
			}
		}
	}

	bool IsLikelyAntiCheatProtected() const
	{
		if (process.IsOpen())
		{
			return process.type != RobloxHandleType::Studio && ProcUtil::IsProcess64Bit(process.Handle());
		}
		else
		{
			return process.type != RobloxHandleType::Studio && CheckExecutableFile64Bit(version_folder / "RobloxPlayerBeta.exe");
		}
	}

	void WriteFlagsFile(int cap)
	{
		WriteClientAppSettingsFile(version_folder, cap, true);
	}

	void SetFPSCapInMemory(double cap)
	{
		if (fd_ptr)
		{
			try
			{
				static const double min_frame_delay = 1.0 / 10000.0;
				double frame_delay = cap <= 0.0 ? min_frame_delay : 1.0 / cap;

				process.Write(fd_ptr, frame_delay);
			} catch (ProcUtil::WindowsException &e)
			{
				printf("[%u] RobloxProcess::SetFPSCapInMemory failed: %s (%d)\n", process.id, e.what(), e.GetLastError());
			}
		}
	}

	bool FindTaskScheduler()
	{
		try
		{
			const auto handle = process.Handle();
			const auto start = (const uint8_t *)main_module.base;
			const auto end = start + main_module.size;

			if (ProcUtil::IsProcess64Bit(handle))
			{
				// 40 53 48 83 EC 20 0F B6 D9 E8 ?? ?? ?? ?? 86 58 04 48 83 C4 20 5B C3
				if (auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x40\x53\x48\x83\xEC\x20\x0F\xB6\xD9\xE8\x00\x00\x00\x00\x86\x58\x04\x48\x83\xC4\x20\x5B\xC3", "xxxxxxxxxx????xxxxxxxxx", start, end))
				{
					auto gts_fn = result + 14 + ProcUtil::Read<int32_t>(handle, result + 10);

					printf("[%u] GetTaskScheduler (sig studio): %p\n", process.id, gts_fn);

					uint8_t buffer[0x100];
					if (ProcUtil::Read(handle, gts_fn, buffer, sizeof(buffer)))
					{
						if (auto inst = sigscan::scan("\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4\x28", "xxx????xxxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
						{
							const uint8_t *remote = gts_fn + (inst - buffer);
							ts_ptr_candidates = { remote + 7 + *(int32_t *)(inst + 3) };
							return true;
						}
					}
				}
				else
				{
					// Assume Byfron
					// 
					// Thought process: Fancy new anti-cheat technology makes inspecting .text a bit more troublesome than before
					// As a result, I've opted to sig GetTaskScheduler directly instead of looking for one its callers.
					// A longer, uglier signature could be used to produce a single result here,
					// but for the sake of (hopefully) increased reliability, we'll use a simple signature that returns about 8 candidates in a loaded game.

					std::unordered_set<const void *> candidates{};
					auto i = start;
					auto stop = (std::min)(end, start + 40 * 1024 * 1024); // optim: keep search roughly within .text
					const size_t candidate_threshold = 5;

					while (i < stop)
					{
						// 48 8B 05 ?? ?? ?? ?? 48 83 C4 48 C3
						auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4\x48\xC3", "xxx????xxxxx", i, stop); // mov rax, <Rel32>; add rsp, 48h; retn
						if (!result) break;
						candidates.insert(result + 7 + ProcUtil::Read<int32_t>(handle, result + 3));
						if (candidates.size() >= candidate_threshold) break;
						i = result + 1;
					}

					printf("[%u] GetTaskScheduler (sig byfron): found %zu candidates\n", process.id, candidates.size());

					if (candidates.size() != candidate_threshold)
						return false; // keep looking

					ts_ptr_candidates = std::vector<const void *>(candidates.begin(), candidates.end());
					return true;
				}
			} else
			{
				// 55 8B EC 83 E4 F8 83 EC 08 E8 ?? ?? ?? ?? 8D 0C 24
				if (auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x55\x8B\xEC\x83\xE4\xF8\x83\xEC\x08\xE8\xDE\xAD\xBE\xEF\x8D\x0C\x24", "xxxxxxxxxx????xxx", start, end))
				{
					auto gts_fn = result + 14 + ProcUtil::Read<int32_t>(handle, result + 10);

					printf("[%u] GetTaskScheduler (sig ltcg): %p\n", process.id, gts_fn);

					uint8_t buffer[0x100];
					if (ProcUtil::Read(handle, gts_fn, buffer, sizeof(buffer)))
					{
						if (auto inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
						{
							//printf("[%p] Inst: %p\n", process, gts_fn + (inst - buffer));
							ts_ptr_candidates = { (const void *)(*(uint32_t *)(inst + 1)) };
							return true;
						}
					}
				}
				// 55 8B EC 83 EC 10 56 E8 ?? ?? ?? ?? 8B F0 8D 45 F0
				else if (auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x55\x8B\xEC\x83\xEC\x10\x56\xE8\x00\x00\x00\x00\x8B\xF0\x8D\x45\xF0", "xxxxxxxx????xxxxx", start, end))
				{
					auto gts_fn = result + 12 + ProcUtil::Read<int32_t>(handle, result + 8);

					printf("[%u] GetTaskScheduler (sig non-ltcg): %p\n", process.id, gts_fn);

					uint8_t buffer[0x100];
					if (ProcUtil::Read(handle, gts_fn, buffer, sizeof(buffer)))
					{
						if (auto inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
						{
							//printf("[%p] Inst: %p\n", process, gts_fn + (inst - buffer));
							ts_ptr_candidates = { (const void *)(*(uint32_t *)(inst + 1)) };
							return true;
						}
					}
				}
				// 55 8B EC 83 E4 F8 83 EC 14 56 E8 ?? ?? ?? ?? 8D 4C 24 10
				else if (auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x55\x8B\xEC\x83\xE4\xF8\x83\xEC\x14\x56\xE8\x00\x00\x00\x00\x8D\x4C\x24\x10", "xxxxxxxxxxx????xxxx", start, end))
				{
					auto gts_fn = result + 15 + ProcUtil::Read<int32_t>(handle, result + 11);

					printf("[%u] GetTaskScheduler (sig uwp): %p\n", process.id, gts_fn);

					uint8_t buffer[0x100];
					if (ProcUtil::Read(handle, gts_fn, buffer, sizeof(buffer)))
					{
						if (auto inst = sigscan::scan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
						{
							ts_ptr_candidates = { (const void *)(*(uint32_t *)(inst + 1)) };
							return true;
						}
					}
				}
			}
		}
		catch (ProcUtil::WindowsException &e)
		{
		}

		return false;
	}

	size_t FindTaskSchedulerFrameDelayOffset(const void *scheduler) const
	{
		const size_t search_offset = 0x100; // ProcUtil::IsProcess64Bit(process) ? 0x200 : 0x100;

		uint8_t buffer[0x100];
		if (!ProcUtil::Read(process.Handle(), (const uint8_t *)scheduler + search_offset, buffer, sizeof(buffer)))
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

public:
	const RobloxProcessHandle &GetHandle() const
	{
		return process;
	}

	bool Attach(const RobloxProcessHandle &handle, int retry_count)
	{
		process = handle;
		version_folder = process.FetchPath().parent_path(); // note: CreateToolhelp32Snapshot opens a handle momentarily here
		retries_left = retry_count;

		printf("[%u] Attached to PID %u, version folder %ls\n", process.id, process.id, version_folder.c_str());

		OnUnlockMethodUpdate();
		MemoryWriteTick();

		return !ts_ptr_candidates.empty() && fd_ptr != nullptr;
	}

	void MemoryWriteTick()
	{
		if (use_flags_file)
			return;

		if (retries_left < 0)
			return; // we tried

		if (!process.IsOpen())
		{
			process.Open();
			printf("[%u] Opened handle %p (can_write: %u)\n", process.id, process.Handle(), process.can_write);
		}

		if (!main_module.base)
		{
			if (!BlockingLoadModuleInfo())
			{
				NotifyError("rbxfpsunlocker Error", "Failed to get process base! Restart Roblox FPS Unlocker or, if you are on a 64-bit operating system, make sure you are using the 64-bit version of Roblox FPS Unlocker.");
				retries_left = -1;
				return;
			}

			printf("[%u] Process base: %p (size %zu)\n", process.id, main_module.base, main_module.size);

			// Small windows exist where we can attach to Roblox's security daemon while it isn't being debugged (see GetRobloxProcesses)
			// As a secondary measure, check module size (daemon is about 1MB, client is about 80MB)
			if (main_module.size < 1024 * 1024 * 10)
			{
				printf("[%u] Ignoring security daemon process\n", process.id);
				retries_left = -1;
				return;
			}
		}

		if (ts_ptr_candidates.empty())
		{
			const auto start_time = std::chrono::steady_clock::now();
			FindTaskScheduler();
			
			if (ts_ptr_candidates.empty())
			{
				if (retries_left-- <= 0)
					NotifyError("rbxfpsunlocker Error", "Unable to find TaskScheduler! This is probably due to a Roblox update-- watch the github for any patches or a fix.");
				return;
			}
			else
			{
				const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
				printf("[%u] Found TaskScheduler candidates in %lldms\n", process.id, elapsed);
			}
		}

		if (!ts_ptr_candidates.empty() && !fd_ptr)
		{
			try
			{
				size_t fail_count = 0;

				for (const void *ts_ptr : ts_ptr_candidates)
				{
					if (auto scheduler = (const uint8_t *)(ProcUtil::ReadPointer(process.Handle(), ts_ptr)))
					{
						printf("[%u] Potential task scheduler: %p\n", process.id, scheduler);

						size_t delay_offset = FindTaskSchedulerFrameDelayOffset(scheduler);
						if (delay_offset == -1)
						{
							fail_count++;
							continue; // try next
						}

						// winner
						printf("[%u] Frame delay offset: %zu (0x%zx)\n", process.id, delay_offset, delay_offset);
						fd_ptr = scheduler + delay_offset;

						// first write
						SetFPSCap(Settings::FPSCap);
						return;
					}
					else
					{
						printf("[%u] *ts_ptr (%p) == nullptr\n", process.id, ts_ptr);
					}
				}

				if (fail_count > 0)
				{
					// one or more candidates had valid pointers with no frame delay variable
					if (retries_left-- <= 0)
						NotifyError("rbxfpsunlocker Error", "Variable scan failed! Make sure your framerate is at ~60.0 FPS (press Shift+F5 in-game) before using Roblox FPS Unlocker.");
				}
			}
			catch (ProcUtil::WindowsException& e)
			{
				printf("[%u] RobloxProcess::Tick failed: %s (%d)\n", process.id, e.what(), e.GetLastError());
				if (retries_left-- <= 0)
					NotifyError("rbxfpsunlocker Error", "An exception occurred while performing the variable scan.");
			}
		}
	}

	void SetFPSCap(double cap)
	{
		if (use_flags_file)
		{
			WriteFlagsFile(cap);
		}
		else
		{
			SetFPSCapInMemory(cap);
		}
	}

	void OnUIClose()
	{
		SetFPSCapInMemory(60.0);
	}

	void OnUnlockMethodUpdate()
	{
		if (Settings::UnlockMethod == Settings::UnlockMethodType::FlagsFile
			|| (Settings::UnlockMethod == Settings::UnlockMethodType::Hybrid && IsLikelyAntiCheatProtected()))
		{
			printf("[%u] Using FlagsFile mode\n", process.id);
			use_flags_file = true;
			WriteFlagsFile(Settings::FPSCap);
		}
		else
		{
			printf("[%u] Using MemoryWrite mode\n", process.id);
			if (use_flags_file || IsTargetFpsFlagActive(version_folder)) WriteFlagsFile(-1);
			use_flags_file = false;
		}
	}
};

std::filesystem::path GetLocalAppDataPath()
{
	wchar_t *path = nullptr;
	if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path) == S_OK)
	{
		assert(path);
		return path;
	}
	return {};
}

std::filesystem::path GetCurrentClientVersionPath()
{
	HKEY key;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\ROBLOX Corporation\\Environments\\roblox-player", NULL, KEY_READ, &key) == ERROR_SUCCESS)
	{
		char version[64]{};
		DWORD length = sizeof(version) - 1;
		if (RegQueryValueEx(key, "version", NULL, NULL, (LPBYTE)version, &length) == ERROR_SUCCESS)
		{
			return GetLocalAppDataPath() / "Roblox" / "Versions" / version;
		}
	}
	return {};
}

std::filesystem::path GetCurrentStudioVersionPath()
{
	HKEY key;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\ROBLOX Corporation\\Environments\\roblox-studio", NULL, KEY_READ, &key) == ERROR_SUCCESS)
	{
		char version[64]{};
		DWORD length = sizeof(version) - 1;
		if (RegQueryValueEx(key, "version", NULL, NULL, (LPBYTE)version, &length) == ERROR_SUCCESS)
		{
			return GetLocalAppDataPath() / "Roblox" / "Versions" / version;
		}
	}
	return {};
}

void UpdateClientAppSettingsViaRegistry(int cap, bool unlock_method_update = false)
{
	if (Settings::UnlockClient)
	{
		auto path = GetCurrentClientVersionPath();
		if (!path.empty())
		{
			if (Settings::UnlockMethod == Settings::UnlockMethodType::FlagsFile
				|| (Settings::UnlockMethod == Settings::UnlockMethodType::Hybrid && CheckExecutableFile64Bit(path / "RobloxPlayerBeta.exe")))
			{
				WriteClientAppSettingsFile(path, cap, false);
			}
			else
			{
				// MemoryWrite or Hybrid w/ 32-bit client, clear flag
				WriteClientAppSettingsFile(path, -1, false);
			}
		}
	}
	if (Settings::UnlockStudio)
	{
		auto path = GetCurrentStudioVersionPath();
		if (!path.empty())
		{
			if (Settings::UnlockMethod == Settings::UnlockMethodType::FlagsFile)
			{
				WriteClientAppSettingsFile(path, cap, false);
			}
			else
			{
				// MemoryWrite or Hybrid, clear flag
				WriteClientAppSettingsFile(path, -1, false);
			}
		}
	}
}

using attached_processes_map_t = std::unordered_map<DWORD, RobloxProcess>;

std::tuple<std::unique_lock<std::mutex>, attached_processes_map_t *> GetAttachedProcesses()
{
	static std::mutex mutex;
	static attached_processes_map_t map;

	std::unique_lock lock(mutex);
	return { std::move(lock), &map };
}

void RFU_SetFPSCap(double value)
{
	auto [lock, attached_processes] = GetAttachedProcesses();

	// update attached processes
	for (auto& it : *attached_processes)
	{
		it.second.SetFPSCap(value);
	}

	// update flags via registry
	UpdateClientAppSettingsViaRegistry(value);
}

void RFU_OnUIUnlockMethodChange()
{
	auto [lock, attached_processes] = GetAttachedProcesses();

	// update attached processes
	for (auto &it : *attached_processes)
	{
		it.second.OnUnlockMethodUpdate();
	}

	// update flags via registry
	UpdateClientAppSettingsViaRegistry(Settings::FPSCap);
}

void RFU_OnUIClose()
{
	auto [lock, attached_processes] = GetAttachedProcesses();
	for (auto &it : *attached_processes)
	{
		it.second.OnUIClose();
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

	UpdateClientAppSettingsViaRegistry(Settings::FPSCap);

	while (1)
	{
		{
			auto [lock, attached_processes] = GetAttachedProcesses();
			auto processes = GetRobloxProcesses(false, Settings::UnlockClient, Settings::UnlockStudio);

			for (auto &process : processes)
			{
				auto id = process.id;
				if (attached_processes->find(id) == attached_processes->end())
				{
					assert(!process.IsOpen());

					RobloxProcess roblox_process;
					roblox_process.Attach(process, 5);
					(*attached_processes)[id] = std::move(roblox_process);

					printf("New size: %zu\n", attached_processes->size());
				}
			}

			for (auto it = attached_processes->begin(); it != attached_processes->end();)
			{
				if (std::find_if(processes.begin(), processes.end(), [&it](const RobloxProcessHandle &x) { return x.id == it->first; }) == processes.end())
				{
					// it's gone
					auto &handle = it->second.GetHandle();
					if (handle.IsOpen())
					{
						DWORD code = 0;
						GetExitCodeProcess(handle.Handle(), &code);
						printf("Purging dead process %p (pid %d, code %X)\n", handle.Handle(), handle.id, code);
					}
					else
					{
						printf("Purging dead process (pid %d)\n", handle.id);
					}

					it = attached_processes->erase(it);
					printf("New size: %zu\n", attached_processes->size());
				}
				else
				{
					it->second.MemoryWriteTick();
					it++;
				}
			}

			UI::AttachedProcessesCount = attached_processes->size();
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

		RobloxProcessHandle process;
		RobloxProcess attacher{};

		do
		{
			Sleep(100);
			process = GetRobloxProcess();
		}
		while (!process.IsValid());

		printf("Found Roblox...\n");
		printf("Attaching...\n");

		if (!attacher.Attach(process, 0))
		{
			printf("\nERROR: unable to attach to process\n");
			pause();
			return 0;
		}

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
#ifdef NDEBUG
				UI::ToggleConsole();
#endif
			}

			return UI::Start(hInstance, WatchThread);
		}
	}
} 
