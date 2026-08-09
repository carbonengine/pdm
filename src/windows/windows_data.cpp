// Copyright © 2026 CCP ehf.

#include <string>

#if _WIN32

#include <winsock2.h>

#include "d3d_info.h"
#include "../../include/pdm_data.h"
#include "../defines.h"
#include "../gatherer.h"
#include "../utilities.h"

#include <algorithm>
#include <sstream>
#include <clocale>
#include <atlstr.h>
#include <Lmcons.h>
#include <intrin.h>
#include <iomanip>
#include <Iphlpapi.h>
#include <sysinfoapi.h>
#include <powerbase.h>
#include <ntstatus.h>
#include <comdef.h>
#include <Wbemidl.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "PowrProf.lib")


namespace PDM
{
	const std::wstring CURRENT_VERSION_KEY = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

	std::string GetStringFromReg(const std::wstring& keyName, const std::wstring& keyValName)
	{
		const HKEY parent = HKEY_LOCAL_MACHINE;

		wchar_t data[256] = {0};
		auto size = sizeof(data);
		auto len = (LPDWORD)&size;
		LONG retCode = RegGetValueW(
			parent,
			keyName.c_str(),
			keyValName.c_str(),
			RRF_RT_REG_SZ,
			nullptr,
			&data,
			len
		);

		if (retCode == ERROR_SUCCESS)
			return WStringToUTF8(std::wstring(data, *len / 2 - 1));

		DWORD number;
		size = sizeof(DWORD);
		len = (LPDWORD)&size;
		retCode = RegGetValueW(
			parent,
			keyName.c_str(),
			keyValName.c_str(),
			RRF_RT_REG_DWORD,
			nullptr,
			&number,
			len
		);
		
		if (retCode == ERROR_SUCCESS)
			return std::to_string(number);

		return "";
	}

	Bitness GetOSBitnessInternal()
	{
#if _WIN64
		return Bitness::BITNESS_64;
#else
		BOOL isWow = false;
		IsWow64Process(GetCurrentProcess(), &isWow);
		return isWow ? Bitness::BITNESS_64 : Bitness::BITNESS_32;
#endif
	}

	std::vector<HardDriveInfo> GetHardDriveInfo()
	{
		if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return {};
		SCOPE_EXIT(CoUninitialize());

		if (FAILED(CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr))) return {};

		IWbemLocator* pLoc = nullptr;
		if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<LPVOID*>(&pLoc)))) return {};

		IWbemServices* pSvc = nullptr;
		if (FAILED(pLoc->ConnectServer(bstr_t(L"root\\Microsoft\\Windows\\Storage"), nullptr, nullptr, nullptr, NULL, nullptr, nullptr, &pSvc))) return {};
		SCOPE_EXIT(pLoc->Release());

		if (FAILED(CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE))) return {};
		SCOPE_EXIT(pSvc->Release());

		IEnumWbemClassObject* pEnumerator = nullptr;
		if (FAILED(pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT * FROM MSFT_PhysicalDisk"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnumerator))) return {};

		IWbemClassObject* pclsObj = nullptr;
		std::vector<HardDriveInfo> drives;
		while (pEnumerator)
		{
			ULONG ret = 0;
			if (FAILED(pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &ret)) || !ret) break;
			SCOPE_EXIT(pclsObj->Release());

			auto vtStr = [](IWbemClassObject& obj, const wchar_t* name)
			{
				VARIANT vtProp{};
				if (FAILED(obj.Get(name, 0, &vtProp, nullptr, nullptr))) return std::string();
				auto str = std::string(WStringToUTF8(vtProp.bstrVal));
				VariantClear(&vtProp);
				return str;
			};

			auto vtLong = [](IWbemClassObject& obj, const wchar_t* name)
			{
				VARIANT vtProp{};
				if (FAILED(obj.Get(name, 0, &vtProp, nullptr, nullptr))) return 0l;
				auto lval = vtProp.lVal;
				VariantClear(&vtProp);
				return lval;
			};

			std::string name = vtStr(*pclsObj, L"FriendlyName");
			long mediaType = vtLong(*pclsObj, L"MediaType");
			std::string sizeStr = vtStr(*pclsObj, L"Size");

			HardDriveInfo::HardDriveType type = HardDriveInfo::HardDriveType::UNKNOWN;
			switch (mediaType)
			{
			case 3:
				type = HardDriveInfo::HardDriveType::HDD;
				break;
			case 4:
				type = HardDriveInfo::HardDriveType::SSD;
				break;
			default:
				// We don't care about USB sticks and such
				continue;
			}
			uint64_t size = std::atoll(sizeStr.c_str());

			drives.push_back({name, type, size});
		}

		return drives;
	}

	uint32_t GetCPUFrequency()
	{
		// WTF Microsoft?
		// "Note that this structure definition was accidentally omitted from WinNT.h.
		// This error will be corrected in the future.
		// In the meantime, to compile your application, include the structure definition contained in this topic in your source code."
		struct PROCESSOR_POWER_INFORMATION {
			ULONG  Number;
			ULONG  MaxMhz;
			ULONG  CurrentMhz;
			ULONG  MhzLimit;
			ULONG  MaxIdleState;
			ULONG  CurrentIdleState;
		};

		SYSTEM_INFO systemInfo;
		GetSystemInfo(&systemInfo);

		std::vector<PROCESSOR_POWER_INFORMATION> data(systemInfo.dwNumberOfProcessors);
		NTSTATUS status = CallNtPowerInformation(ProcessorInformation, nullptr, 0, &data[0], data.size() * sizeof(PROCESSOR_POWER_INFORMATION));
		return status == STATUS_SUCCESS ? data[0].MaxMhz : 0;
	}

	OS GetOSType()
	{
		return IsWine() ? OS::WINE : OS::WINDOWS;
	}

	// One cached Win32_OperatingSystem query feeds the whole OS block below. WMI reports the
	// true current OS, whereas several CurrentVersion registry values are frozen legacy strings
	// (ProductName stays "Windows 10 ..." and CurrentVersion stays "6.3" on Windows 10/11). The
	// query runs at most once (static cache); every getter falls back to the registry when WMI
	// is unavailable.
	struct Win32OsInfo
	{
		std::string caption;     // e.g. "Microsoft Windows 11 Pro"
		std::string version;     // e.g. "10.0.26200" (major.minor.build)
		std::string buildNumber; // e.g. "26200"
	};

	Win32OsInfo QueryWin32OsInfo()
	{
		Win32OsInfo info;
		if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return info;
		SCOPE_EXIT(CoUninitialize());

		// CoInitializeSecurity is process-wide and may already be set by another PDM WMI
		// query (e.g. GetHardDriveInfo); treat "already set" as success.
		const HRESULT hrSec = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
		if (FAILED(hrSec) && hrSec != RPC_E_TOO_LATE) return info;

		IWbemLocator* pLoc = nullptr;
		if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<LPVOID*>(&pLoc)))) return info;
		SCOPE_EXIT(pLoc->Release());

		IWbemServices* pSvc = nullptr;
		if (FAILED(pLoc->ConnectServer(bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc))) return info;
		SCOPE_EXIT(pSvc->Release());

		if (FAILED(CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE))) return info;

		IEnumWbemClassObject* pEnumerator = nullptr;
		if (FAILED(pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT Caption,Version,BuildNumber FROM Win32_OperatingSystem"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnumerator))) return info;
		SCOPE_EXIT(pEnumerator->Release());

		IWbemClassObject* pclsObj = nullptr;
		ULONG ret = 0;
		if (FAILED(pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &ret)) || !ret) return info;
		SCOPE_EXIT(pclsObj->Release());

		auto getStr = [&pclsObj](const wchar_t* name)
		{
			VARIANT vtProp{};
			std::string out;
			if (SUCCEEDED(pclsObj->Get(name, 0, &vtProp, nullptr, nullptr)) && vtProp.vt == VT_BSTR && vtProp.bstrVal)
				out = WStringToUTF8(vtProp.bstrVal);
			VariantClear(&vtProp);
			return out;
		};

		info.caption     = getStr(L"Caption");
		info.version     = getStr(L"Version");
		info.buildNumber = getStr(L"BuildNumber");
		return info;
	}

	const Win32OsInfo& GetWin32OsInfo()
	{
		static const Win32OsInfo info = IsWine() ? Win32OsInfo{} : QueryWin32OsInfo();
		return info;
	}

	// Returns the dotted component at 'index' of a "major.minor.build" string, or "" if absent.
	std::string VersionComponent(const std::string& version, size_t index)
	{
		size_t start = 0;
		for (size_t i = 0; i < index; ++i)
		{
			const size_t dot = version.find('.', start);
			if (dot == std::string::npos) return "";
			start = dot + 1;
		}
		const size_t end = version.find('.', start);
		return version.substr(start, end == std::string::npos ? std::string::npos : end - start);
	}

	std::string GetOSName()
	{
		if (IsWine()) return "";

		// Prefer WMI's Win32_OperatingSystem.Caption: it reports the true current OS. The
		// ProductName registry value stays "Windows 10 ..." on Windows 11, so reading it
		// directly mislabels the OS. Strip the "Microsoft " prefix to keep the prior format.
		std::string name = GetWin32OsInfo().caption;
		const std::string kMicrosoftPrefix = "Microsoft ";
		if (name.rfind(kMicrosoftPrefix, 0) == 0)
			name.erase(0, kMicrosoftPrefix.size());
		if (!name.empty())
			return name;

		// Fallback when WMI is unavailable: registry ProductName, corrected for the Windows 11
		// build range (>= 22000) where ProductName still reads "Windows 10 ...".
		name = GetStringFromReg(CURRENT_VERSION_KEY, L"ProductName");
		if (std::atoll(GetOSBuildNumber().c_str()) >= 22000)
		{
			const auto pos = name.find("Windows 10");
			if (pos != std::string::npos)
				name.replace(pos, 10, "Windows 11");
		}
		return name;
	}

	std::string GetOSMajorVersion()
	{
		if (IsWine()) return "";
		std::string major = VersionComponent(GetWin32OsInfo().version, 0);
		if (!major.empty()) return major;
		return GetStringFromReg(CURRENT_VERSION_KEY, L"CurrentMajorVersionNumber");
	}

	std::string GetOSMinorVersion()
	{
		if (IsWine()) return "";
		std::string minor = VersionComponent(GetWin32OsInfo().version, 1);
		if (!minor.empty()) return minor;
		return GetStringFromReg(CURRENT_VERSION_KEY, L"CurrentMinorVersionNumber");
	}

	std::string GetOSBuildNumber()
	{
		if (IsWine()) return "";
		const std::string& build = GetWin32OsInfo().buildNumber;
		if (!build.empty()) return build;
		return GetStringFromReg(CURRENT_VERSION_KEY, L"CurrentBuild");
	}

	std::string GetOSKernelVersion()
	{
		if (IsWine()) return "";
		// NT major.minor of the true version (e.g. "10.0"); the CurrentVersion registry value is
		// frozen at "6.3" on Windows 10/11.
		const std::string& version = GetWin32OsInfo().version;
		const std::string major = VersionComponent(version, 0);
		const std::string minor = VersionComponent(version, 1);
		if (!major.empty() && !minor.empty()) return major + "." + minor;
		return GetStringFromReg(CURRENT_VERSION_KEY, L"CurrentVersion");
	}

	std::string GetHardwareModel()
	{
		auto SystemInformationKey = L"SYSTEM\\CurrentControlSet\\Control\\SystemInformation";

		std::string manu = GetStringFromReg(SystemInformationKey, L"SystemManufacturer");
		if (manu == "System manufacturer") manu = "";

		std::string prod = GetStringFromReg(SystemInformationKey, L"SystemProductName");
		if (prod == "System Product Name") prod = "";
		if (!manu.empty() && !prod.empty()) prod = " [" + prod + "]";

		return manu + prod;
	}

	std::string GetMachineName()
	{
		constexpr auto INFO_BUFFER_SIZE = 1024;
		wchar_t infoBuf[INFO_BUFFER_SIZE] = {0};
		DWORD bufCharCount = INFO_BUFFER_SIZE;
		return WStringToUTF8(GetComputerNameW(infoBuf, &bufCharCount) ? infoBuf : L"");
	}

	std::string GetUsername()
	{
		wchar_t username[UNLEN + 1] = {0};
		DWORD username_len = UNLEN + 1;
		return WStringToUTF8(GetUserNameW(username, &username_len) ? username : L"");
	}

	uint32_t GetMonitorCount()
	{
		return GetSystemMetrics(SM_CMONITORS);
	}

	uint64_t GetTotalMemory()
	{
		ULONGLONG kbytes;
		return GetPhysicallyInstalledSystemMemory(&kbytes) ? kbytes * 1024 : 0;
	}

	BatteryStatus GetBatteryStatus()
	{
		SYSTEM_POWER_STATUS spsPwr;
		if (!GetSystemPowerStatus(&spsPwr) || spsPwr.BatteryFlag == 0xFF)
			return BatteryStatus::UNKNOWN;
		if ((spsPwr.BatteryFlag & 0x80) == 0x80)
			return BatteryStatus::NOT_DETECTED;
		return BatteryStatus::DETECTED;
	}

	std::string GetMachineUuidString()
	{
		REGSAM access = KEY_READ;
#if !_WIN64
		if (GetOSBitnessInternal() != Bitness::BITNESS_32)
			access |= KEY_WOW64_64KEY;
#endif

		HKEY key;
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, access, &key) != ERROR_SUCCESS) return "";

		DWORD type;
		char guid[256] = {0};
		DWORD size = sizeof(guid);
		LSTATUS status = RegQueryValueExA(key, "MachineGuid", nullptr, &type, reinterpret_cast<LPBYTE>(guid), &size);
		RegCloseKey(key);

		return status == ERROR_SUCCESS && type == REG_SZ ? toupper(std::string(guid)) : "";
	}

	std::vector<NetworkAdapterInfo> GetNetworkAdapterInfo()
	{
		ULONG size = 0;
		if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW) return {};

		std::vector<uint8_t> data(size);
		auto addr = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(data.data());
		if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, addr, &size) != ERROR_SUCCESS) return {};

		std::vector<NetworkAdapterInfo> adapters;
		do
		{
			std::vector<uint8_t> macAddress;
			std::stringstream stream;
			bool hasPhysical = false;
			for (unsigned i = 0; i < 6; i++)
			{
				if (i) stream << ":";
				auto val = static_cast<unsigned>(addr->PhysicalAddress[i]);
				if (val <= 0xf) stream << "0";
				stream << std::hex << val;

				macAddress.push_back(val);

				if (val) hasPhysical = true;
			}

			std::string uuid = addr->AdapterName;
			if (uuid.rfind("{") == 0 && uuid[uuid.size() - 1] == '}')
				uuid = uuid.substr(1, uuid.size() - 2);
			
			if (hasPhysical && std::wstring(addr->Description).find(L"Software Loopback Interface") != 0)
			{
				adapters.push_back
				({
					WStringToUTF8(addr->Description),
					toupper(std::string(stream.str())),
					uuid,
					macAddress,
					HexStringToByteArray(uuid, 16)
				});
			}

			addr = addr->Next;
		}
		while(addr);

		return adapters;
	}

	std::string GetUserLocale()
	{
		auto locale = std::setlocale(LC_ALL, "");
		std::string loc(locale ? locale : "");
		std::setlocale(LC_ALL, "C"); // Reset
		return WStringToUTF8(NativeToWString(loc));
	}

	bool IsRemoteSession()
	{
		return GetSystemMetrics(SM_REMOTESESSION);
	}

	std::vector<GPUInfo> GetGPUInfo()
	{
		if (IsWine()) return {};
		return GetD3DInfo().adapters;
	}

	std::vector<MonitorInfo> GetMonitorsInfo()
	{
		if (IsWine()) return {};
		return GetD3DInfo().monitors;
	}

#pragma warning(disable:26812)
	constexpr const char* D3DFeatureSupportToString(D3D_FEATURE_LEVEL support)
#pragma warning(default:26812)
	{
		switch (support)
		{
		case D3D_FEATURE_LEVEL_12_2:
			return "12.2";
		case D3D_FEATURE_LEVEL_12_1:
			return "12.1";
		case D3D_FEATURE_LEVEL_12_0:
			return "12.0";
		case D3D_FEATURE_LEVEL_11_1:
			return "11.1";
		case D3D_FEATURE_LEVEL_11_0:
			return "11.0";
		case D3D_FEATURE_LEVEL_10_1:
			return "10.1";
		case D3D_FEATURE_LEVEL_10_0:
			return "10.0";
		case D3D_FEATURE_LEVEL_9_3:
			return "9.3";
		case D3D_FEATURE_LEVEL_9_2:
			return "9.2";
		case D3D_FEATURE_LEVEL_9_1:
			return "9.1";
		case D3D_FEATURE_LEVEL_1_0_CORE:
			return "1.0 CORE";
		case D3D_FEATURE_LEVEL_NONE:
			return "NONE";
		default:
			return "UNKNOWN";
		}
	}

	std::string GetD3DHighestSupport()
	{
		return D3DFeatureSupportToString(GetD3DInfo().maxSupportedFeatureLevel);
	}
}

#else

namespace PDM
{
	std::string GetD3DHighestSupport()
	{
		return "NONE";
	}
}

#endif
