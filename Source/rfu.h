#pragma once

#include <cstdint>

#define RFU_VERSION "5.2"
#define RFU_GITHUB_REPO "axstin/rbxfpsunlocker"

namespace RFU
{
	namespace Event
	{
		constexpr uint32_t UNLOCK_CLIENT = 1u << 0u;
		constexpr uint32_t UNLOCK_STUDIO = 1u << 1u;
		constexpr uint32_t FPS_CAP = 1u << 2u;
		constexpr uint32_t ALT_ENTER = 1u << 3u;
		constexpr uint32_t UNLOCK_METHOD = 1u << 4u;
		constexpr uint32_t SETTINGS_MASK = (1u << 5u) - 1u; // indicates a setting event, but not necessarily that settings were changed (e.g. load from disk)

		constexpr uint32_t PROCESS_DIED = 1u << 5u;
		constexpr uint32_t CLOSE = 1u << 6u;
		constexpr uint32_t APP_EXIT = 1u << 7u;
		constexpr uint32_t CLOSE_MASK = PROCESS_DIED | CLOSE | APP_EXIT;
	}

	bool CheckForUpdates();
	void OnEvent(uint32_t);
}


