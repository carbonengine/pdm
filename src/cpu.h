// Copyright © 2026 CCP ehf.

#pragma once

#include <string>
#include <vector>

#include "../include/pdm_data.h"

// CPU queries are split into three layers:
//  - src/arch/     Answers the ISA gives directly (x86 cpuid, timers). No OS headers.
//  - platform_cpu.h Answers only the OS can give (ARM64 identification registers are privileged).
//  - src/cpu_vm.cpp Policy built on top of the primitives, written once.
// The VM primitives (HasHypervisorBit, GetHypervisorName, IsHyperVGuestOS, GetTimingCycles)
// are declared in include/pdm.h and implemented by the arch layer.

namespace PDM
{
    constexpr auto HYPER_V_NAME = "Microsoft Hv";

    constexpr CPUArchitecture GetCPUArchitecture()
    {
#if _M_IX86 || __i386
		return CPUArchitecture::X86;
#elif _M_AMD64 || __amd64
		return CPUArchitecture::X86_64;
#elif _M_ARM || __arm__
		return CPUArchitecture::ARM;
#elif __aarch64__
		return CPUArchitecture::ARM64;
#else
		return CPUArchitecture::UNKNOWN;
#error Unknown CPU architecture
#endif
    }

    // Architecture independent CPU identification, implemented in src/arch/
    class CPUID
    {
    public:
        static Bitness GetBitness();
        static std::string GetBrand();
        static std::string GetVendor();
        static int32_t GetModel();
        static int32_t GetStepping();
    };

    std::vector<std::string> GetCPUExtensions();
}
