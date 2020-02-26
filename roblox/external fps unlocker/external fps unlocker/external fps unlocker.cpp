#include <Windows.h>
#include <vector>
#include <TlHelp32.h>
#include <iostream>

DWORD GetPID(char* procName) {
	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);
	DWORD pID = NULL;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (Process32First(snapshot, &entry)) {
		do {
			if (_stricmp(entry.szExeFile, procName) == 0) {
				pID = entry.th32ProcessID;
				break;
			}
		} while (Process32Next(snapshot, &entry));
	}

	CloseHandle(snapshot);

	return pID;

}

DWORD GetModuleBaseAddress(DWORD pID, char* moduleName) {

	MODULEENTRY32 entry;
	entry.dwSize = sizeof(MODULEENTRY32);
	DWORD baseAddress = 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pID);	

	if (Module32First(snapshot, &entry)) {
		do {
			if (_stricmp(entry.szModule, moduleName) == 0) {
				baseAddress = (DWORD)entry.modBaseAddr;
				break;
			}
		} while (Module32Next(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return baseAddress;
}

DWORD GetModuleSize(DWORD pID, char* moduleName) {

	MODULEENTRY32 entry;
	entry.dwSize = sizeof(MODULEENTRY32);
	DWORD moduleSize = 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pID);

	if (Module32First(snapshot, &entry)) {
		do {
			if (_stricmp(entry.szModule, moduleName) == 0) {
				moduleSize = (DWORD)entry.modBaseSize;
				break;
			}
		} while (Module32Next(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return moduleSize;
}

BOOL ComparePattern(HANDLE pHandle, DWORD address, char* pattern, char* mask) {

	DWORD patternSize = strlen(mask);

	auto memBuf = new char[patternSize + 1];
	memset(memBuf, 0, patternSize + 1);
	ReadProcessMemory(pHandle, (LPVOID)address, memBuf, patternSize, 0);


	for (DWORD i = 1; i < patternSize; i++) {

		if (memBuf[i] != pattern[i] && mask[i] != *"?") {
			return false;
		}
	}
	
	return true;
}

DWORD ExternalAoBScan(HANDLE pHandle, DWORD pID, char* mod, char* pattern, char* mask) {

	std::vector<DWORD> matches;
	DWORD patternSize = strlen(mask);

	DWORD moduleBase = GetModuleBaseAddress(pID, mod);
	DWORD moduleSize = GetModuleSize(pID, mod);

	if (!moduleBase || !moduleSize) {
		std::cout << "Could not get " << mod << " base address or size" << std::endl;
		return NULL;
	}

	auto moduleBytes = new char[moduleSize + 1];
	memset(moduleBytes, 0, moduleSize + 1);
	ReadProcessMemory(pHandle, (LPVOID)moduleBase, moduleBytes, moduleSize, 0);
	for (int i = 0; i + patternSize < moduleSize; i++) {
		if (pattern[0] == moduleBytes[i]) {
			if (ComparePattern(pHandle, moduleBase + i, pattern, mask)) {
				matches.push_back(moduleBase + i);
			}
		}
	}


	if (matches.size() == 0) {
		return NULL;
	}
	return matches[0];
}


int main() {
	char process[] = "RobloxPlayerBeta.exe";
	char pattern[] = "\x55\x8B\xEC\xE8\x00\x00\x00\x00\x8A\x4D\x08\x83\xC0\x04\x86\x08\x5D\xC3";
	char mask[] = "xxxx????xxxxxxxxxx";
	DWORD pID = GetPID(process);
	if (!pID) {
		std::cout << "Could not find process" << std::endl;
		std::cin.get();
		return 1;
	}

	HANDLE pHandle = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, pID);
	if (!pHandle) {
		std::cout << "Could not obtain handle" << std::endl;
		std::cin.get();
		return 1;
	}

	DWORD address = ExternalAoBScan(pHandle, pID, process, pattern, mask);
	if (address) {
		std::cout << "Address: " << std::hex << address << std::endl;	
	}
	else {
		std::cout << "No address" << std::endl;
	}

	std::cin.get();
	return 0;

}