#pragma once

#include <Windows.h>
#include <Psapi.h>

#include <vector>
#include <string>
#include <filesystem>
#include <stdexcept>

#define PAGE_READABLE (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE)

namespace ProcUtil
{
	// Problem: Calling GetLastError() in a catch block is sketchy/unreliable as Windows' internal exception handling _may_ call WinAPI functions beforehand that change the error. Better safe than sorry.
	// Solution: This class
	class WindowsException : public std::runtime_error
	{
	public:
		WindowsException(const char *message)
			: std::runtime_error(init(message)), last_error(0)
		{
		}

		DWORD GetLastError() const
		{
			return last_error;
		}

	private:
		const char *init(const char *message)
		{
			last_error = ::GetLastError();
			return message;
		}

		DWORD last_error;
	};

	struct ModuleInfo;
	struct ProcessInfo;

	std::vector<HANDLE> GetProcessesByImageName(LPCWSTR image_name, size_t limit = -1, DWORD access = PROCESS_ALL_ACCESS);
	HANDLE GetProcessByImageName(LPWSTR image_name);

	std::vector<HMODULE> GetProcessModules(HANDLE process);
	ModuleInfo GetModuleInfo(HANDLE process, HMODULE module);
	bool FindModuleInfo(HANDLE process, const std::filesystem::path& name, ModuleInfo& out);
	void *ScanProcess(HANDLE process, const char *aob, const char *mask, const uint8_t *start = nullptr, const uint8_t *end = (const uint8_t *)UINTPTR_MAX);
	
	bool IsOS64Bit();
	bool IsProcess64Bit(HANDLE process);
	
	template <typename T>
	bool Read(HANDLE process, const void *location, T *buffer, size_t size = 1) noexcept
	{
		return ReadProcessMemory(process, location, buffer, size * sizeof(T), nullptr) != 0;
	}

	template <typename T>
	T Read(HANDLE process, const void *location)
	{
		T value;
		if (!ReadProcessMemory(process, location, static_cast<LPVOID>(&value), sizeof(T), nullptr)) throw WindowsException("unable to read process memory");
		return value;
	}

	inline const void *ReadPointer(HANDLE process, const void *location)
	{
#ifdef _WIN64
		return IsProcess64Bit(process) ? (const void *)Read<uint64_t>(process, location) : (const void *)Read<uint32_t>(process, location);
#else
		return Read<const void *>(process, location);
#endif
	}

	template <typename T>
	void Write(HANDLE process, const void *location, const T& value)
	{
		if (!WriteProcessMemory(process, (LPVOID) location, static_cast<LPCVOID>(&value), sizeof(T), nullptr)) throw WindowsException("unable to write process memory");
	}

	template <size_t N, typename T>
	bool ExecuteStub(HANDLE process, const uint8_t (&code)[N], T& arg)
	{
		if (auto alloc = static_cast<const uint8_t*>(VirtualAllocEx(process, nullptr, sizeof(T) + sizeof(code), MEM_COMMIT,
		                                                            PAGE_EXECUTE_READWRITE)))
		{
			Write(process, alloc, arg);
			Write(process, alloc + sizeof(T), code);

			if (const auto thread = CreateRemoteThread(process, nullptr, 0, (LPTHREAD_START_ROUTINE)(alloc + sizeof(T)), (LPVOID) alloc, NULL, nullptr))
			{
				WaitForSingleObject(thread, INFINITE);
				arg = Read<T>(process, alloc);
				VirtualFreeEx(process, (LPVOID) alloc, 0, MEM_RELEASE);
				return true;
			}
		}
		
		return false;
	}

	struct ModuleInfo
	{
		std::filesystem::path path;
		void *base = nullptr;
		size_t size = 0;
		void *entry_point = nullptr;

		HMODULE GetHandle() const
		{
			return static_cast<HMODULE>(base);
		}
	};

	struct ProcessInfo
	{
		HANDLE handle = nullptr;
		ModuleInfo module;

		DWORD id = 0;
		std::string name;

		HWND window = nullptr;
		std::string window_title;

		bool FindMainWindow() // a.k.a. find first window associated with the process that is visible
		{
			window = nullptr;

			EnumWindows([](HWND window, LPARAM param) -> BOOL
			{
				auto info = reinterpret_cast<ProcessInfo*>(param);

				DWORD process_id;
				GetWindowThreadProcessId(window, &process_id);

				if (IsWindowVisible(window) && process_id == info->id)
				{
					char title[256] = { 0 };
					GetWindowTextA(window, title, sizeof title);

					info->window = window;
					info->window_title = title;
					return FALSE;
				}

				return TRUE;
			}, LPARAM(this));

			return window != nullptr;
		}

		ProcessInfo()
		{}

		ProcessInfo(HANDLE handle, bool find_window = false)
			: handle(handle)
		{
			id = GetProcessId(handle);
			printf("[%p] Got ID %d\n", handle, id);
			module = GetModuleInfo(handle, nullptr);
			printf("[%p] Got ModuleInfo\n", handle);
			name = module.path.filename().string();
			printf("[%p] Got module name\n", handle);
			
			if (find_window)
				FindMainWindow();
		}
	};
}