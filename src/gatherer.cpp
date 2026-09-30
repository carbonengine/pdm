// Copyright © 2026 CCP ehf.

#include "gatherer.h"
#include "defines.h"
#include "utilities.h"
#include "../include/pdm.h"
#include "../include/version.h"
#include "cpu.h"

#include <string>
#include <ctime>
#include <optional>
#include <thread>

namespace PDM
{
	Bitness GetOSBitnessInternal();
	uint32_t GetCPUFrequency();

	std::string GetPDMVersion()
	{
		return PROJECT_VER;
	}

	CPUInfo GetCPUInfo()
	{
		Bitness bitness = CPUID::GetBitness();
		std::string brand = CPUID::GetBrand();
		std::string vendor = CPUID::GetVendor();
		int32_t model = CPUID::GetModel();
		int32_t stepping = CPUID::GetStepping();
		unsigned logicalCoreCount = std::thread::hardware_concurrency();
		CPUArchitecture architecture = GetCPUArchitecture();
		std::vector<std::string> extensions = GetCPUExtensions();
		uint32_t frequency = GetCPUFrequency();

		return { model, stepping, vendor, brand, bitness, logicalCoreCount, architecture, extensions, frequency };
	}

	Bitness GetProcessBitness()
	{
		switch (sizeof(void*))
		{
		case 8:
			return Bitness::BITNESS_64;
		case 4:
			return Bitness::BITNESS_32;
		default:
			return Bitness::BITNESS_UNKNOWN;
		}
	}

	Bitness GetOSBitness()
	{
		if (GetProcessBitness() == Bitness::BITNESS_64) return Bitness::BITNESS_64;

		return GetOSBitnessInternal();
	}

	Bitness GetCPUBitness(CPUInfo info)
	{
		if (GetOSBitness() == Bitness::BITNESS_64) return Bitness::BITNESS_64;

		return info.bitness;
	}

	std::vector<uint8_t> GetMachineUuid()
	{
		return HexStringToByteArray(GetMachineUuidString(), 16);
	}

	std::optional<std::string> GetEnvironmentVariable(std::string name)
	{
		auto ret = std::getenv(name.c_str());
		return ret ? ret : std::optional<std::string>();
	}

	StreamingService GetStreamingService()
	{
		std::optional<std::string> service = GetEnvironmentVariable("streamingservice");
		if (service.has_value())
		{
			std::string s = tolower(*service);
			if (s == "intel")
				return StreamingService::INTEL_STREAM;
			return StreamingService::UNKNOWN;
		}

		return StreamingService::NONE;
	}

	TimeStamp GetCurrentTime()
	{

		time_t rawtime;
		time(&rawtime);
		TimeStamp time{ {0} };

#if _WIN32
		localtime_s(&time, &rawtime);
#else
		localtime_r(&rawtime, &time);
#endif

		return time;
	}

	constexpr const char* BitnessToString(Bitness bitness)
	{
		switch (bitness)
		{
		case Bitness::BITNESS_64:
			return "x64";
		case Bitness::BITNESS_32:
			return "x32";
		case Bitness::BITNESS_UNKNOWN:
		default:
			return "UNKNOWN";
		}
	}

	constexpr const char* CPUArchitectureToString(CPUArchitecture architecture)
	{
		switch (architecture)
		{
		case CPUArchitecture::X86:
			return "x86";
		case CPUArchitecture::X86_64:
			return "x86_64";
		case CPUArchitecture::ARM:
			return "ARM";
		case CPUArchitecture::ARM64:
			return "ARM64";
		case CPUArchitecture::UNKNOWN:
		default:
			return "UNKNOWN";
		}
	}

	constexpr const char* OSToString(OS os)
	{
		switch (os)
		{
		case OS::WINDOWS:
			return "Windows";
		case OS::MACOS:
			return "macOS";
		case OS::WINE:
			return "Wine";
		case OS::LINUX:
			return "Linux";
		case OS::UNKNOWN:
		default:
			return "UNKNOWN";
		}
	}

	constexpr const char* StreamingServiceToString(StreamingService streamingService)
	{
		switch (streamingService)
		{
		case StreamingService::NONE:
			return "NONE";
		case StreamingService::INTEL_STREAM:
			return "Intel";
		case StreamingService::UNKNOWN:
		default:
			return "UNKNOWN";
		}
	}

	constexpr const char* VulkanSupportToString(VulkanSupport support)
	{
		switch (support)
		{
		case VulkanSupport::SUPPORTED:
			return "YES";
		case VulkanSupport::UNSUPPORTED:
			return "NO";
		case VulkanSupport::UNKNOWN:
		default:
			return "UNKNOWN";
		}
	}

    constexpr const char* BatteryStatusToString(BatteryStatus status)
    {
        switch (status)
        {
        case BatteryStatus::DETECTED:
            return "YES";
        case BatteryStatus::NOT_DETECTED:
            return "NO";
        case BatteryStatus::UNKNOWN:
        default:
            return "UNKNOWN";
        }
    }

	constexpr const char* HardDriveTypeToString(HardDriveInfo::HardDriveType type)
	{
		switch (type)
		{
		case HardDriveInfo::HardDriveType::SSD:
			return "SSD";
		case HardDriveInfo::HardDriveType::HDD:
			return "HDD";
		case HardDriveInfo::HardDriveType::UNKNOWN:
		default:
			return "UNKNOWN";
		}
	}

	std::string TimestampToString(const TimeStamp& timestamp)
	{
		const int MAX_SIZE = 20;
		char time[MAX_SIZE];
		strftime(time, MAX_SIZE, "%F %T", &timestamp);
		return time;
	}

	PDMData GatherData(const std::string& applicationName, const std::string& applicationVersion)
	{
		TimeStamp timestamp = GetCurrentTime();
		CPUInfo cpuinfo = GetCPUInfo();

		std::vector<SubItem> monitors;
		for (auto& monitor : GetMonitorsInfo())
		{
			monitors.push_back
			({
				"MONITOR",
				{},
				{
					{"NAME",                monitor.name},
					{"HORIZONTAL_RES",      std::to_string(monitor.width)},
					{"VERTICAL_RES",        std::to_string(monitor.height)},
					{"BITS_PER_COLOR",      std::to_string(monitor.bitsPerColor)},
					{"REFRESH_RATE",        std::to_string(monitor.refreshRate)},
					{"DPI_SCALING_PERCENT", std::to_string(monitor.dpiScaling)},
				}
			});
		}

		std::vector<SubItem> gpus;
		for (auto& gpu : GetGPUInfo())
		{
			gpus.push_back
			({
				"GPU",
				{
					{
						"DRIVER",
						{},
						{
							{"DATE",    gpu.driverDate},
							{"VENDOR",  gpu.driverVendor},
							{"VERSION", gpu.driverVersionString},
						}
					}
				},
				{
					{"DESCRIPTION",  gpu.description},
					{"VENDOR_ID",    std::to_string(gpu.vendorID)},
					{"DEVICE_ID",    std::to_string(gpu.deviceID)},
					{"REVISION",     std::to_string(gpu.revision)},
                    {"VIDEO_MEMORY", std::to_string(gpu.memory)},
                    {"CORE_COUNT",   std::to_string(gpu.coreCount)},
				}
			});
		}

		std::vector<SubItem> networkAdapters;
		for (auto& adapter : GetNetworkAdapterInfo())
		{
			networkAdapters.push_back
			({
				"ADAPTER",
				{},
				{
					{"NAME",        adapter.name},
					{"MAC_ADDRESS", adapter.macAddressString},
					{"UUID",        adapter.uuidString},
				}
			});
		}
		
		std::vector<SubItem> hardDrives;
		for (auto& drive : GetHardDriveInfo())
		{
			hardDrives.push_back
			({
				"HARD_DRIVE",
				{},
				{
					{"NAME", drive.name},
					{"SIZE", std::to_string(drive.size)},
					{"TYPE", HardDriveTypeToString(drive.type)},
				}
			});
		}

		std::string cpuExtensions;
		for (auto& extension : cpuinfo.extensions) cpuExtensions += (cpuExtensions.empty() ? "" : " ") + extension;
		
		VulkanProperties vulkanProperties = GetVulkanProperties();

		return
		{
			{
				"DATA",
				{
					{
						{
							"APPLICATION",
							{},
							{
								{"NAME",    applicationName},
								{"VERSION", applicationVersion},
							}
						},
						{
							"PROCESS",
							{},
							{
								{"VERSION",    GetPDMVersion()},
								{"TIMESTAMP",  TimestampToString(timestamp)},
								{"BITNESS",    BitnessToString(GetProcessBitness())},
								{"IS_ROSETTA", IsRosetta() ? "YES" : "NO"},
							}
						},
						{
							"OS",
							{
								{
									"GRAPHICS_APIS",
									{},
									{
										{"METAL_SUPPORTED",        GetMetalSupported() ? "YES" : "NO"},
										{"VULKAN_SUPPORTED",       VulkanSupportToString(vulkanProperties.support)},
										{"VULKAN_HIGHEST_SUPPORT", vulkanProperties.version},
										{"D3D_HIGHEST_SUPPORT",    GetD3DHighestSupport()},
									},
								},
								{
									"WINE",
									{},
									{
										{"VERSION", GetWineVersion()},
										{"HOST_OS", GetWineHostOs()},
									},
								},
								{
									"STREAMING_SERVICE",
									{},
									{
										{"PROVIDER", StreamingServiceToString(GetStreamingService())},
									},
								},
							},
							{
								{"TYPE",              OSToString(GetOSType())},
								{"NAME",              GetOSName()},
								{"BITNESS",           BitnessToString(GetOSBitness())},
								{"MAJOR_VERSION",     GetOSMajorVersion()},
								{"MINOR_VERSION",     GetOSMinorVersion()},
								{"BUILD_NUMBER",      GetOSBuildNumber()},
								{"KERNEL_VERSION",    GetOSKernelVersion()},
								{"USERNAME",          GetUsername()},
								{"USER_LOCALE",       GetUserLocale()},
								{"IS_REMOTE_SESSION", IsRemoteSession() ? "YES" : "NO"},
							}
						},
						{
							"MACHINE",
							{
								{
									"CPU",
									{},
									{
										{"BITNESS",            BitnessToString(GetCPUBitness(cpuinfo))},
										{"LOGICAL_CORE_COUNT", std::to_string(cpuinfo.logicalCoreCount)},
										{"BRAND",              cpuinfo.brand},
										{"VENDOR",             cpuinfo.vendor},
										{"MODEL",              std::to_string(cpuinfo.model)},
										{"STEPPING",           std::to_string(cpuinfo.stepping)},
										{"FREQUENCY_MHZ",      std::to_string(cpuinfo.frequency)},
										{"ARCHITECTURE",       CPUArchitectureToString(cpuinfo.architecture)},
										{"EXTENSIONS",         cpuExtensions },
									}
								},
								{
									"VM",
									{},
									{
										{"IS_SUSPECTED_VM",         IsSuspectedVM() ? "YES" : "NO"},
										{"HAS_HYPERVISOR_BIT",      HasHypervisorBit() ? "YES" : "NO"},
										{"HYPERVISOR_NAME",         GetHypervisorName()},
										{"IS_HYPERV_GUEST_OS",      IsHyperVGuestOS() ? "YES" : "NO"},
										{"HAS_VM_EXECUTION_TIMING", HasVMExecutionTiming() ? "YES" : "NO"},
									}
								},
								{
									"MONITORS",
									monitors,
									{},
								},
								{
									"GPUS",
									gpus,
									{},
								},
								{
									"NETWORK_ADAPTERS",
									networkAdapters,
									{},
								},
								{
									"HARD_DRIVES",
									hardDrives,
									{},
								},
							},
							{
								{"MODEL",         GetHardwareModel()},
								{"NAME",          GetMachineName()},
								{"UUID",          GetMachineUuidString()},
								{"TOTAL_MEMORY",  std::to_string(GetTotalMemory())},
								{"MONITOR_COUNT", std::to_string(GetMonitorCount())},
								{"HAS_BATTERY",   BatteryStatusToString(GetBatteryStatus())},
							}
						},
					},
				},
				{}
			},
			timestamp
		};
	}
}
