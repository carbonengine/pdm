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
        // The sysctls Apple documents, see "Determining Instruction Set Characteristics":
        // https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_instruction_set_characteristics
        // followed by the remaining features from the Arm Architecture Reference Manual for A-profile that are usable at EL0:
        // https://developer.arm.com/documentation/ddi0487/latest
        // These use the same hw.optional.arm.FEAT_<name> scheme, with the name spelled as in the Arm ARM.
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

            // ARM features beyond Apple's documented list
            { "SM3",        "hw.optional.arm.FEAT_SM3" },
            { "SM4",        "hw.optional.arm.FEAT_SM4" },
            { "LSE128",     "hw.optional.arm.FEAT_LSE128" },
            { "LRCPC3",     "hw.optional.arm.FEAT_LRCPC3" },
            { "DIT",        "hw.optional.arm.FEAT_DIT" },
            { "PACA",       "hw.optional.arm.FEAT_PAuth" },
            { "PACG",       "hw.optional.arm.FEAT_PAuth" },
            { "PAUTH2",     "hw.optional.arm.FEAT_PAuth2" },
            { "FPAC",       "hw.optional.arm.FEAT_FPAC" },
            { "FPACCOMBINE", "hw.optional.arm.FEAT_FPACCOMBINE" },
            { "CSV2",       "hw.optional.arm.FEAT_CSV2" },
            { "CSV3",       "hw.optional.arm.FEAT_CSV3" },
            { "SPECRES",    "hw.optional.arm.FEAT_SPECRES" },
            { "SPECRES2",   "hw.optional.arm.FEAT_SPECRES2" },
            { "CLRBHB",     "hw.optional.arm.FEAT_CLRBHB" },
            { "DGH",        "hw.optional.arm.FEAT_DGH" },
            { "RNG",        "hw.optional.arm.FEAT_RNG" },
            { "MTE",        "hw.optional.arm.FEAT_MTE2" },
            { "MTE3",       "hw.optional.arm.FEAT_MTE3" },
            { "AFP",        "hw.optional.arm.FEAT_AFP" },
            { "RPRES",      "hw.optional.arm.FEAT_RPRES" },
            { "EBF16",      "hw.optional.arm.FEAT_EBF16" },
            { "WFXT",       "hw.optional.arm.FEAT_WFxT" },
            { "CSSC",       "hw.optional.arm.FEAT_CSSC" },
            { "RPRFM",      "hw.optional.arm.FEAT_RPRFM" },
            { "MOPS",       "hw.optional.arm.FEAT_MOPS" },
            { "HBC",        "hw.optional.arm.FEAT_HBC" },
            { "CMPBR",      "hw.optional.arm.FEAT_CMPBR" },
            { "LS64",       "hw.optional.arm.FEAT_LS64" },
            { "GCS",        "hw.optional.arm.FEAT_GCS" },
            { "POE",        "hw.optional.arm.FEAT_S1POE" },
            { "FPMR",       "hw.optional.arm.FEAT_FPMR" },
            { "FAMINMAX",   "hw.optional.arm.FEAT_FAMINMAX" },
            { "LUT",        "hw.optional.arm.FEAT_LUT" },
            { "LSFE",       "hw.optional.arm.FEAT_LSFE" },
            { "F8CVT",      "hw.optional.arm.FEAT_FP8" },
            { "F8FMA",      "hw.optional.arm.FEAT_FP8FMA" },
            { "F8DP4",      "hw.optional.arm.FEAT_FP8DOT4" },
            { "F8DP2",      "hw.optional.arm.FEAT_FP8DOT2" },
            { "SVE",        "hw.optional.arm.FEAT_SVE" },
            { "SVE2",       "hw.optional.arm.FEAT_SVE2" },
            { "SVE2P1",     "hw.optional.arm.FEAT_SVE2p1" },
            { "SVE2P2",     "hw.optional.arm.FEAT_SVE2p2" },
            { "SVEAES",     "hw.optional.arm.FEAT_SVE_AES" },
            { "SVEPMULL",   "hw.optional.arm.FEAT_SVE_PMULL128" },
            { "SVEBITPERM", "hw.optional.arm.FEAT_SVE_BitPerm" },
            { "SVESHA3",    "hw.optional.arm.FEAT_SVE_SHA3" },
            { "SVESM4",     "hw.optional.arm.FEAT_SVE_SM4" },
            { "SVEB16B16",  "hw.optional.arm.FEAT_SVE_B16B16" },
            { "SVEF32MM",   "hw.optional.arm.FEAT_F32MM" },
            { "SVEF64MM",   "hw.optional.arm.FEAT_F64MM" },
            { "SME",        "hw.optional.arm.FEAT_SME" },
            { "SME2",       "hw.optional.arm.FEAT_SME2" },
            { "SME2P1",     "hw.optional.arm.FEAT_SME2p1" },
            { "SME2P2",     "hw.optional.arm.FEAT_SME2p2" },
            { "SME_FA64",   "hw.optional.arm.FEAT_SME_FA64" },
            { "SME_I16I64", "hw.optional.arm.FEAT_SME_I16I64" },
            { "SME_F64F64", "hw.optional.arm.FEAT_SME_F64F64" },
            { "SME_F16F16", "hw.optional.arm.FEAT_SME_F16F16" },
            { "SME_B16B16", "hw.optional.arm.FEAT_SME_B16B16" },
            { "SME_LUTV2",  "hw.optional.arm.FEAT_SME_LUTv2" },
            { "SME_F8F16",  "hw.optional.arm.FEAT_SME_F8F16" },
            { "SME_F8F32",  "hw.optional.arm.FEAT_SME_F8F32" },
            { "SME_SF8FMA", "hw.optional.arm.FEAT_SSVE_FP8FMA" },
            { "SME_SF8DP4", "hw.optional.arm.FEAT_SSVE_FP8DOT4" },
            { "SME_SF8DP2", "hw.optional.arm.FEAT_SSVE_FP8DOT2" },
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
