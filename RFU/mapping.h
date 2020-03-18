#pragma once

#include <Windows.h>

class FileMapping
{
	HANDLE file = nullptr;
	HANDLE mapping = nullptr;
	LPVOID view = nullptr;

public:
	LPVOID Open(const char *file_name, const char *map_name, size_t size = 1024)
	{
		if (view)
			return nullptr;

		if (file_name)
		{
			file = CreateFileA(file_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return nullptr;
		}

		mapping = CreateFileMappingA(file ? file : INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, map_name);
		if (mapping == nullptr)
		{
			Close();
			return nullptr;
		}

		view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
		if (view == nullptr)
		{
			Close();
			return nullptr;
		}
		
		return view;
	}

	LPVOID Open(const char *map_name, size_t size = 1024)
	{
		if (view)
			return nullptr;

		mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, map_name);
		if (mapping == nullptr)
			return nullptr;

		view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
		if (view == nullptr)
		{
			Close();
			return nullptr;
		}

		return view;
	}

	void Close()
	{
		if (view) UnmapViewOfFile(view);
		if (mapping) CloseHandle(mapping);
		if (file) CloseHandle(file);

		file = nullptr;
		mapping = nullptr;
		view = nullptr;
	}

	template <typename T>
	T Get()
	{
		return static_cast<T>(view);
	}

	bool IsOpen()
	{
		return view != nullptr;
	}

	~FileMapping()
	{
		Close();
	}
};