// Copyright © 2026 CCP ehf.

#if __APPLE__

#include "../platform_cpu.h"
#include "../utilities.h"

namespace PDM::Platform
{
    std::string CPUVendor()
    {
        return "Apple";
    }

    std::string CPUBrand()
    {
        return GetOSString("machdep.cpu.brand_string");
    }

    int32_t CPUModel()
    {
        return GetOSInteger("hw.cpufamily");
    }

    int32_t CPUStepping()
    {
        return GetOSInteger("hw.cpusubfamily");
    }

    std::vector<std::string> CPUExtensions()
    {
        // TODO: Query extensions
        return {"AES", "CRC32", "PMULL", "SHA1", "SHA2"};
    }

    bool HypervisorPresent()
    {
        return GetOSInteger("kern.hv_vmm_present");
    }

    std::string HypervisorName()
    {
        // Model name will always contain 'mac' on original hardware
        std::string hw = GetOSString("hw.model");
        return tolower(hw).find("mac") == std::string::npos ? hw : "";
    }
}

#endif
