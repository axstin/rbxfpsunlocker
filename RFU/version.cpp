#include "rfu.h"

#include <Windows.h>

#pragma comment(lib, "WinInet.lib")
#include <WinInet.h>

#include <string>
#include <regex>

bool HttpRequest(const char *url, std::string &response)
{
	if (const auto internet = InternetOpenA("axstin/rbxfpsunlocker", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, NULL))
	{
		if (const auto request = InternetOpenUrlA(internet, url, nullptr, 0, INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE, NULL))
		{
			char buffer[1024];
			DWORD bytes_read;

			while (InternetReadFile(request, buffer, sizeof buffer, &bytes_read) && bytes_read > 0)
			{
				response.append(buffer, bytes_read);
			}

			InternetCloseHandle(internet);
			InternetCloseHandle(request);
			return true;
		}
		else
		{
			InternetCloseHandle(internet);
			return false;
		}
	}

	return false;
}

bool CheckForUpdates()
{
	std::string response;
	if (!HttpRequest("https://api.github.com/repos/" RFU_GITHUB_REPO "/releases/latest", response))
	{
		MessageBoxA(nullptr, "Failed to connect to Github", "Update Check", MB_OK);
		return false;
	}

	std::smatch matches;
	std::regex_search(response, matches, std::regex(R"x("tag_name":\s*"v?([^"]+))x")); // "tag_name":\s*"v?(.+)"

	if (matches.size() <= 1)
	{
		printf("Response: %s\n", response.c_str());
		MessageBoxA(nullptr, "Invalid response", "Update Check", MB_OK);
		return false;
	}

	const auto latest_version = matches[1].str();

	if (latest_version != RFU_VERSION)
	{
		char buffer[256];
		sprintf_s(buffer, "A new version of Roblox FPS Unlocker is available.\n\nCurrent Version: %s\nLatest Version: %s\n\nVisit download page?", RFU_VERSION, latest_version.c_str());

		if (MessageBoxA(nullptr, buffer, "Update Check", MB_YESNOCANCEL | MB_ICONEXCLAMATION) == IDYES)
		{
			ShellExecuteA(nullptr, "open", "https://github.com/" RFU_GITHUB_REPO "/releases", nullptr, nullptr, SW_SHOWNORMAL);
			return true;
		}
	}

	return false;
}