#include "procutil.h"

#include <TlHelp32.h>
#include <filesystem>

#include "sigscan.h"

#define READ_LIMIT (1024 * 1024 * 2) // 2 MB

std::vector<DWORD> ProcUtil::GetProcessIdsByImageName(const char *image_name, size_t limit)
{
	std::vector<DWORD> result;

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
				result.push_back(entry.th32ProcessID);
				count++;
			}
		}
	}

	CloseHandle(snapshot);
	return result;
}

std::vector<HANDLE> ProcUtil::GetProcessesByImageName(const char *image_name, DWORD access, size_t limit)
{
	std::vector<HANDLE> result;

	for (DWORD pid : GetProcessIdsByImageName(image_name, limit))
	{
		if (HANDLE process = OpenProcess(access, FALSE, pid))
		{
			result.push_back(process);
		}
	}

	return result;
}

HANDLE ProcUtil::GetProcessByImageName(const char* image_name)
{
	auto processes = GetProcessesByImageName(image_name, 1);
	return processes.size() > 0 ? processes[0] : NULL;
}

std::vector<ProcUtil::ModuleInfo> ProcUtil::GetProcessModules(DWORD process_id, size_t limit)
{
	if (!process_id) return {};

	std::vector<ProcUtil::ModuleInfo> result;

	MODULEENTRY32W entry;
	entry.dwSize = sizeof(MODULEENTRY32W);

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
	if (snapshot == INVALID_HANDLE_VALUE)
		throw WindowsException("unable to enum modules");

	if (Module32FirstW(snapshot, &entry) == TRUE)
	{
		do
		{
			ProcUtil::ModuleInfo info{};
			info.path = entry.szExePath;
			info.base = entry.modBaseAddr;
			info.size = entry.modBaseSize;
			result.push_back(std::move(info));
		} while (result.size() < limit && Module32NextW(snapshot, &entry) == TRUE);
	}

	CloseHandle(snapshot);
	return result;
}


ProcUtil::ModuleInfo ProcUtil::GetMainModuleInfo(HANDLE process)
{
	char buffer[MAX_PATH];
	DWORD size = sizeof(buffer);

	if (!QueryFullProcessImageName(process, 0, buffer, &size)) // Requires at least PROCESS_QUERY_LIMITED_INFORMATION 
		return {};

	ModuleInfo result{};
	bool found;

	printf("[ProcUtil] QueryFullProcessImageName(%p) returned %s\n", process, buffer);

	try
	{
		found = FindModuleInfo(process, buffer, result);
	}
	catch (WindowsException& e)
	{
		printf("[ProcUtil] GetModuleInfo(%p, NULL) failed: %s (%X)\n", process, e.what(), e.GetLastError());
		found = false;
	}

	if (!found) // Couldn't enum modules or GetModuleFileNameEx/GetModuleInformation failed
	{
		result.path = buffer;
		result.base = nullptr;
		result.size = 0;
	}
	
	return result;
}

bool ProcUtil::FindModuleInfo(HANDLE process, const std::filesystem::path& path, ModuleInfo& out)
{
	printf("[ProcUtil] FindModuleInfo(%p, %s)\n", process, path.string().c_str());

	for (const auto &info : GetProcessModules(GetProcessId(process)))
	{
		try
		{
			printf("\tbase=%p, size=%zu, path=%s\n", info.base, info.size, info.path.string().c_str());

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
		else
		{
			return nullptr;
		}
	   
		if (bytes_read > aob_len) bytes_read -= aob_len;

		size -= bytes_read;
		base += bytes_read;
	}

	return nullptr;
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
