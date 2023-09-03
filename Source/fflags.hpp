#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include "nlohmann.hpp"

using target_fps_t = std::optional<nlohmann::json::number_integer_t>;
using alt_enter_t = std::optional<bool>;

struct RobloxFFlags
{
	const std::filesystem::path settings_file_path;
	nlohmann::json object;
	bool target_fps_mod = false;
	bool alt_enter_mod = false;

	RobloxFFlags(const std::filesystem::path &version_folder) : settings_file_path(version_folder / "ClientSettings" / "ClientAppSettings.json")
	{
		read_disk();
	}

	void read_disk()
	{
		object = {};
		std::ifstream file(settings_file_path);

		if (file.is_open())
		{
			nlohmann::json result = nlohmann::json::parse(file, nullptr, false);
			if (!result.is_discarded())
			{
				object = std::move(result);
			}
		}
	}

	bool write_disk()
	{
		std::error_code ec{};
		std::filesystem::create_directory(settings_file_path.parent_path(), ec);

		std::ofstream file(settings_file_path);
		if (!file.is_open()) return false;

		file << object.dump(4);
		return true;
	}

	template <typename T>
	std::optional<T> read_json_opt(const char *key) const
	{
		if (object.contains(key))
		{
			if (auto ptr = object[key].template get_ptr<const T*>())
			{
				return *ptr;
			}
		}

		return {};
	}

	template <typename T>
	bool update_flag(const char *key, std::optional<T> new_value)
	{
		auto flag = read_json_opt<T>(key);
		if (flag != new_value)
		{
			if (new_value) object[key] = *new_value;
			else object.erase(key);
			return true;
		}
		return false;
	}

	target_fps_t target_fps() const
	{
		return read_json_opt<nlohmann::json::number_integer_t>("DFIntTaskSchedulerTargetFps");
	}

	RobloxFFlags &set_target_fps(target_fps_t cap_opt)
	{
		if (cap_opt) *cap_opt = *cap_opt == 0 ? 5588562 : *cap_opt;
		if (update_flag("DFIntTaskSchedulerTargetFps", cap_opt)) target_fps_mod = true;
		return *this;
	}

	alt_enter_t alt_enter() const
	{
		return read_json_opt<bool>("FFlagHandleAltEnterFullscreenManually");
	}

	RobloxFFlags &set_alt_enter_flag(alt_enter_t alt_enter_opt)
	{
		if (update_flag("FFlagHandleAltEnterFullscreenManually", alt_enter_opt)) alt_enter_mod = true;
		return *this;
	}

	bool apply(bool prompt);
};