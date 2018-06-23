#include <Windows.h>

#include <string>
#include <iostream>
#include <vector>

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include "sigscan.h"

HMODULE MainModule = NULL;
HANDLE SingletonMutex = NULL;

void WINAPI DllInit();
void WINAPI DllExit();

bool WriteMemory(void* address, const void* patch, size_t sz)
{
	DWORD protect;

	if (!VirtualProtect(address, sz, PAGE_EXECUTE_READWRITE, &protect)) return false;
	memcpy((void*)address, patch, sz);
	if (!VirtualProtect(address, sz, protect, (PDWORD)&protect)) return false;

	return true;
}

void* HookVFT(void* object, int index, void* targetf)
{
	int* vftable = *(int**)(object);
	void* previous = (void*)vftable[index];

	WriteMemory(vftable + index, &targetf, sizeof(void*));

	return previous;
}

enum
{
	Roblox,
	RobloxStudio,
	Win10,
	Invalid
} Platform;

void DetectPlatform()
{
	/* Get ProcessName */
	char FileName[MAX_PATH];
	GetModuleFileNameA(NULL, FileName, sizeof(FileName));

	if (strstr(FileName, "RobloxPlayerBeta.exe"))
		Platform = Roblox;
	else if (strstr(FileName, "RobloxStudioBeta.exe"))
		Platform = RobloxStudio;
	else if (strstr(FileName, "Win10Universal.exe"))
		Platform = Win10;
	else
	{
		Platform = Invalid;
		MessageBoxA(NULL, "Unknown platform", "Error", MB_OK);
		DllExit();
	}
}

uintptr_t FindDebugGraphicsVsync()
{
	uintptr_t flag;

	try
	{
		flag = (uintptr_t)sigscan::scan(NULL, "\x80\x3D\x00\x00\x00\x00\x00\x75\x04\xB0\x01\xEB", "xx????xxxxxx"); // 80 3D ?? ?? ?? ?? 00 75 04 B0 01 EB
	}
	catch (...)
	{
		flag = 0;
	}

	if (!flag)
	{
		MessageBoxA(NULL, "Scan failed! This is probably due to a Roblox update-- watch the github for any patches or a fix.", "rbxfpsunlocker Error", MB_OK);
		DllExit();
	}

	return *(uint32_t*)(flag + 2);
}

typedef HRESULT(_stdcall *IDXGISwapChainPresentFn)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
IDXGISwapChainPresentFn IDXGISwapChainPresent;

HRESULT __stdcall IDXGISwapChainPresentHook(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	/*
	* https://msdn.microsoft.com/en-us/library/windows/desktop/bb174576(v=vs.85).aspx
	* https://i.imgur.com/BH2lY12.png
	*
	* Enabling DebugGraphicsVsync disables throttling in the engine but enables VSync via the SyncInterval parameter of Present calls
	* Solution: Hook Present and set SyncInterval to 0
	*/
	return IDXGISwapChainPresent(pSwapChain, 0, Flags);
}

void CheckRunning()
{
	char Name[64];
	sprintf_s(Name, sizeof(Name), "RbxFpsUnlockerMutex_%d", GetCurrentProcessId());

	SingletonMutex = CreateMutexA(NULL, FALSE, Name);

	if (!SingletonMutex)
	{
		MessageBoxA(NULL, "Unable to create mutex", "Error", MB_OK);
		DllExit();
	}

	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		MessageBoxA(NULL, "rbxfpsunlocker is already running", "Error", MB_OK);
		DllExit();
	}
}

void WINAPI DllInit()
{
	CheckRunning();

	if (GetModuleHandleA("dxgi.dll"))
	{
		/* Detect platform */
		DetectPlatform();

		/* Scan for DebugGraphicsVsync flag */
		uint32_t Flag = FindDebugGraphicsVsync();

		/* Create dummy ID3D11Device to grab its vftable */
		ID3D11Device* Device = 0;
		ID3D11DeviceContext* DeviceContext = 0;
		IDXGISwapChain* SwapChain = 0;
		D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_11_0;
		DXGI_SWAP_CHAIN_DESC SwapChainDesc;

		/* Create dummy window */
		HWND DummyWindow = CreateWindowA("STATIC", "Dummy", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
		if (!DummyWindow)
		{
			MessageBoxA(NULL, "Unable to create dummy window", "Error", MB_OK);
			DllExit();
		}

		ZeroMemory(&SwapChainDesc, sizeof(SwapChainDesc));
		SwapChainDesc.BufferCount = 1;
		SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		SwapChainDesc.OutputWindow = DummyWindow;
		SwapChainDesc.SampleDesc.Count = 1;
		SwapChainDesc.Windowed = TRUE;
		SwapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		SwapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_NULL, NULL, NULL, &FeatureLevel, 1, D3D11_SDK_VERSION, &SwapChainDesc, &SwapChain, &Device, NULL, &DeviceContext)))
		{
			/* Hook IDXGISwapChain::Present & enable flag */
			IDXGISwapChainPresent = (IDXGISwapChainPresentFn)HookVFT(SwapChain, 8, IDXGISwapChainPresentHook);
			*(unsigned char*)Flag = 1;

			/* Free objects */
			Device->Release();
			DeviceContext->Release();
			SwapChain->Release();
			CloseHandle(DummyWindow);
		}
		else
		{
			MessageBoxA(NULL, "Unable to create D3D11 device", "Error", MB_OK);
			DllExit();
		}
	}
}

void WINAPI DllExit()
{
	if (SingletonMutex) CloseHandle(SingletonMutex);
	FreeLibraryAndExitThread(MainModule, 0); // This probably doesn't free our module if we manually map /shrug
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		MainModule = hinstDLL;
		DisableThreadLibraryCalls(hinstDLL);
		CreateThread(0, 0, (LPTHREAD_START_ROUTINE)DllInit, 0, 0, 0);
	}

	return TRUE;
}


