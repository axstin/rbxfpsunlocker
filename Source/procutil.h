#pragma once

#include <Windows.h>
#include <Psapi.h>

#include <vector>
#include <string>
#include <filesystem>
#include <optional>

#define PAGE_READABLE (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE)

namespace ProcUtil
{
	// Problem: Calling GetLastError() in a catch block is sketchy/unreliable as Windows' internal exception handling _may_ call WinAPI functions beforehand that change the error. Better safe than sorry.
	// Solution: This class
	class WindowsException : public std::runtime_error
	{
	public:
		WindowsException(const char *message)
			: std::runtime_error(init(message))
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

	std::vector<DWORD> GetProcessIdsByImageName(const char *image_name, size_t limit = -1);
	std::vector<HANDLE> GetProcessesByImageName(const char *image_name, DWORD access, size_t limit = -1);
	HANDLE GetProcessByImageName(const char* image_name);

	std::vector<ModuleInfo> GetProcessModules(HANDLE process);
	ModuleInfo GetMainModuleInfo(HANDLE process);
	bool FindModuleInfo(HANDLE process, const std::filesystem::path& name, ModuleInfo& out);
	void *ScanProcess(HANDLE process, const char *aob, const char *mask, const uint8_t *start = nullptr, const uint8_t *end = (const uint8_t *)UINTPTR_MAX);
	
	bool IsOS64Bit();
	bool IsProcess64Bit(HANDLE process);
	
	template <typename T>
	inline bool Read(HANDLE process, const void *location, T *buffer, size_t size = 1) noexcept
	{
		return ReadProcessMemory(process, location, buffer, size * sizeof(T), NULL) != 0;
	}

	template <typename T>
	inline T Read(HANDLE process, const void *location)
	{
		T value;
		if (!ReadProcessMemory(process, location, (LPVOID) &value, sizeof(T), NULL)) throw WindowsException("unable to read process memory");
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
	inline void Write(HANDLE process, const void *location, const T& value)
	{
		if (!WriteProcessMemory(process, (LPVOID) location, (LPCVOID) &value, sizeof(T), NULL)) throw WindowsException("unable to write process memory");
	}

	struct ModuleInfo
	{
		std::filesystem::path path;
		void *base = nullptr;
		size_t size = 0;

		HMODULE GetHandle() const
		{
			return (HMODULE)base;
		}
	};

	struct ProcessInfo
	{
		HANDLE handle = NULL;
		ModuleInfo module;

		DWORD id = 0;
		std::string name;

		HWND window = NULL;
		std::string window_title;

		bool FindMainWindow() // a.k.a. find first window associated with the process that is visible
		{
			window = NULL;

			EnumWindows([](HWND window, LPARAM param) -> BOOL
			{
				auto info = (ProcessInfo *)param;

				DWORD process_id;
				GetWindowThreadProcessId(window, &process_id);

				if (IsWindowVisible(window) && process_id == info->id)
				{
					char title[256] = { 0 };
					GetWindowTextA(window, title, sizeof(title));

					info->window = window;
					info->window_title = title;
					return FALSE;
				}

				return TRUE;
			}, (LPARAM)this);

			return window != NULL;
		}

		ProcessInfo()
		{}

		ProcessInfo(HANDLE handle, bool find_window = false)
			: handle(handle), window(NULL)
		{
			id = GetProcessId(handle);
			module = GetMainModuleInfo(handle);
			name = module.path.filename().string();

			if (find_window)
				FindMainWindow();
		}
	};
}