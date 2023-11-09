#include <Windows.h>
#include <iostream>
#include <sstream>
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
#include "fflags.hpp"

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

	bool ShouldAttach() const
	{
		switch (type)
		{
		case RobloxHandleType::Client:
		case RobloxHandleType::UWP:
			return Settings::UnlockClient;
		case RobloxHandleType::Studio:
			return Settings::UnlockStudio;
		default:
			return false;
		}
	}

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

std::vector<RobloxProcessHandle> GetRobloxProcesses(bool open_all, bool include_client, bool include_studio)
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
	auto processes = GetRobloxProcesses(true, true, true);

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

bool RobloxFFlags::apply(bool prompt)
{
	if (target_fps_mod || alt_enter_mod)
	{
		if (!write_disk())
		{
			NotifyError("rbxfpsunlocker Error", "Failed to write ClientAppSettings.json! If running the Windows Store version of Roblox, try running Roblox FPS Unlocker as administrator or using a different unlock method.");
			return false;
		}

		auto target_fps_value = target_fps();
		auto alt_enter_value = alt_enter();

		printf("Wrote flags to %ls (target_fps=", settings_file_path.c_str());
		if (target_fps_value) printf("%llu", *target_fps_value); else printf("null");
		printf(", alt_enter=");
		if (alt_enter_value) printf("%u", *alt_enter_value); else printf("null");
		printf(")\n");


		if (prompt)
		{
			std::stringstream stream{};
			if (target_fps_mod)
			{
				stream << "Set DFIntTaskSchedulerTargetFps to ";
				auto value = target_fps();
				if (value) stream << *value; else stream << "<null>";
				stream << "\n";
			}
			if (alt_enter_mod)
			{
				stream << "Set FFlagHandleAltEnterFullscreenManually to ";
				auto value = alt_enter();
				if (value) stream << *value; else stream << "<null>";
			}
			stream << "\n\nin " << settings_file_path << "\n\nRestarting Roblox may be required for changes to take effect.";

			MessageBoxA(UI::Window, stream.str().c_str(), "rbxfpsunlocker", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
		}

		target_fps_mod = false;
		alt_enter_mod = false;

		return true;
	}

	return false;
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

class RobloxInstance
{
	std::filesystem::path version_folder{};
	bool is_client = false;

	RobloxProcessHandle process{};

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
			{
				MEMORY_BASIC_INFORMATION mbi{};
				if (VirtualQueryEx(process.Handle(), main_module.base, &mbi, sizeof(mbi)) && mbi.Type == MEM_PRIVATE)
				{
					// We need the Hyperion-mapped Roblox base, not this
					// This VirtualQueryEx check will fail eventually due to Hyperion stripping our handle
					printf("[%u] WARNING: GetMainModuleInfo returned invalid base address\n", process.id);
				}
				else
				{
					return true;
				}
			}

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
			return is_client && ProcUtil::IsProcess64Bit(process.Handle());
		}
		else
		{
			return is_client && CheckExecutableFile64Bit(version_folder / "RobloxPlayerBeta.exe");
		}
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
						ts_ptr_candidates.clear();
						auto inst = buffer;
						while (inst = sigscan::scan("\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4", "xxx????xxx", (uintptr_t)inst, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
						{
							const uint8_t *remote = gts_fn + (inst - buffer);
							ts_ptr_candidates.push_back(remote + 7 + *(int32_t *)(inst + 3));
							inst++;
						}
						return !ts_ptr_candidates.empty();
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
						// 48 8B 05 ?? ?? ?? ?? 48 83 C4 ?? 5B C3
						auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4\x00\x5B\xC3", "xxx????xxx?xx", i, stop); // mov rax, <Rel32>; add rsp, 40h; pop rbx; retn
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
				// 55 8B EC 53 56 57 8B F9 E8 ?? ?? ?? ?? 8B D8
				else if (auto result = (const uint8_t *)ProcUtil::ScanProcess(handle, "\x55\x8B\xEC\x53\x56\x57\x8B\xF9\xE8\x00\x00\x00\x00\x8B\xD8", "xxxxxxxxx????xx", start, end))
				{
					auto gts_fn = result + 13 + ProcUtil::Read<int32_t>(handle, result + 9);

					printf("[%u] GetTaskScheduler (sig uwp): %p\n", process.id, gts_fn);

					uint8_t buffer[0x100];
					if (ProcUtil::Read(handle, gts_fn, buffer, sizeof(buffer)))
					{
						if (auto inst = sigscan::scan("\x50\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "xx????xxx", (uintptr_t)buffer, (uintptr_t)buffer + 0x100)) // mov eax, <TaskSchedulerPtr>; mov ecx, [ebp-0Ch])
						{
							ts_ptr_candidates = { (const void *)(*(uint32_t *)(inst + 2)) };
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
	RobloxInstance() {}
	RobloxInstance(std::filesystem::path version_folder, bool is_client) : version_folder(std::move(version_folder)), is_client(is_client) {}

	const RobloxProcessHandle &GetHandle() const
	{
		return process;
	}

	bool IsClient() const
	{
		return is_client;
	}

	bool IsStudio() const
	{
		return !is_client;
	}

	bool IsRegistryInstance() const
	{
		return !process.IsValid();
	}

	bool AttachProcess(const RobloxProcessHandle &handle, int retry_count)
	{
		process = handle;
		version_folder = process.FetchPath().parent_path(); // note: CreateToolhelp32Snapshot opens a handle momentarily here
		is_client = handle.type != RobloxHandleType::Studio;
		retries_left = retry_count;

		printf("[%u] Attached to PID %u, version folder %ls\n", process.id, process.id, version_folder.c_str());

		// Special case for Windows Store / "UWP" app.
		// The process will point to a read-only location while settings are stored elsewhere.
		if (version_folder.string().find("WindowsApps") != std::string::npos)
		{
			auto local_app_data_path = static_cast<std::string>(getenv("LOCALAPPDATA"));
			auto uwp_versioned_name = version_folder.filename().string();
			auto uwp_settings_name = uwp_versioned_name.substr(0, uwp_versioned_name.find('_')) + uwp_versioned_name.substr(uwp_versioned_name.rfind('_'));
			version_folder = std::filesystem::path(local_app_data_path + "\\Packages\\" + uwp_settings_name + "\\LocalState");
			printf("[%u] Windows Store path was found. Using %ls\n", process.id, version_folder.c_str());
		}

		OnEvent(RFU::Event::SETTINGS_MASK);
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
						SetFPSCapInMemory(Settings::FPSCap);
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

	void OnEvent(uint32_t ev_flags)
	{	
		// todo: redesign the event/flag writing/blah code (again) (maybe)
		// dealing with initiation, one or multiple setting changes, unlocks, and exits on both attached processes and registry instances whilst 
		// making sure it prompts the user correctly is a fun task
		// 1. any user input that triggers a flag write (loading settings from disk, setting change in UI, app init) should notify the user if the version being written to has a process running AND the flag values have changed
		// 2. ideally, a flag file is not read or written to more than once per version per event
		// 3. flags should only be reverted if "Unlock Client/Studio" is toggled off or the user exits the app
		// 4. note UnlockMethod setting, which can change whether a process needs to be attached, detached, or reattached as well as trigger flag changes
		
		// a separate todo:
		// there are obvious problems with attaching to Hyperion processes
		// right now RFU has an okay success rate but it has a lot to do with attach timing and changes with OS (win 10 vs win 11)
		// reattaches via settings changes or relaunches of RFU will lead to crashes as well
		// consider removing the ability to attach to Hyperion clients, or hide it from the UI
		// or... double down and implement more special code to circumvent Hyperion (fun, but not a good idea)
		// or... convince Roblox to add fps cap and vsync settings to the graphics menu :-D

		if (ev_flags & RFU::Event::CLOSE_MASK)
		{
			printf("[%u] Closing instance (IsRegistryInstance=%u, RevertFlagsOnClose=%u)\n", process.id, IsRegistryInstance(), Settings::RevertFlagsOnClose);

			if (ev_flags != RFU::Event::PROCESS_DIED)
			{
				// revert flags if process isn't dead
				if (Settings::RevertFlagsOnClose)
				{
					RobloxFFlags(version_folder).set_target_fps(std::nullopt).set_alt_enter_flag(std::nullopt).apply(ev_flags != RFU::Event::APP_EXIT && !IsRegistryInstance());
				}
				SetFPSCapInMemory(60.0);
			}
		}
		else if (ev_flags & RFU::Event::SETTINGS_MASK)
		{
			RobloxFFlags fflags(version_folder);

			if (ev_flags & RFU::Event::UNLOCK_METHOD)
			{
				if (Settings::UnlockMethod == Settings::UnlockMethodType::FlagsFile
					|| (Settings::UnlockMethod == Settings::UnlockMethodType::Hybrid && IsLikelyAntiCheatProtected()))
				{
					printf("[%u] Using FlagsFile mode\n", process.id);
					use_flags_file = true;
					fflags.set_target_fps(Settings::FPSCap);
				}
				else
				{
					printf("[%u] Using MemoryWrite mode\n", process.id);
					use_flags_file = false;
					fflags.set_target_fps(std::nullopt);
				}
			}

			if (ev_flags & RFU::Event::FPS_CAP)
			{
				if (use_flags_file) fflags.set_target_fps(Settings::FPSCap);
				else SetFPSCapInMemory(Settings::FPSCap);
			}

			if (ev_flags & RFU::Event::ALT_ENTER)
			{
				fflags.set_alt_enter_flag(Settings::AltEnterFix ? alt_enter_t{false} : std::nullopt);
			}

			// write flags to disk. only prompt for live instances
			fflags.apply(!IsRegistryInstance());
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

using attached_instances_map_t = std::unordered_map<DWORD, RobloxInstance>;

struct RFUContext
{
	attached_instances_map_t attached_instances{};
	bool unlocking_client = false;
	bool unlocking_studio = false;
};

std::tuple<std::unique_lock<std::mutex>, RFUContext *> AcquireRFUContext()
{
	static std::mutex mutex;
	static RFUContext context;

	std::unique_lock lock(mutex);
	return { std::move(lock), &context };
}

void RFU::OnEvent(uint32_t ev)
{
	auto [lock, context] = AcquireRFUContext();

	// update live processes
	for (auto &it : context->attached_instances)
	{
		it.second.OnEvent(ev);
	}

	// update registry
	if (context->unlocking_client)
	{
		auto client_path = GetCurrentClientVersionPath();
		if (!client_path.empty())
		{
			RobloxInstance(client_path, true).OnEvent(ev);
		}
	}

	if (context->unlocking_studio)
	{
		auto studio_path = GetCurrentStudioVersionPath();
		if (!studio_path.empty())
		{
			RobloxInstance(studio_path, false).OnEvent(ev);
		}
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
		{
			auto [lock, context] = AcquireRFUContext();
			auto processes = GetRobloxProcesses(false, true, true);

			for (auto &process : processes)
			{
				auto id = process.id;
				if (context->attached_instances.find(id) == context->attached_instances.end())
				{
					// we found a process that isn't in our attached list
					if (process.ShouldAttach())
					{
						assert(!process.IsOpen());

						RobloxInstance roblox_process;
						roblox_process.AttachProcess(process, 5);
						context->attached_instances[id] = std::move(roblox_process);

						printf("New size: %zu\n", context->attached_instances.size());
					}
				}
			}

			for (auto it = context->attached_instances.begin(); it != context->attached_instances.end();)
			{
				auto &handle = it->second.GetHandle();

				if (std::find_if(processes.begin(), processes.end(), [&it](const RobloxProcessHandle &x) { return x.id == it->first; }) == processes.end())
				{
					// it's gone
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

					// close 
					it->second.OnEvent(RFU::Event::PROCESS_DIED);

					it = context->attached_instances.erase(it);
					printf("New size: %zu\n", context->attached_instances.size());
				}
				else if (!handle.ShouldAttach())
				{
					// settings changed
					printf("Closing process (pid %d)\n", handle.id);
					it->second.OnEvent(RFU::Event::CLOSE);
					it = context->attached_instances.erase(it);
					printf("New size: %zu\n", context->attached_instances.size());
				}
				else
				{
					it->second.MemoryWriteTick();
					it++;
				}
			}

			if (context->unlocking_client != Settings::UnlockClient)
			{
				auto path = GetCurrentClientVersionPath();
				if (!path.empty())
				{
					RobloxInstance(path, true).OnEvent(Settings::UnlockClient ? RFU::Event::SETTINGS_MASK : RFU::Event::CLOSE);
				}

				context->unlocking_client = Settings::UnlockClient;
			}

			if (context->unlocking_studio != Settings::UnlockStudio)
			{
				auto path = GetCurrentStudioVersionPath();
				if (!path.empty())
				{
					RobloxInstance(path, false).OnEvent(Settings::UnlockStudio ? RFU::Event::SETTINGS_MASK : RFU::Event::CLOSE);
				}

				context->unlocking_studio = Settings::UnlockStudio;
			}

			UI::AttachedProcessesCount = context->attached_instances.size();
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
		RobloxInstance attacher{};

		do
		{
			Sleep(100);
			process = GetRobloxProcess();
		}
		while (!process.IsValid());

		printf("Found Roblox...\n");
		printf("Attaching...\n");

		if (!attacher.AttachProcess(process, 0))
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
				if (RFU::CheckForUpdates()) return 0;
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
