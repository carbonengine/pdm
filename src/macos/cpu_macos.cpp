// Copyright © 2026 CCP ehf.

#if __APPLE__

#include "../platform_cpu.h"
#include "../utilities.h"

#include <algorithm>

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
        // Only the sysctls Apple documents, see "Determining Instruction Set Characteristics":
        // https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_instruction_set_characteristics
        // Reported under the Linux kernel's HWCAP names where one exists, e.g. FEAT_SHA256 -> SHA2, otherwise in the same style.
        // Sysctls the running macOS doesn't know read as 0, so newer features are simply absent on older releases.
        // Some features were renamed, so the legacy sysctl is checked too when the current one isn't set.
        struct Feature
        {
            const char* name;
            const char* sysctl;
            const char* legacySysctl = nullptr;
        };

        static const Feature features[] =
        {
            { "FP",         "hw.optional.floatingpoint" },
            { "ASIMD",      "hw.optional.AdvSIMD",           "hw.optional.neon" },
            { "ASIMDHPFPCVT", "hw.optional.AdvSIMD_HPFPCvt", "hw.optional.neon_hpfp" },
            { "CRC32",      "hw.optional.armv8_crc32" },
            { "AES",        "hw.optional.arm.FEAT_AES" },
            { "PMULL",      "hw.optional.arm.FEAT_PMULL" },
            { "SHA1",       "hw.optional.arm.FEAT_SHA1" },
            { "SHA2",       "hw.optional.arm.FEAT_SHA256" },
            { "SHA3",       "hw.optional.arm.FEAT_SHA3",     "hw.optional.armv8_2_sha3" },
            { "SHA512",     "hw.optional.arm.FEAT_SHA512",   "hw.optional.armv8_2_sha512" },
            { "ATOMICS",    "hw.optional.arm.FEAT_LSE",      "hw.optional.armv8_1_atomics" },
            { "USCAT",      "hw.optional.arm.FEAT_LSE2" },
            { "FPHP",       "hw.optional.arm.FEAT_FP16",     "hw.optional.neon_fp16" },
            { "ASIMDHP",    "hw.optional.arm.FEAT_FP16",     "hw.optional.neon_fp16" },
            { "ASIMDRDM",   "hw.optional.arm.FEAT_RDM" },
            { "ASIMDDP",    "hw.optional.arm.FEAT_DotProd" },
            { "ASIMDFHM",   "hw.optional.arm.FEAT_FHM",      "hw.optional.armv8_2_fhm" },
            { "JSCVT",      "hw.optional.arm.FEAT_JSCVT" },
            { "FCMA",       "hw.optional.arm.FEAT_FCMA",     "hw.optional.armv8_3_compnum" },
            { "LRCPC",      "hw.optional.arm.FEAT_LRCPC" },
            { "ILRCPC",     "hw.optional.arm.FEAT_LRCPC2" },
            { "DCPOP",      "hw.optional.arm.FEAT_DPB" },
            { "DCPODP",     "hw.optional.arm.FEAT_DPB2" },
            { "FLAGM",      "hw.optional.arm.FEAT_FlagM" },
            { "FLAGM2",     "hw.optional.arm.FEAT_FlagM2" },
            { "FRINT",      "hw.optional.arm.FEAT_FRINTTS" },
            { "SSBS",       "hw.optional.arm.FEAT_SSBS" },
            { "SB",         "hw.optional.arm.FEAT_SB" },
            { "BTI",        "hw.optional.arm.FEAT_BTI" },
            { "I8MM",       "hw.optional.arm.FEAT_I8MM" },
            { "BF16",       "hw.optional.arm.FEAT_BF16" },
            { "ECV",        "hw.optional.arm.FEAT_ECV" },
        };

        std::vector<std::string> extensions;
        for (const auto& [name, sysctl, legacySysctl] : features)
        {
            if (GetOSInteger(sysctl) || (legacySysctl && GetOSInteger(legacySysctl))) extensions.push_back(name);
        }

        std::sort(extensions.begin(), extensions.end());
        return extensions;
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
