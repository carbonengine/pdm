// Copyright © 2026 CCP ehf.

#pragma once

#include <string>
#include <vector>
#include "../include/pdm_data.h"

namespace PDM
{
	std::vector<uint8_t> HexStringToByteArray(std::string uuid, size_t byteCount);
	std::string tolower(std::string str);

	std::string UTF8ToNative(const std::string_view utf8String);
	std::wstring UTF8ToWString(const std::string_view utf8String);

#if _WIN32
	std::string WStringToUTF8(const std::wstring_view wideString);
	std::string WStringToNative(const std::wstring_view wideString);
	std::wstring NativeToWString(const std::string_view str);
#elif __APPLE__ || __linux__
	std::string GetOSString(const char* name);
	uint64_t GetOSInteger(const char* name);
#else
#error "Unsupported platform"
#endif
}
