// Copyright © 2026 CCP ehf.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// CPU information that has to come from the OS rather than the ISA.
// Used by architectures whose identification registers are not readable from user mode (ARM64).
// Implemented per platform in src/macos/cpu_macos.cpp and src/linux/cpu_linux.cpp.

namespace PDM::Platform
{
    std::string CPUVendor();
    std::string CPUBrand();
    int32_t CPUModel();
    int32_t CPUStepping();
    std::vector<std::string> CPUExtensions();
    bool HypervisorPresent();
    std::string HypervisorName();
}
