#pragma once

#include <Windows.h>
#include <map>

class FileMapping
{
private:
	HANDLE file = NULL;
	HANDLE mapping = NULL;
	LPVOID view = NULL;

public:
	LPVOID Open(const char *file_name, const char *map_name, size_t size = 1024)
	{
		if (view)
			return NULL;

		if (file_name)
		{
			file = CreateFileA(file_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file == INVALID_HANDLE_VALUE) return NULL;
		}

		mapping = CreateFileMappingA(file ? file : INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, map_name);
		if (mapping == NULL)
		{
			Close();
			return NULL;
		}

		view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
		if (view == NULL)
		{
			Close();
			return NULL;
		}
		
		return view;
	}

	LPVOID Open(const char *map_name, size_t size = 1024)
	{
		if (view)
			return NULL;

		mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, map_name);
		if (mapping == NULL)
			return NULL;

		view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
		if (view == NULL)
		{
			Close();
			return NULL;
		}

		return view;
	}

	void Close()
	{
		if (view) UnmapViewOfFile(view);
		if (mapping) CloseHandle(mapping);
		if (file) CloseHandle(file);

		file = NULL;
		mapping = NULL;
		view = NULL;
	}

	template <typename T>
	inline T Get()
	{
		return (T)view;
	}

	inline bool IsOpen()
	{
		return view != NULL;
	}

	~FileMapping()
	{
		Close();
	}
};