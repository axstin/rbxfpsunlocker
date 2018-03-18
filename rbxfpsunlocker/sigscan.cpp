#include "sigscan.h"

#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")

namespace sigscan
{
	bool compare(const char* location, const char* aob, const char* mask)
	{
		for (; *mask; ++aob, ++mask, ++location)
		{
			if (*mask == 'x' && *location != *aob)
			{
				return false;
			}
		}

		return true;
	}

	bool compare_reverse(const char* location, const char* aob, const char* mask)
	{
		const char* mask_iter = mask + strlen(mask) - 1;
		for (; mask_iter >= mask; --aob, --mask_iter, --location)
		{
			if (*mask_iter == 'x' && *location != *aob)
			{
				return false;
			}
		}

		return true;
	}

	byte* scan(const char* aob, const char* mask, uintptr_t start, uintptr_t end)
	{
		if (start <= end)
		{
			for (; start <= end; ++start)
			{
				if (compare((char*)start, (char*)aob, mask))
				{
					return (byte*)start;
				}
			}
		}
		else
		{
			for (; start >= end; --start)
			{
				if (compare_reverse((char*)start, (char*)aob, mask))
				{
					return (byte*)start - strlen(mask) - 1;
				}
			}
		}

		return 0;
	};

	byte* scan(const char* module, const char* aob, const char* mask)
	{
		MODULEINFO info;
		if (GetModuleInformation(GetCurrentProcess(), GetModuleHandle(module), &info, sizeof(info)))
			return scan(aob, mask, (uintptr_t)info.lpBaseOfDll, (uintptr_t)info.lpBaseOfDll + info.SizeOfImage);

		return 0;
	}
}