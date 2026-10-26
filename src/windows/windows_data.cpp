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

	// Runs a WQL query against the given WMI namespace, invoking the callback for every returned object.
	// Returns false if WMI could not be queried at all.
	template <typename Callback>
	bool QueryWMI( const wchar_t* wmiNamespace, const wchar_t* query, Callback&& callback )
	{
		// RPC_E_CHANGED_MODE means COM is already initialized on this thread with a different
		// concurrency model, which is fine for our purposes, but we must not uninitialize it.
		HRESULT hr = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
		if( FAILED( hr ) && hr != RPC_E_CHANGED_MODE )
		{
			return false;
		}
		SCOPE_EXIT( if( SUCCEEDED( hr ) ) CoUninitialize() );

		// RPC_E_TOO_LATE means security has already been initialized for this process
		HRESULT secHr = CoInitializeSecurity( nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr );
		if( FAILED( secHr ) && secHr != RPC_E_TOO_LATE )
		{
			return false;
		}

		CComPtr<IWbemLocator> pLoc;
		if( FAILED( CoCreateInstance( CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<LPVOID*>( &pLoc ) ) ) )
		{
			return false;
		}

		CComPtr<IWbemServices> pSvc;
		if( FAILED( pLoc->ConnectServer( bstr_t( wmiNamespace ), nullptr, nullptr, nullptr, NULL, nullptr, nullptr, &pSvc ) ) )
		{
			return false;
		}

		if( FAILED( CoSetProxyBlanket( pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE ) ) )
		{
			return false;
		}

		CComPtr<IEnumWbemClassObject> pEnumerator;
		if( FAILED( pSvc->ExecQuery( bstr_t( "WQL" ), bstr_t( query ), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnumerator ) ) )
		{
			return false;
		}

		while( pEnumerator )
		{
			CComPtr<IWbemClassObject> pclsObj;
			ULONG ret = 0;
			if( FAILED( pEnumerator->Next( WBEM_INFINITE, 1, &pclsObj, &ret ) ) || !ret )
			{
				break;
			}
			callback( *pclsObj );
		}

		return true;
	}

	std::string GetWMIString( IWbemClassObject& obj, const wchar_t* name )
	{
		VARIANT vtProp{};
		if( FAILED( obj.Get( name, 0, &vtProp, nullptr, nullptr ) ) )
		{
			return "";
		}
		auto str = vtProp.vt == VT_BSTR && vtProp.bstrVal ? WStringToUTF8( vtProp.bstrVal ) : "";
		VariantClear( &vtProp );
		return str;
	}

	long GetWMILong( IWbemClassObject& obj, const wchar_t* name )
	{
		VARIANT vtProp{};
		if( FAILED( obj.Get( name, 0, &vtProp, nullptr, nullptr ) ) )
		{
			return 0l;
		}
		auto lval = vtProp.lVal;
		VariantClear( &vtProp );
		return lval;
	}

	std::vector<HardDriveInfo> GetHardDriveInfo()
	{
		std::vector<HardDriveInfo> drives;
		QueryWMI( L"root\\Microsoft\\Windows\\Storage", L"SELECT * FROM MSFT_PhysicalDisk", [&]( IWbemClassObject& obj ) {
			std::string name = GetWMIString(obj, L"FriendlyName");
			long mediaType = GetWMILong(obj, L"MediaType");
			std::string sizeStr = GetWMIString(obj, L"Size");

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
				return;
			}
			uint64_t size = std::atoll(sizeStr.c_str());

			drives.push_back({name, type, size});
		});

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

	std::string GetOSName()
	{
		if( IsWine() )
		{
			return "";
		}

		// The registry ProductName reports "Windows 10" even on Windows 11, so prefer WMI
		std::string name;
		QueryWMI( L"root\\CIMV2", L"SELECT Caption FROM Win32_OperatingSystem", [&]( IWbemClassObject& obj ) {
			if( name.empty() )
			{
				name = GetWMIString( obj, L"Caption" );
			}
		} );

		// Match the registry format, e.g. "Microsoft Windows 11 Pro " -> "Windows 11 Pro"
		const std::string prefix = "Microsoft ";
		if( name.rfind( prefix, 0 ) == 0 )
		{
			name = name.substr( prefix.size() );
		}
		name.erase( name.find_last_not_of( " \t" ) + 1 );

		return name.empty() ? GetStringFromReg( CURRENT_VERSION_KEY, L"ProductName" ) : name;
	}

	std::string GetOSMajorVersion()
	{
		if (IsWine()) return "";
		std::string version = GetStringFromReg(CURRENT_VERSION_KEY, L"CurrentMajorVersionNumber");
		if (!version.empty()) return version;

		version = GetOSKernelVersion();
		if (version.find('.') != -1) return version.substr(0, version.find('.'));

		return "";
	}

	std::string GetOSMinorVersion()
	{
		if (IsWine()) return "";
		std::string version = GetStringFromReg(CURRENT_VERSION_KEY, L"CurrentMinorVersionNumber");
		if (!version.empty()) return version;

		version = GetOSKernelVersion();
		if (version.find('.') != -1) return version.substr(version.find('.') + 1);

		return "";
	}

	std::string GetOSBuildNumber()
	{
		return IsWine() ? "" : GetStringFromReg(CURRENT_VERSION_KEY, L"CurrentBuild");
	}

	std::string GetOSKernelVersion()
	{
		return IsWine() ? "" : GetStringFromReg(CURRENT_VERSION_KEY, L"CurrentVersion");
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
