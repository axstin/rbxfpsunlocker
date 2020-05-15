#pragma once

#include <cstdint>

namespace sigscan
{
	bool compare(const char *location, const char *aob, const char *mask);
	bool compare_reverse(const char *location, const char *aob, const char *mask);
	uint8_t *scan(const char *aob, const char *mask, uintptr_t start, uintptr_t end);
	uint8_t *scan(const char *module, const char *aob, const char *mask);
}