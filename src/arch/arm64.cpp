// Copyright © 2026 CCP ehf.

#if __aarch64__

#include "../cpu.h"
#include "../platform_cpu.h"
#include "../../include/pdm.h"

namespace PDM
{
    // ARM64 identification registers (MIDR_EL1, ID_AA64*_EL1) are privileged, so most answers come from the OS

    Bitness CPUID::GetBitness()
    {
        return Bitness::BITNESS_64;
    }

    std::string CPUID::GetBrand()
    {
        return Platform::CPUBrand();
    }

    std::string CPUID::GetVendor()
    {
        return Platform::CPUVendor();
    }

    int32_t CPUID::GetModel()
    {
        return Platform::CPUModel();
    }

    int32_t CPUID::GetStepping()
    {
        return Platform::CPUStepping();
    }

    std::vector<std::string> GetCPUExtensions()
    {
        return Platform::CPUExtensions();
    }

    bool HasHypervisorBit()
    {
        return Platform::HypervisorPresent();
    }

    std::string GetHypervisorName()
    {
        return Platform::HypervisorName();
    }

    bool IsHyperVGuestOS()
    {
        return false;
    }

    size_t GetTimingCycles()
    {
        volatile size_t time1 = 0;
        volatile size_t time2 = 0;

        asm volatile("mrs %0, cntvct_el0" : "=r" (time1));
        asm volatile("mrs %0, cntvct_el0" : "=r" (time2));

        return time2 - time1;
    }
}

#endif
