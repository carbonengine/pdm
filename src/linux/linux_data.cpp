// Copyright © 2026 CCP ehf.

#if __linux__

#include "../../include/pdm.h"
#include "../defines.h"
#include "../utilities.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#include <dlfcn.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace PDM
{
    namespace
    {
        std::string ReadFile(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            std::stringstream contents;
            contents << file.rdbuf();
            return contents.str();
        }

        // sysfs and procfs values are a single line with a trailing newline
        std::string ReadValue(const fs::path& path)
        {
            return getTrimmed(ReadFile(path));
        }

        uint64_t ReadNumber(const fs::path& path)
        {
            try
            {
                return std::stoull(ReadValue(path), nullptr, 0);
            }
            catch (std::exception&)
            {
                return 0;
            }
        }

        // Firmware strings (DMI, EDID) are nominally ASCII but not guaranteed to be, and the output must be valid UTF-8
        std::string ToPrintableASCII(std::string str)
        {
            for (char& c : str)
            {
                if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) c = '?';
            }
            return str;
        }

        std::vector<std::string> Split(const std::string& str, char delimiter)
        {
            std::vector<std::string> parts;
            std::stringstream stream(str);
            for (std::string part; std::getline(stream, part, delimiter);) parts.push_back(part);
            return parts;
        }

        // Sorted, so that repeated runs report devices in the same order
        std::vector<fs::path> ListDirectory(const fs::path& path)
        {
            std::vector<fs::path> entries;
            std::error_code error;
            for (fs::directory_iterator it(path, error), end; !error && it != end; it.increment(error))
                entries.push_back(it->path());

            std::sort(entries.begin(), entries.end());
            return entries;
        }

        std::string GetUnameRelease()
        {
            struct utsname un;
            return uname(&un) == 0 ? un.release : "";
        }

        std::map<std::string, std::string> ReadOSRelease()
        {
            std::string contents = ReadFile("/etc/os-release");
            if (contents.empty()) contents = ReadFile("/usr/lib/os-release");

            std::map<std::string, std::string> fields;
            for (const auto& line : Split(contents, '\n'))
            {
                auto equals = line.find('=');
                if (equals == std::string::npos || line[0] == '#') continue;

                std::string value = getTrimmed(line.substr(equals + 1));
                if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
                    value = value.substr(1, value.size() - 2);

                fields[line.substr(0, equals)] = value;
            }

            return fields;
        }

        std::string GetOSReleaseField(const char* name)
        {
            static const auto fields = ReadOSRelease();
            auto it = fields.find(name);
            return it == fields.end() ? "" : it->second;
        }

        // VERSION_ID is e.g. "24.04" on Ubuntu, and absent on rolling releases
        std::string GetOSVersionPart(size_t part)
        {
            auto parts = Split(GetOSReleaseField("VERSION_ID"), '.');
            return parts.size() > part ? parts[part] : "";
        }

        std::string ReadDMI(const char* field)
        {
            return ToPrintableASCII(ReadValue(fs::path("/sys/class/dmi/id") / field));
        }

        struct EDIDInfo
        {
            std::string name;
            uint32_t width{};
            uint32_t height{};
            uint32_t bitsPerColor{};
            uint32_t refreshRate{};
        };

        // Reads the preferred (native) mode. The current mode is only known to the display server.
        EDIDInfo ParseEDID(const std::string& edid)
        {
            EDIDInfo info;

            const auto* data = reinterpret_cast<const uint8_t*>(edid.data());
            const uint8_t header[] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
            if (edid.size() < 128 || !std::equal(std::begin(header), std::end(header), data)) return info;

            // EDID 1.4 digital inputs declare the color depth
            if (data[19] >= 4 && (data[20] & 0x80))
            {
                uint32_t depth = (data[20] >> 4) & 0x7;
                if (depth >= 1 && depth <= 6) info.bitsPerColor = 4 + depth * 2;
            }

            bool havePreferredTiming = false;
            for (size_t offset = 54; offset < 126; offset += 18)
            {
                const uint8_t* descriptor = data + offset;
                uint32_t pixelClock = descriptor[0] | (descriptor[1] << 8);

                if (pixelClock != 0)
                {
                    // The first detailed timing descriptor is the preferred mode
                    if (havePreferredTiming) continue;
                    havePreferredTiming = true;

                    uint32_t hActive = descriptor[2] | ((descriptor[4] & 0xf0) << 4);
                    uint32_t hBlank  = descriptor[3] | ((descriptor[4] & 0x0f) << 8);
                    uint32_t vActive = descriptor[5] | ((descriptor[7] & 0xf0) << 4);
                    uint32_t vBlank  = descriptor[6] | ((descriptor[7] & 0x0f) << 8);

                    info.width = hActive;
                    info.height = vActive;

                    // Pixel clock is in units of 10 kHz
                    uint64_t pixelsPerFrame = static_cast<uint64_t>(hActive + hBlank) * (vActive + vBlank);
                    if (pixelsPerFrame) info.refreshRate = static_cast<uint32_t>(std::lround(pixelClock * 10000.0 / pixelsPerFrame));
                }
                else if (descriptor[3] == 0xfc)
                {
                    // Monitor name, up to 13 characters terminated by a newline
                    std::string name(reinterpret_cast<const char*>(descriptor + 5), 13);
                    info.name = ToPrintableASCII(getTrimmed(name.substr(0, name.find('\n'))));
                }
            }

            return info;
        }

        // DRM connectors are named after their card, e.g. card0-HDMI-A-1
        std::vector<fs::path> GetConnectedDisplays()
        {
            std::vector<fs::path> connectors;
            for (const auto& entry : ListDirectory("/sys/class/drm"))
            {
                std::string name = entry.filename().string();
                if (name.rfind("card", 0) != 0 || name.find('-') == std::string::npos) continue;

                if (ReadValue(entry / "status") == "connected") connectors.push_back(entry);
            }

            return connectors;
        }

        // The cards themselves are card0, card1, ...
        bool IsDRMCard(const std::string& name)
        {
            return name.size() > 4 && name.rfind("card", 0) == 0 &&
                std::all_of(name.begin() + 4, name.end(), [](char c) { return c >= '0' && c <= '9'; });
        }

        // Looks up "<vendor> <device>" in the pci.ids database shipped by most distributions
        std::string LookupPCIName(uint32_t vendorID, uint32_t deviceID)
        {
            char vendorHex[8], deviceHex[8];
            snprintf(vendorHex, sizeof(vendorHex), "%04x", vendorID);
            snprintf(deviceHex, sizeof(deviceHex), "%04x", deviceID);

            for (auto path : { "/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids" })
            {
                std::ifstream file(path);
                if (!file) continue;

                // Vendors are unindented, their devices are indented by one tab
                std::string vendorName;
                for (std::string line; std::getline(file, line);)
                {
                    if (line.empty() || line[0] == '#') continue;

                    if (line[0] != '\t')
                    {
                        if (!vendorName.empty()) break;
                        if (line.size() > 6 && line.compare(0, 4, vendorHex) == 0) vendorName = line.substr(6);
                    }
                    else if (!vendorName.empty() && line.size() > 7 && line[1] != '\t' && line.compare(1, 4, deviceHex) == 0)
                    {
                        return vendorName + " " + line.substr(7);
                    }
                }

                return vendorName;
            }

            return "";
        }

        // Minimal subset of vulkan_core.h, so that neither the Vulkan headers nor the loader are build dependencies
        using VkInstance = struct VkInstance_T*;
        using VkPhysicalDevice = struct VkPhysicalDevice_T*;
        using VkResult = int32_t;

        struct VkApplicationInfo
        {
            int32_t sType;
            const void* pNext;
            const char* pApplicationName;
            uint32_t applicationVersion;
            const char* pEngineName;
            uint32_t engineVersion;
            uint32_t apiVersion;
        };

        struct VkInstanceCreateInfo
        {
            int32_t sType;
            const void* pNext;
            uint32_t flags;
            const VkApplicationInfo* pApplicationInfo;
            uint32_t enabledLayerCount;
            const char* const* ppEnabledLayerNames;
            uint32_t enabledExtensionCount;
            const char* const* ppEnabledExtensionNames;
        };

        using PFN_vkVoidFunction = void (*)();
        using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction (*)(VkInstance, const char*);
        using PFN_vkEnumerateInstanceVersion = VkResult (*)(uint32_t*);
        using PFN_vkCreateInstance = VkResult (*)(const VkInstanceCreateInfo*, const void*, VkInstance*);
        using PFN_vkEnumeratePhysicalDevices = VkResult (*)(VkInstance, uint32_t*, VkPhysicalDevice*);
        using PFN_vkDestroyInstance = void (*)(VkInstance, const void*);

        constexpr VkResult VK_SUCCESS = 0;
        constexpr int32_t VK_STRUCTURE_TYPE_APPLICATION_INFO = 0;
        constexpr int32_t VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1;
        constexpr uint32_t VK_API_VERSION_1_0 = 1u << 22;

        std::string VulkanVersionToString(uint32_t version)
        {
            return std::to_string((version >> 22) & 0x7f) + "." + std::to_string((version >> 12) & 0x3ff) + "." + std::to_string(version & 0xfff);
        }

        VulkanProperties QueryVulkanProperties()
        {
            // Kept loaded for the lifetime of the process, unloading Vulkan drivers is not reliably safe
            void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            if (!loader) return { VulkanSupport::UNSUPPORTED, {} };

            auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr"));
            if (!getInstanceProcAddr) return { VulkanSupport::UNSUPPORTED, {} };

            // vkEnumerateInstanceVersion only exists from Vulkan 1.1
            uint32_t instanceVersion = VK_API_VERSION_1_0;
            auto enumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(getInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
            if (enumerateInstanceVersion) enumerateInstanceVersion(&instanceVersion);
            std::string version = VulkanVersionToString(instanceVersion);

            auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(getInstanceProcAddr(nullptr, "vkCreateInstance"));
            if (!createInstance) return { VulkanSupport::UNSUPPORTED, version };

            VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "PDM", 0, nullptr, 0, VK_API_VERSION_1_0 };
            VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &appInfo, 0, nullptr, 0, nullptr };
            VkInstance instance = nullptr;
            if (createInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) return { VulkanSupport::UNSUPPORTED, version };

            // The loader is present, but Vulkan is only usable if a driver exposes a device
            uint32_t deviceCount = 0;
            auto enumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(getInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
            if (enumeratePhysicalDevices) enumeratePhysicalDevices(instance, &deviceCount, nullptr);

            auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(getInstanceProcAddr(instance, "vkDestroyInstance"));
            if (destroyInstance) destroyInstance(instance, nullptr);

            return { deviceCount > 0 ? VulkanSupport::SUPPORTED : VulkanSupport::UNSUPPORTED, version };
        }
    }

    Bitness GetOSBitnessInternal()
    {
        struct utsname un;
        if (uname(&un) != 0) return Bitness::BITNESS_UNKNOWN;

        // e.g. x86_64, aarch64, riscv64 and i686, armv7l
        std::string machine{ un.machine };
        if (machine.find("64") != std::string::npos) return Bitness::BITNESS_64;
        if ((machine.size() == 4 && machine[0] == 'i' && machine.compare(2, 2, "86") == 0) || machine.rfind("arm", 0) == 0)
            return Bitness::BITNESS_32;

        return Bitness::BITNESS_UNKNOWN;
    }

    uint32_t GetCPUFrequency()
    {
        // Maximum across all cores, so heterogeneous (big.LITTLE) systems report their fastest cores
        uint64_t maxKHz = 0;
        for (const auto& cpu : ListDirectory("/sys/devices/system/cpu"))
            maxKHz = std::max(maxKHz, ReadNumber(cpu / "cpufreq" / "cpuinfo_max_freq"));

        if (maxKHz) return static_cast<uint32_t>(maxKHz / 1000);

        // Without cpufreq (common in VMs) x86 kernels still report the current frequency
        std::ifstream cpuinfo("/proc/cpuinfo");
        for (std::string line; std::getline(cpuinfo, line);)
        {
            if (line.rfind("cpu MHz", 0) != 0) continue;

            auto colon = line.find(':');
            return colon == std::string::npos ? 0 : static_cast<uint32_t>(std::atof(line.c_str() + colon + 1));
        }

        return 0;
    }

    OS GetOSType()
    {
        return OS::LINUX;
    }

    std::string GetOSName()
    {
        std::string name = GetOSReleaseField("PRETTY_NAME");
        return name.empty() ? GetOSReleaseField("NAME") : name;
    }

    std::string GetOSMajorVersion()
    {
        return GetOSVersionPart(0);
    }

    std::string GetOSMinorVersion()
    {
        return GetOSVersionPart(1);
    }

    std::string GetOSBuildNumber()
    {
        std::string build = GetOSVersionPart(2);
        return build.empty() ? GetOSReleaseField("BUILD_ID") : build;
    }

    std::string GetOSKernelVersion()
    {
        return GetUnameRelease();
    }

    std::string GetHardwareModel()
    {
        // Matches the Windows format, "<manufacturer> [<product>]", skipping unfilled OEM placeholders
        auto isPlaceholder = [](const std::string& str)
        {
            return str.empty() || str == "System manufacturer" || str == "System Product Name" ||
                str == "To Be Filled By O.E.M." || str == "Default string";
        };

        std::string manu = ReadDMI("sys_vendor");
        if (isPlaceholder(manu)) manu = "";

        std::string prod = ReadDMI("product_name");
        if (isPlaceholder(prod)) prod = "";

        if (!manu.empty() && !prod.empty()) return manu + " [" + prod + "]";
        if (!manu.empty() || !prod.empty()) return manu + prod;

        // Device tree systems without DMI, e.g. "Raspberry Pi 4 Model B Rev 1.4"
        return ToPrintableASCII(ReadValue("/proc/device-tree/model"));
    }

    std::string GetMachineName()
    {
        char hostname[256] = { 0 };
        return gethostname(hostname, sizeof(hostname) - 1) == 0 ? hostname : "";
    }

    std::string GetUsername()
    {
        if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_name) return pw->pw_name;

        const char* user = std::getenv("USER");
        return user ? user : "";
    }

    std::string GetUserLocale()
    {
        // Read from the environment rather than setlocale(), which would change the host application's locale
        for (auto name : { "LC_ALL", "LANG" })
        {
            const char* value = std::getenv(name);
            if (value && *value) return value;
        }

        return "";
    }

    uint32_t GetMonitorCount()
    {
        return static_cast<uint32_t>(GetConnectedDisplays().size());
    }

    std::vector<MonitorInfo> GetMonitorsInfo()
    {
        std::vector<MonitorInfo> monitors;

        for (const auto& connector : GetConnectedDisplays())
        {
            EDIDInfo edid = ParseEDID(ReadFile(connector / "edid"));

            // Without EDID (e.g. some virtual displays) fall back to the connector name and preferred mode, e.g. "1920x1080"
            if (edid.name.empty())
            {
                std::string name = connector.filename().string();
                edid.name = name.substr(name.find('-') + 1);
            }
            if (!edid.width)
            {
                std::ifstream modes(connector / "modes");
                std::string mode;
                std::getline(modes, mode);
                if (auto x = mode.find('x'); x != std::string::npos)
                {
                    edid.width = std::atoi(mode.c_str());
                    edid.height = std::atoi(mode.c_str() + x + 1);
                }
            }

            // DPI scaling is a display server setting, and unknown here
            monitors.push_back({ edid.name, edid.width, edid.height, edid.bitsPerColor, edid.refreshRate, 0 });
        }

        return monitors;
    }

    std::vector<GPUInfo> GetGPUInfo()
    {
        std::vector<GPUInfo> gpus;

        for (const auto& card : ListDirectory("/sys/class/drm"))
        {
            if (!IsDRMCard(card.filename().string())) continue;

            fs::path device = card / "device";
            std::error_code error;
            std::string driver = fs::read_symlink(device / "driver", error).filename().string();

            // The firmware framebuffer, not a GPU. It is replaced once the real driver loads.
            if (driver == "simpledrm") continue;

            GPUInfo gpu{};
            gpu.vendorID = static_cast<uint32_t>(ReadNumber(device / "vendor"));
            gpu.deviceID = static_cast<uint32_t>(ReadNumber(device / "device"));
            gpu.revision = static_cast<uint32_t>(ReadNumber(device / "revision"));

            // Only amdgpu reports video memory in sysfs
            gpu.memory = ReadNumber(device / "mem_info_vram_total");

            // PCI GPUs are looked up by ID, SoC GPUs identify themselves through the device tree, e.g. "arm,mali-bifrost"
            if (gpu.vendorID) gpu.description = LookupPCIName(gpu.vendorID, gpu.deviceID);
            if (gpu.description.empty()) gpu.description = ToPrintableASCII(ReadFile(device / "of_node" / "compatible").c_str());
            if (gpu.description.empty()) gpu.description = driver;

            // The kernel driver identifies the provider (e.g. nvidia, nouveau, amdgpu). In-tree drivers are versioned with the kernel.
            gpu.driverVendor = driver;
            gpu.driverVersionString = ReadValue(fs::path("/sys/module") / driver / "version");
            if (gpu.driverVersionString.empty() && !driver.empty()) gpu.driverVersionString = GetUnameRelease();

            gpus.push_back(gpu);
        }

        return gpus;
    }

    std::vector<NetworkAdapterInfo> GetNetworkAdapterInfo()
    {
        std::vector<NetworkAdapterInfo> adapters;

        for (const auto& interface : ListDirectory("/sys/class/net"))
        {
            // Only physical adapters have a backing device, which skips loopback, bridges, VPNs and container interfaces
            std::error_code error;
            if (!fs::exists(interface / "device", error)) continue;

            std::string mac = toupper(ReadValue(interface / "address"));
            auto macAddress = HexStringToByteArray(mac, 6);
            if (macAddress.empty() || std::all_of(macAddress.begin(), macAddress.end(), [](uint8_t b) { return b == 0; })) continue;

            adapters.push_back({ interface.filename().string(), mac, {}, macAddress, {} });
        }

        return adapters;
    }

    std::vector<HardDriveInfo> GetHardDriveInfo()
    {
        std::vector<HardDriveInfo> drives;

        for (const auto& block : ListDirectory("/sys/block"))
        {
            // Only real disks have a backing device, which skips loop, ram, zram, device mapper and md devices
            std::error_code error;
            if (!fs::exists(block / "device", error)) continue;

            // We don't care about USB sticks and such
            if (ReadValue(block / "removable") == "1") continue;
            if (fs::canonical(block, error).string().find("/usb") != std::string::npos) continue;

            std::string name = ReadValue(block / "device" / "model");
            if (name.empty()) name = ReadValue(block / "device" / "name");
            if (name.empty()) name = block.filename().string();

            std::string rotational = ReadValue(block / "queue" / "rotational");
            auto type = rotational == "1" ? HardDriveInfo::HardDriveType::HDD :
                        rotational == "0" ? HardDriveInfo::HardDriveType::SSD :
                                            HardDriveInfo::HardDriveType::UNKNOWN;

            // Always in 512 byte sectors, regardless of the device's sector size
            uint64_t size = ReadNumber(block / "size") * 512;

            drives.push_back({ ToPrintableASCII(name), type, size });
        }

        return drives;
    }

    uint64_t GetTotalMemory()
    {
        long pages = sysconf(_SC_PHYS_PAGES);
        long pageSize = sysconf(_SC_PAGE_SIZE);
        return pages > 0 && pageSize > 0 ? static_cast<uint64_t>(pages) * pageSize : 0;
    }

    bool IsRemoteSession()
    {
        for (auto name : { "SSH_CONNECTION", "SSH_CLIENT", "SSH_TTY" })
        {
            if (std::getenv(name)) return true;
        }

        return false;
    }

    BatteryStatus GetBatteryStatus()
    {
        std::error_code error;
        if (!fs::is_directory("/sys/class/power_supply", error)) return BatteryStatus::UNKNOWN;

        for (const auto& supply : ListDirectory("/sys/class/power_supply"))
        {
            // Peripherals such as wireless mice report their batteries with a "Device" scope
            if (ReadValue(supply / "type") == "Battery" && ReadValue(supply / "scope") != "Device")
                return BatteryStatus::DETECTED;
        }

        return BatteryStatus::NOT_DETECTED;
    }

    std::string GetMachineUuidString()
    {
        // The DMI product UUID is only readable by root, so use the OS installation's machine ID instead
        std::string id = ReadValue("/etc/machine-id");
        if (id.empty()) id = ReadValue("/var/lib/dbus/machine-id");
        if (id.size() != 32) return "";

        // Formatted like the other platforms, e.g. 4C4C4544-0038-4B10-8039-B7C04F4E4E32
        id = toupper(id);
        return id.substr(0, 8) + "-" + id.substr(8, 4) + "-" + id.substr(12, 4) + "-" + id.substr(16, 4) + "-" + id.substr(20);
    }

    VulkanProperties GetVulkanProperties()
    {
        static const VulkanProperties properties = QueryVulkanProperties();
        return properties;
    }

    bool GetMetalSupported()
    {
        return false;
    }

    bool IsRosetta()
    {
        return false;
    }
}

#endif
