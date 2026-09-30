// Copyright © 2026 CCP ehf.

// Only ARM64 needs the platform layer on Linux, x86_64 reads everything via cpuid
#if __linux__ && __aarch64__

#include "../platform_cpu.h"
#include "../defines.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace PDM::Platform
{
    namespace
    {
        using CPUInfoBlock = std::map<std::string, std::string>;

        std::string ReadFile(const std::string& path)
        {
            std::ifstream file(path);
            std::stringstream contents;
            contents << file.rdbuf();
            return contents.str();
        }

        // One block per logical processor, keyed by field name, e.g. "CPU implementer" -> "0x41"
        std::vector<CPUInfoBlock> ReadProcCPUInfo()
        {
            std::vector<CPUInfoBlock> blocks(1);
            std::ifstream file("/proc/cpuinfo");
            std::string line;
            while (std::getline(file, line))
            {
                auto colon = line.find(':');
                if (colon == std::string::npos)
                {
                    if (!blocks.back().empty()) blocks.emplace_back();
                    continue;
                }
                blocks.back()[getTrimmed(line.substr(0, colon))] = getTrimmed(line.substr(colon + 1));
            }
            if (blocks.back().empty()) blocks.pop_back();
            return blocks;
        }

        const std::vector<CPUInfoBlock>& ProcCPUInfo()
        {
            static const std::vector<CPUInfoBlock> blocks = ReadProcCPUInfo();
            return blocks;
        }

        const CPUInfoBlock& FirstCPU()
        {
            static const CPUInfoBlock empty;
            return ProcCPUInfo().empty() ? empty : ProcCPUInfo().front();
        }

        // The kernel reports the MIDR_EL1 fields as numbers, e.g. "CPU part : 0xd0b"
        uint32_t GetNumericField(const CPUInfoBlock& cpu, const char* name)
        {
            auto it = cpu.find(name);
            if (it == cpu.end()) return 0;

            try
            {
                return std::stoul(it->second, nullptr, 0);
            }
            catch (std::exception&)
            {
                return 0;
            }
        }

        std::string ToHex(uint32_t value)
        {
            std::ostringstream str;
            str << "0x" << std::hex << value;
            return str.str();
        }

        std::string ImplementerName(uint32_t implementer)
        {
            switch (implementer)
            {
            case 0x41: return "ARM";
            case 0x42: return "Broadcom";
            case 0x43: return "Cavium";
            case 0x46: return "Fujitsu";
            case 0x48: return "HiSilicon";
            case 0x4e: return "NVIDIA";
            case 0x50: return "APM";
            case 0x51: return "Qualcomm";
            case 0x53: return "Samsung";
            case 0x56: return "Marvell";
            case 0x61: return "Apple";
            case 0x6d: return "Microsoft";
            case 0x70: return "Phytium";
            case 0xc0: return "Ampere";
            default:   return "";
            }
        }

        // Only ARM's own cores are named, other implementers fall back to the part number
        std::string PartName(uint32_t implementer, uint32_t part)
        {
            if (implementer == 0x41)
            {
                switch (part)
                {
                case 0xd03: return "Cortex-A53";
                case 0xd04: return "Cortex-A35";
                case 0xd05: return "Cortex-A55";
                case 0xd07: return "Cortex-A57";
                case 0xd08: return "Cortex-A72";
                case 0xd09: return "Cortex-A73";
                case 0xd0a: return "Cortex-A75";
                case 0xd0b: return "Cortex-A76";
                case 0xd0c: return "Neoverse-N1";
                case 0xd0d: return "Cortex-A77";
                case 0xd40: return "Neoverse-V1";
                case 0xd41: return "Cortex-A78";
                case 0xd44: return "Cortex-X1";
                case 0xd46: return "Cortex-A510";
                case 0xd47: return "Cortex-A710";
                case 0xd48: return "Cortex-X2";
                case 0xd49: return "Neoverse-N2";
                case 0xd4d: return "Cortex-A715";
                case 0xd4e: return "Cortex-X3";
                case 0xd4f: return "Neoverse-V2";
                case 0xd80: return "Cortex-A520";
                case 0xd81: return "Cortex-A720";
                case 0xd82: return "Cortex-X4";
                case 0xd84: return "Neoverse-V3";
                case 0xd8e: return "Neoverse-N3";
                }
            }

            return ToHex(part);
        }

        bool StartsWith(const std::string& str, const std::string& prefix)
        {
            return str.compare(0, prefix.size(), prefix) == 0;
        }

        bool EndsWith(const std::string& str, const std::string& suffix)
        {
            return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::string ReadDMI(const char* field)
        {
            return getTrimmed(ReadFile(std::string("/sys/class/dmi/id/") + field));
        }

        // Virtual platforms as they identify themselves in DMI, following systemd-detect-virt
        std::string GetDMIHypervisorName()
        {
            const char* fields[] = { "sys_vendor", "product_name", "board_vendor", "bios_vendor" };
            const char* vendors[] =
            {
                "KVM", "QEMU", "Amazon EC2", "Google Compute Engine", "VMware", "innotek GmbH", "VirtualBox",
                "Parallels", "Xen", "Bochs", "BHYVE", "Hyper-V", "Apple Virtualization", "OpenStack", "KubeVirt",
            };

            for (auto field : fields)
            {
                std::string value = ReadDMI(field);
                for (auto vendor : vendors)
                {
                    if (!StartsWith(value, vendor)) continue;

                    // EC2 bare metal instances report the same vendor as EC2 VMs
                    if (value == "Amazon EC2" && EndsWith(ReadDMI("product_name"), ".metal")) continue;

                    return value;
                }
            }

            // Hyper-V guests report Microsoft as the vendor, which Microsoft's own hardware does too
            std::string sysVendor = ReadDMI("sys_vendor");
            std::string productName = ReadDMI("product_name");
            if (sysVendor == "Microsoft Corporation" && productName == "Virtual Machine")
                return sysVendor + " " + productName;

            return "";
        }
    }

    std::string CPUVendor()
    {
        auto it = FirstCPU().find("CPU implementer");
        if (it == FirstCPU().end()) return "";

        std::string name = ImplementerName(GetNumericField(FirstCPU(), "CPU implementer"));
        return name.empty() ? it->second : name;
    }

    std::string CPUBrand()
    {
        // ARM64 kernels usually only expose the MIDR_EL1 fields, but some report a model name
        auto it = FirstCPU().find("model name");
        if (it != FirstCPU().end()) return it->second;

        // Heterogeneous (big.LITTLE) systems list each core type, e.g. "ARM Cortex-A76 / Cortex-A55"
        std::vector<std::string> parts;
        for (const auto& cpu : ProcCPUInfo())
        {
            // Hypervisors may mask the part number, e.g. Apple's Virtualization framework reports 0
            uint32_t partNumber = GetNumericField(cpu, "CPU part");
            if (partNumber == 0) continue;

            std::string part = PartName(GetNumericField(cpu, "CPU implementer"), partNumber);
            if (std::find(parts.begin(), parts.end(), part) == parts.end()) parts.push_back(part);
        }

        std::string brand = CPUVendor();
        for (size_t i = 0; i < parts.size(); i++)
            brand += (i == 0 ? " " : " / ") + parts[i];

        return brand;
    }

    int32_t CPUModel()
    {
        return GetNumericField(FirstCPU(), "CPU part");
    }

    int32_t CPUStepping()
    {
        // Combines ARM's rNpM revision scheme, e.g. r1p2 -> 0x12
        return (GetNumericField(FirstCPU(), "CPU variant") << 4) | GetNumericField(FirstCPU(), "CPU revision");
    }

    std::vector<std::string> CPUExtensions()
    {
        std::vector<std::string> extensions;

        auto it = FirstCPU().find("Features");
        if (it == FirstCPU().end()) return extensions;

        // The kernel's names for the HWCAP bits, which match the names used on macOS
        std::istringstream features(it->second);
        std::string feature;
        while (features >> feature)
        {
            // Not ISA extensions: the timer event stream, and the kernel emulating ID register reads
            if (feature == "evtstrm" || feature == "cpuid") continue;

            extensions.push_back(toupper(feature));
        }

        std::sort(extensions.begin(), extensions.end());
        return extensions;
    }

    bool HypervisorPresent()
    {
        // No equivalent of the x86 cpuid hypervisor bit, so presence is inferred from the name
        return !HypervisorName().empty();
    }

    std::string HypervisorName()
    {
        static const std::string name = []() -> std::string
        {
            // Xen exposes itself directly
            std::string type = getTrimmed(ReadFile("/sys/hypervisor/type"));
            if (!type.empty()) return type;

            // Device tree booted guests, e.g. "xen,xen". The first entry of the NUL separated list is the most specific.
            std::string compatible = ReadFile("/proc/device-tree/hypervisor/compatible");
            if (!compatible.empty()) return compatible.c_str();

            // QEMU's generic virt machine, when not booted via UEFI
            if (ReadFile("/proc/device-tree/compatible").find("linux,dummy-virt") != std::string::npos) return "QEMU";

            // ACPI booted guests describe the virtual platform in DMI
            return GetDMIHypervisorName();
        }();

        return name;
    }
}

#endif
