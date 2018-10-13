#include <Windows.h>

#include <string>
#include <iostream>
#include <vector>

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include "sigscan.h"
#include "../injector/mapping.h"

#pragma pack(push, 1)
struct SettingsIPC
{
	bool vsync_enabled;
	double fps_cap;

	struct
	{
		int scan_result;
		void *scheduler;
		int sfd_offset;
		int present_count;
	} debug;
};
#pragma pack(pop)

HMODULE MainModule = NULL;
HANDLE SingletonMutex = NULL;
uintptr_t TaskScheduler = 0;
int TaskSchedulerFrameDelayOffset = 0;
FileMapping IPC;

void WINAPI DllInit();
void WINAPI DllExit();

inline SettingsIPC* GetIPC()
{
	return IPC.Get<SettingsIPC *>();
}

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

uintptr_t FindTaskScheduler()
{
	uintptr_t result;

	try
	{
		result = (uintptr_t)sigscan::scan(NULL, "\x55\x8B\xEC\xE8\x00\x00\x00\x00\x8A\x4D\x08\x83\xC0\x04\x86\x08\x5D\xC3", "xxxx????xxxxxxxxxx"); // 55 8B EC E8 ?? ?? ?? ?? 8A 4D 08 83 C0 04 86 08 5D C3
		
		if (result)
		{
			typedef uintptr_t(*GetTaskSchedulerFn)();
			GetTaskSchedulerFn GetTaskScheduler = (GetTaskSchedulerFn)(result + 8 + *(uintptr_t*)(result + 4)); // extract offset from call instruction

			GetIPC()->debug.scan_result = (int)GetTaskScheduler - (int)GetModuleHandleA(NULL);

			result = GetTaskScheduler();
		}
	}
	catch (...)
	{
		result = 0;
	}

	return result;
}

int FindTaskSchedulerFrameDelayOffset(uintptr_t scheduler)
{
	/* Find the frame delay variable inside TaskScheduler (ugly, but it should survive updates unless the variable is removed or shifted) (variable was at +0x228 as of 10/11/2018) */
	for (int i = 0x200; i < 0x300; i += 4)
	{
		static const double frame_delay = 1.0 / 60.0;
		double difference = *(double*)(scheduler + i) - frame_delay;
		difference = difference < 0 ? -difference : difference;
		if (difference < 0.01) return i;
	}

	return 0;
}

typedef HRESULT(_stdcall *IDXGISwapChainPresentFn)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
IDXGISwapChainPresentFn IDXGISwapChainPresent;

HRESULT __stdcall IDXGISwapChainPresentHook(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	auto ipc = IPC.Get<SettingsIPC *>();

	static const double min_frame_delay = 1.0 / 1000000.0; // just using 0 here causes roblox to freeze for some reason
	*(double*)(TaskScheduler + TaskSchedulerFrameDelayOffset) = ipc->fps_cap <= 0.0 ? min_frame_delay : 1.0 / ipc->fps_cap;

	ipc->debug.present_count++;

	return IDXGISwapChainPresent(pSwapChain, ipc->vsync_enabled, Flags);
}

void CheckRunning()
{
	char Name[64];
	sprintf_s(Name, sizeof(Name), "RFUMutex_%d", GetCurrentProcessId());

	SingletonMutex = CreateMutexA(NULL, FALSE, Name);

	if (!SingletonMutex)
	{
		MessageBoxA(NULL, "Unable to create mutex", "Error", MB_OK);
		DllExit();
	}

	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		MessageBoxA(NULL, "Roblox FPS Unlocker is already running in this process", "Error", MB_OK);
		DllExit();
	}
}

void WINAPI DllInit()
{
	CheckRunning();

	if (GetModuleHandleA("dxgi.dll"))
	{
		/* Init IPC */
		IPC.Open("RFUSettingsMap", sizeof(SettingsIPC));
		if (!IPC.IsOpen())
		{
			MessageBoxA(NULL, "Unable to initiate IPC", "Error", MB_OK);
			DllExit();
		}

		/* Detect platform */
		DetectPlatform();

		/* Find TaskScheduler */
		TaskScheduler = FindTaskScheduler();
		if (!TaskScheduler)
		{
			MessageBoxA(NULL, "Unable to find TaskScheduler! This is probably due to a Roblox update-- watch the github for any patches or a fix.", "rbxfpsunlocker Error", MB_OK);
			DllExit();
		}

		/* Find frame delay offset inside TaskScheduler */
		TaskSchedulerFrameDelayOffset = FindTaskSchedulerFrameDelayOffset(TaskScheduler);
		if (!TaskSchedulerFrameDelayOffset)
		{
			MessageBoxA(NULL, "Variable scan failed! This is probably due to a Roblox update-- watch the github for any patches or a fix.", "rbxfpsunlocker Error", MB_OK);
			DllExit();
		}

		GetIPC()->debug.scheduler = (void *)TaskScheduler;
		GetIPC()->debug.sfd_offset = TaskSchedulerFrameDelayOffset;
		GetIPC()->debug.present_count = 0;
		
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


