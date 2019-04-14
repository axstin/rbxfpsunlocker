#include "procutil.h"

#include <TlHelp32.h>
#include <filesystem>

#include "../rbxfpsunlocker/sigscan.h"

#define READ_LIMIT (1024 * 1024 * 2) // 2 MB

std::vector<HANDLE> ProcUtil::GetProcessesByImageName(const char *image_name, size_t limit, DWORD access)
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
			if (_stricmp(entry.szExeFile, image_name) == 0)
			{
				if (HANDLE process = OpenProcess(access, FALSE, entry.th32ProcessID))
				{
					result.push_back(process);
					count++;
				}
			}
		}
	}

	CloseHandle(snapshot);
	return result;
}

HANDLE ProcUtil::GetProcessByImageName(const char* image_name)
{
	auto processes = GetProcessesByImageName(image_name, 1);
	return processes.size() > 0 ? processes[0] : NULL;
}

std::vector<HMODULE> ProcUtil::GetProcessModules(HANDLE process)
{
	std::vector<HMODULE> result;

	DWORD last = 0;
	DWORD needed;

	while (true)
	{
		if (!EnumProcessModulesEx(process, result.data(), last, &needed, LIST_MODULES_ALL))
			throw WindowsException("unable to enum modules");

		result.resize(needed / sizeof(HMODULE));
		if (needed <= last)	return result;
		last = needed;
	}
}

ProcUtil::ModuleInfo ProcUtil::GetModuleInfo(HANDLE process, HMODULE module)
{
	ModuleInfo result;

	if (module == NULL)
	{
		/*
			GetModuleInformation works with hModule set to NULL with the caveat that lpBaseOfDll will be NULL aswell: https://doxygen.reactos.org/de/d86/dll_2win32_2psapi_2psapi_8c_source.html#l01102
			Solutions: 
				1) Enumerate modules in the process and compare file names
				2) Use NtQueryInformationProcess with ProcessBasicInformation to find the base address (as done here: https://doxygen.reactos.org/de/d86/dll_2win32_2psapi_2psapi_8c_source.html#l00142)
		*/

		char buffer[MAX_PATH];
		DWORD size = sizeof(buffer);

		if (!QueryFullProcessImageName(process, 0, buffer, &size)) // Requires at least PROCESS_QUERY_LIMITED_INFORMATION 
			throw WindowsException("unable to query process image name");

		bool found;

		printf("ProcUtil::GetModuleInfo: buffer = %s\n", buffer);

		try
		{
			found = FindModuleInfo(process, buffer, result);
		}
		catch (WindowsException& e)
		{
			printf("[%p] ProcUtil::GetModuleInfo failed: %s (%X)\n", process, e.what(), e.GetLastError());
			found = false;
		}

		if (!found) // Couldn't enum modules or GetModuleFileNameEx/GetModuleInformation failed
		{
			result.path = buffer;
			result.base = nullptr;
			result.size = 0;
			result.entry_point = nullptr;
		}
	}
	else
	{
		char buffer[MAX_PATH];
		if (!GetModuleFileNameEx(process, module, buffer, sizeof(buffer))) // Requires PROCESS_QUERY_INFORMATION | PROCESS_VM_READ 
			throw WindowsException("unable to get module file name");

		MODULEINFO mi;
		if (!GetModuleInformation(process, module, &mi, sizeof(mi))) // Requires PROCESS_QUERY_INFORMATION | PROCESS_VM_READ 
			throw WindowsException("unable to get module information");

		result.path = buffer;
		result.base = mi.lpBaseOfDll;
		result.size = mi.SizeOfImage;
		result.entry_point = mi.EntryPoint;
	}

	return result;
}

bool ProcUtil::FindModuleInfo(HANDLE process, const std::filesystem::path& path, ModuleInfo& out)
{
	printf("ProcUtil::FindModuleInfo: path = %s\n", path.string().c_str());

	for (auto module : GetProcessModules(process))
	{
		try
		{
			ModuleInfo info = GetModuleInfo(process, module);

			printf("ProcUtil::FindModuleInfo: info.path = %s\n", path.string().c_str());

			if (std::filesystem::equivalent(info.path, path))
			{
				out = info;
				return true;
			}
		}
		catch (std::filesystem::filesystem_error& e)
		{
		}
	}

	return false;
}

void *ScanRegion(HANDLE process, const char *aob, const char *mask, const uint8_t *base, size_t size)
{
	std::vector<uint8_t> buffer;
	buffer.resize(READ_LIMIT);

	size_t aob_len = strlen(mask);

	while (size >= aob_len)
	{
		size_t bytes_read = 0;

		if (ReadProcessMemory(process, base, buffer.data(), size < buffer.size() ? size : buffer.size(), (SIZE_T *)&bytes_read) && bytes_read >= aob_len)
		{
			if (uint8_t *result = sigscan::scan(aob, mask, (uintptr_t)buffer.data(), (uintptr_t)buffer.data() + bytes_read))
			{
				return (uint8_t *)base + (result - buffer.data());
			}
		}
	   
		if (bytes_read > aob_len) bytes_read -= aob_len;

		size -= bytes_read;
		base += bytes_read;
	}

	return false;
}


void *ProcUtil::ScanProcess(HANDLE process, const char *aob, const char *mask, const uint8_t *start, const uint8_t *end)
{
	auto i = start;

	while (i < end)
	{
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQueryEx(process, i, &mbi, sizeof(mbi)))
		{
			return nullptr;
		}

		size_t size = mbi.RegionSize - (i - (const uint8_t *)mbi.BaseAddress);
		if (i + size >= end) size = end - i;

		if (mbi.State & MEM_COMMIT && mbi.Protect & PAGE_READABLE && !(mbi.Protect & PAGE_GUARD))
		{
			if (void *result = ScanRegion(process, aob, mask, i, size))
			{
				return result;
			}
		}

		i += size;
	}

	return nullptr;
}

bool ProcUtil::IsOS64Bit()
{
#ifdef _WIN64
	return true;
#else
	SYSTEM_INFO info;
	GetNativeSystemInfo(&info);

	return info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
		info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64; // lol arm
#endif
}

bool ProcUtil::IsProcess64Bit(HANDLE process)
{
	if (IsOS64Bit())
	{
		BOOL result;
		if (!IsWow64Process(process, &result))
			throw WindowsException("unable to check process wow64");

		return result == 0;
	}
	else
	{
		return false;
	}
}
