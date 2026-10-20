// Copyright © 2026 CCP ehf.

#include "utilities.h"
#include "../include/pdm.h"

#include <regex>

#if _WIN32
#include <codecvt>
#include <Windows.h>
#endif

namespace PDM
{
	std::vector<uint8_t> HexStringToByteArray(std::string uuid, size_t byteCount)
	{
		auto str = std::regex_replace(uuid, std::regex("[:-]"), "");
		if (str.length() != byteCount * 2)
			return {};

		std::vector<uint8_t> bytes;
		for (size_t i = 0; i < byteCount; i++)
		{
			try
			{
				int c = std::stoi(str.substr(i*2, 2), nullptr, 16);
				bytes.push_back(c);
			}
			catch (std::invalid_argument&)
			{
				return {};
			}
		}

		return bytes;
	}
	
	std::string tolower(std::string str)
	{
		transform(str.begin(), str.end(), str.begin(), ::tolower);
		return str;
	}

#if _WIN32
	std::string WStringToUTF8(const std::wstring_view wideString)
	{
		if (wideString.empty())
			return {};

		const int size_needed = WideCharToMultiByte(CP_UTF8, 0, wideString.data(), wideString.size(), nullptr, 0, nullptr, nullptr);
		std::string result(size_needed, 0);
		WideCharToMultiByte(CP_UTF8, 0, wideString.data(), wideString.size(), result.data(), result.size(), nullptr, nullptr);
		return result;
	}

	std::wstring UTF8ToWString(const std::string_view utf8String)
	{
		if (utf8String.empty())
			return {};

		const int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8String.data(), utf8String.size(), nullptr, 0);
		std::wstring result(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, utf8String.data(), utf8String.size(), result.data(), result.size());
		return result;
	}

	std::wstring NativeToWString(const std::string_view str)
	{
		if (str.empty())
			return {};

		int count = MultiByteToWideChar(CP_ACP, 0, str.data(), str.size(), nullptr, 0);
		std::wstring wstr(count, 0);
		MultiByteToWideChar(CP_ACP, 0, str.data(), str.size(), &wstr[0], count);
		return wstr;
	}

	std::string WStringToNative(const std::wstring_view wideString)
	{
		if (wideString.empty())
			return {};

		const int size_needed = WideCharToMultiByte(CP_ACP, 0, wideString.data(), wideString.size(), nullptr, 0, nullptr, nullptr);
		std::string result(size_needed, 0);
		WideCharToMultiByte(CP_ACP, 0, wideString.data(), wideString.size(), result.data(), result.size(), nullptr, nullptr);
		return result;
	}

	std::string UTF8ToNative(const std::string_view utf8String)
	{
		return WStringToNative(UTF8ToWString(utf8String));
	}

	bool GetMetalSupported()
	{
		return false;
	}

	bool IsRosetta()
	{
		return false;
	}

#elif __APPLE__
	std::string UTF8ToNative(const std::string_view utf8String)
	{
		return std::string(utf8String);
	}
	// macOS implementation of UTF8ToWString is in macos_data.mm
#elif __linux__
	std::string UTF8ToNative(const std::string_view utf8String)
	{
		return std::string(utf8String);
	}

	std::wstring UTF8ToWString(const std::string_view utf8String)
	{
		// wchar_t is UTF-32 on Linux, so each code point is one character. Malformed sequences become U+FFFD.
		std::wstring result;
		result.reserve(utf8String.size());

		for (size_t i = 0; i < utf8String.size();)
		{
			auto lead = static_cast<unsigned char>(utf8String[i]);
			size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xe ? 3 : (lead >> 3) == 0x1e ? 4 : 0;

			bool valid = length != 0 && i + length <= utf8String.size();
			for (size_t j = 1; valid && j < length; j++)
				valid = (static_cast<unsigned char>(utf8String[i + j]) & 0xc0) == 0x80;

			if (!valid)
			{
				result += L'�';
				i++;
				continue;
			}

			char32_t codePoint = length == 1 ? lead : lead & (0x7f >> length);
			for (size_t j = 1; j < length; j++)
				codePoint = (codePoint << 6) | (static_cast<unsigned char>(utf8String[i + j]) & 0x3f);

			result += static_cast<wchar_t>(codePoint);
			i += length;
		}

		return result;
	}
#else
#error unsupported platform
#endif
}
