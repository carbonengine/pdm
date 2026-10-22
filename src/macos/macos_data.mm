// Copyright © 2026 CCP ehf.

#if __APPLE__

#include "../../include/pdm.h"
#include "../utilities.h"
#include "../defines.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <sstream>
#include <regex>

#include <vector>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>

#import <IOKit/ps/IOPowerSources.h>
#import <IOKit/ps/IOPSKeys.h>
#import <IOKit/graphics/IOGraphicsLib.h>
#import <IOKit/network/IOEthernetInterface.h>
#import <IOKit/network/IOEthernetController.h>

namespace PDM
{
    std::vector<std::string> split(const std::string& str, const std::string& regex_str)
    {
        std::regex regexz(regex_str);
        return { std::sregex_token_iterator(str.begin(), str.end(), regexz, -1), std::sregex_token_iterator() };
    }

    std::string exec(const std::string& cmd)
    {
        std::array<char, 128> buffer;
        std::stringstream result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe) return {};

        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
            result << buffer.data();
        return getTrimmed(result.str());
    }

    std::string GetOSString(const char* name)
    {
        char buffer[1024] = { 0 };
        size_t size = sizeof(buffer);
        sysctlbyname(name, buffer, &size, nullptr, 0);

        return buffer;
    }

    uint64_t GetOSInteger(const char* name)
    {
        uint64_t result = 0;
        size_t size = sizeof(result);
        sysctlbyname(name, &result, &size, nullptr, 0);

        return result;
    }

    Bitness GetOSBitnessInternal()
    {
#if __aarch64__
        return Bitness::BITNESS_64;
#else
        struct utsname un;
        int res = uname(&un);
        if (res >= 0)
        {
            std::string machine{un.machine};
            if (machine == "x86_64") return Bitness::BITNESS_64;
            if (machine == "i386"  ) return Bitness::BITNESS_32;
        }

        return Bitness::BITNESS_UNKNOWN;
#endif
    }

	std::vector<HardDriveInfo> GetHardDriveInfo()
	{
		auto strToPlist = [](const std::string& plistStr)
		{
			NSString* str = [NSString stringWithCString:plistStr.c_str() encoding:NSUTF8StringEncoding];
			NSData* plistData = [str dataUsingEncoding:NSUTF8StringEncoding];

			id plist = [NSPropertyListSerialization propertyListWithData: plistData
														options: NSPropertyListImmutable
														format: nullptr
														error: nullptr];

			return plist;
		};

		// We could also use diskutil list -plist
		// to get all disks, but iterating through all disks
		// triggers a popup dialog to the user, which we don't want
		id diskInfo = strToPlist(exec("diskutil info -plist disk0"));
		if (!diskInfo) return {};

		id name = diskInfo[@"MediaName"];
		id size = diskInfo[@"TotalSize"];
		id ssd = diskInfo[@"SolidState"];
		// We don't care about USB sticks and such
		id removable = diskInfo[@"RemovableMedia"];

		if (!name || !size || !ssd || !removable || [removable boolValue]) return {};

		return {{
			[static_cast<NSString*>(name) UTF8String],
			[ssd boolValue] ? HardDriveInfo::HardDriveType::SSD : HardDriveInfo::HardDriveType::HDD,
			static_cast<uint64_t>([size integerValue])
		}};
	}

    // Returns the max CPU frequency in MHz on Apple Silicon, or 0 if it can't be determined
    uint32_t GetAppleSiliconCPUFrequency()
    {
        io_iterator_t iterator;
        if ( IOServiceGetMatchingServices( kIOMasterPortDefault, IOServiceMatching( "AppleARMIODevice" ), &iterator ) != kIOReturnSuccess )
        {
            return 0;
        }
        SCOPE_EXIT( IOObjectRelease( iterator ) );

        uint64_t maxFrequency = 0;
        io_registry_entry_t regEntry;

        while ( ( regEntry = IOIteratorNext( iterator ) ) )
        {
            SCOPE_EXIT( IOObjectRelease( regEntry ) );

            // The power manager lists the frequency steps of each CPU core cluster
            io_name_t name;
            if ( IORegistryEntryGetName( regEntry, name ) != kIOReturnSuccess || strcmp( name, "pmgr" ) != 0 )
            {
                continue;
            }

            // voltage-states1-sram holds the efficiency cores, voltage-states5-sram the performance cores,
            // and M5 Max chips add a core cluster type in voltage-states22-sram and voltage-states23-sram
            for ( auto key : { CFSTR("voltage-states1-sram"), CFSTR("voltage-states5-sram"), CFSTR("voltage-states22-sram"), CFSTR("voltage-states23-sram") } )
            {
                auto data = static_cast<CFDataRef>( IORegistryEntryCreateCFProperty( regEntry, key, kCFAllocatorDefault, 0 ) );
                if ( !data )
                {
                    continue;
                }
                SCOPE_EXIT( CFRelease( data ) );
                if ( CFGetTypeID( data ) != CFDataGetTypeID() )
                {
                    continue;
                }

                // Each entry is a pair of little endian uint32 values: frequency and voltage (mV)
                uint64_t clusterMaxFrequency = 0;
                auto bytes = CFDataGetBytePtr( data );
                for ( CFIndex i = 0; i + 8 <= CFDataGetLength(data); i += 8 )
                {
                    uint32_t frequency;
                    memcpy( &frequency, bytes + i, sizeof( frequency ) );
                    clusterMaxFrequency = std::max<uint64_t>( clusterMaxFrequency, CFSwapInt32LittleToHost( frequency ) );
                }

                // The frequency is in Hz on M1-M3, but in kHz from M4 onwards
                if ( clusterMaxFrequency < 100'000'000 )
                {
                    clusterMaxFrequency *= 1000;
                }
                maxFrequency = std::max( maxFrequency, clusterMaxFrequency );
            }
            break;
        }

        return static_cast<uint32_t>( maxFrequency / 1000 / 1000 );
    }

    uint32_t GetCPUFrequency()
    {
#if __aarch64__
        bool appleSilicon = true;
#elif __x86_64__
        bool appleSilicon = IsRosetta();
#else
        #error "Unsupported architecture"
#endif
        if ( appleSilicon )
        {
            if ( auto frequency = GetAppleSiliconCPUFrequency(); frequency > 0 )
            {
                return frequency;
            }
        }

        auto parts = split( exec( "sysctl hw.cpufrequency_max" ), ": " );
        if ( parts.size() != 2 )
        {
            return 0;
        }
        return std::atoll( parts[1].c_str() ) / 1000 / 1000;
    }

    OS GetOSType()
    {
        return OS::MACOS;
    }

    std::string GetOSName()
    {
        return [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String];
    }

    std::string GetOSPart(unsigned part)
    {
        auto parts = split(exec("sw_vers -productVersion"), "\\.");
        return parts.size() > part ? parts[part] : "0";
    }

    std::string GetOSMajorVersion()
    {
        return GetOSPart(0);
    }

    std::string GetOSMinorVersion()
    {
        return GetOSPart(1);
    }

    std::string GetOSBuildNumber()
    {
        return GetOSPart(2);
    }

    std::string GetOSKernelVersion()
    {
        return GetOSString("kern.osrelease");
    }

    std::string GetMachineName()
    {
        return [[[NSProcessInfo processInfo] hostName] UTF8String];
    }

    std::string GetUsername()
    {
        return [[[NSProcessInfo processInfo] userName] UTF8String];
    }

    uint32_t GetMonitorCount()
    {
        return [[NSScreen screens] count];
    }

    std::string GetHardwareModel()
    {
        return GetOSString("hw.model");
    }

    uint64_t GetTotalMemory()
    {
        return [[NSProcessInfo processInfo] physicalMemory];
    }

    bool IsRemoteSession()
    {
        return false;
    }

    BatteryStatus GetBatteryStatus()
    {
        auto blob = IOPSCopyPowerSourcesInfo();
        if (!blob) return BatteryStatus::UNKNOWN;
        SCOPE_EXIT(CFRelease(blob));

        auto sources = IOPSCopyPowerSourcesList(blob);
        if (!sources) return BatteryStatus::UNKNOWN;
        SCOPE_EXIT(CFRelease(sources));

        for (long i = 0, keyCount = CFArrayGetCount(sources); i < keyCount; i++)
        {
            auto ps = CFArrayGetValueAtIndex(sources, i);
            auto dict = IOPSGetPowerSourceDescription(blob, ps);

            auto deviceType = static_cast<CFStringRef>(CFDictionaryGetValue(dict, CFSTR(kIOPSTypeKey)));
            if (deviceType && !CFStringCompare(deviceType, CFSTR(kIOPSInternalBatteryType), 0))
                return BatteryStatus::DETECTED;
        }

        return BatteryStatus::NOT_DETECTED;
    }

    std::string GetMachineUuidString()
    {
        char buffer[128] = { 0 };
        io_registry_entry_t ioRegistryRoot = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/");
        CFStringRef uuidCf = static_cast<CFStringRef>(IORegistryEntryCreateCFProperty(ioRegistryRoot, CFSTR(kIOPlatformUUIDKey), kCFAllocatorDefault, 0));
        IOObjectRelease(ioRegistryRoot);
        CFStringGetCString(uuidCf, buffer, sizeof(buffer), kCFStringEncodingMacRoman);
        CFRelease(uuidCf);

        return buffer;
    }

    std::string GetUserLocale()
    {
        return [[[NSLocale currentLocale] localeIdentifier] UTF8String];
    }

    uint32_t GetIntFromID(CFMutableDictionaryRef dict, NSString* name)
    {
        auto val = CFDictionaryGetValue(dict, name);
        if (val == nil) return 0;
        return *static_cast<const uint32_t*>([static_cast<NSData*>(val) bytes]);
    }

    uint64_t GetLongFromID(CFMutableDictionaryRef dict, NSString* name)
    {
        auto val = CFDictionaryGetValue(dict, name);
        if (val == nil) return 0;
        return *static_cast<const uint64_t*>([static_cast<NSData*>(val) bytes]);
    }

    uint32_t GetIntFromNumber(CFMutableDictionaryRef dict, NSString* name)
    {
        auto val = CFDictionaryGetValue(dict, name);
        if (val == nil) return 0;
        return [static_cast<NSNumber*>(val) unsignedIntegerValue];
    }

    NSString* screenNameForDisplay(CGDirectDisplayID displayID)
    {
        NSString *screenName = @"";

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        NSDictionary *deviceInfo = (NSDictionary *)IODisplayCreateInfoDictionary(CGDisplayIOServicePort(displayID), kIODisplayOnlyPreferredName);
#pragma clang diagnostic pop

        NSDictionary *localizedNames = [deviceInfo objectForKey:[NSString stringWithUTF8String:kDisplayProductName]];

        if ([localizedNames count] > 0) screenName = [[localizedNames objectForKey:[[localizedNames allKeys] objectAtIndex:0]] retain];

        [deviceInfo release];
        return [screenName autorelease];
    }

    std::vector<MonitorInfo> GetMonitorsInfo()
    {
        uint32_t displayCount;
        CGGetOnlineDisplayList(0, nullptr, &displayCount);
        std::vector<CGDirectDisplayID> onlineDisplays(displayCount);
        CGGetOnlineDisplayList(displayCount, &onlineDisplays[0], nullptr);

        std::vector<MonitorInfo> monitors;

        for (NSScreen* screen in [NSScreen screens])
        {
            uint32_t refreshRate = 0;
            NSString* screenName = [screen respondsToSelector:NSSelectorFromString(@"localizedName")] ? [(id)screen localizedName] : @"";

            if (id d = screen.deviceDescription[@"NSScreenNumber"]; d)
            {
                unsigned nr = [d unsignedIntValue];
                for (CGDirectDisplayID display : onlineDisplays)
                {
                    if (display == nr)
                    {
                        if ([screenName isEqual:@""]) screenName = screenNameForDisplay(display);
                        refreshRate = static_cast<uint32_t>(CGDisplayModeGetRefreshRate(CGDisplayCopyDisplayMode(display)));
                        break;
                    }
                }
            }

            monitors.push_back
            ({
                [screenName UTF8String],
                static_cast<uint32_t>(screen.frame.size.width  * screen.backingScaleFactor),
                static_cast<uint32_t>(screen.frame.size.height * screen.backingScaleFactor),
                static_cast<uint32_t>(NSBitsPerSampleFromDepth(screen.depth)),
                refreshRate,
                static_cast<uint32_t>(screen.backingScaleFactor * 100),
            });
        }

        return monitors;
    }

    std::string bytesToHexString(NSData* data)
    {
        auto result = [[NSMutableString alloc] initWithCapacity: [data length] << 1];
        auto mbytes = static_cast<const UInt8*>([data bytes]);
        char hBytes[8] = {'\0'};

        for (unsigned i = 0; i < [data length]; i++)
        {
            snprintf(hBytes, 3, "%02X", mbytes[i]);
            [result appendFormat:(i ? @":%s" : @"%s"), hBytes];
        }

        std::string str = [result UTF8String];
        [result release];

        return str;
    }

    std::vector<NetworkAdapterInfo> GetNetworkAdapterInfo()
    {
        std::vector<NetworkAdapterInfo> adapters;

        mach_port_t machPort;
        IOMasterPort(MACH_PORT_NULL, &machPort);
        io_iterator_t netIterator = {0};
        IOServiceGetMatchingServices(machPort, IOServiceMatching(kIOEthernetInterfaceClass), &netIterator);
        io_object_t interfaceService, controllerService;

        while ( (interfaceService = IOIteratorNext(netIterator)) )
        {
            if (kern_return_t kernResult = IORegistryEntryGetParentEntry( interfaceService, kIOServicePlane, &controllerService ); kernResult == KERN_SUCCESS)
            {
                CFTypeRef MACAddrAsCFData   = IORegistryEntryCreateCFProperty(controllerService, CFSTR(kIOMACAddress), kCFAllocatorDefault, 0);
                CFTypeRef BSDNameAsCFString = IORegistryEntryCreateCFProperty(interfaceService,  CFSTR("BSD Name"),    kCFAllocatorDefault, 0);

                if (MACAddrAsCFData && BSDNameAsCFString)
                {
                    auto str = bytesToHexString(static_cast<NSData*>(MACAddrAsCFData));
                    adapters.push_back
                    ({
                        [static_cast<NSString*>(BSDNameAsCFString) UTF8String],
                        str,
                        {},
                        HexStringToByteArray(str, 6),
                        {}
                    });
                }
                if (nil != BSDNameAsCFString) CFRelease(BSDNameAsCFString);
                if (nil != MACAddrAsCFData) CFRelease(MACAddrAsCFData);
            }
        }
        IOObjectRelease(netIterator);

        return adapters;
    }

    std::vector<GPUInfo> GetGPUInfo()
    {
        std::vector<GPUInfo> gpus;

        io_iterator_t iterator;

#ifdef __aarch64__
        bool m1 = true;
#else
        bool m1 = IsRosetta();
#endif
        auto match = m1 ? "AGXAccelerator" : "IOPCIDevice";

        if (IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching(match), &iterator) == kIOReturnSuccess)
        {
            io_registry_entry_t regEntry;

            while ((regEntry = IOIteratorNext(iterator)))
            {
                CFMutableDictionaryRef serviceDictionary;
                if (IORegistryEntryCreateCFProperties(regEntry, &serviceDictionary, kCFAllocatorDefault, kNilOptions) != kIOReturnSuccess)
                {
                    IOObjectRelease(regEntry);
                    continue;
                }

                if (auto model = static_cast<NSData*>(CFDictionaryGetValue(serviceDictionary, @"model")); model != nil)
                {
                    if (m1 || CFGetTypeID(model) == CFDataGetTypeID())
                    {
                        std::string modelStr;
                        if (m1)
                            modelStr = [static_cast<NSString*>(model) UTF8String];
                        else
                        {
                            NSString *nsStr = [[NSString alloc] initWithData:model encoding:NSASCIIStringEncoding];
                            modelStr = [nsStr UTF8String];
                            [nsStr release];
                        }

                        gpus.push_back
                        ({
                            modelStr,
                            GetIntFromID(serviceDictionary, @"vendor-id"),
                            m1 ? 0 : GetIntFromID(serviceDictionary, @"device-id"),
                            m1 ? 0 : GetIntFromID(serviceDictionary, @"revision-id"),
                            m1 ? 0 : GetLongFromID(serviceDictionary, @"VRAM,totalsize"),
                            m1 ? GetIntFromNumber(serviceDictionary, @"gpu-core-count") : 0,
                            {},
                            {},
                            {}
                        });
                    }
                }

                CFRelease(serviceDictionary);
                IOObjectRelease(regEntry);
            }

            IOObjectRelease(iterator);
        }

        return gpus;
    }

    bool GetMetalSupported()
    {
        return [MTLCopyAllDevices() count] > 0;
    }

    bool IsRosetta()
    {
        int ret = 0;
        size_t size = sizeof(ret);
        return sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) == -1 ? false : ret;
    }

    VulkanProperties GetVulkanProperties()
    {
        // We can safely assume that metal support implies vulkan support (but we might want to probe this in the future anyway)
        return
        {
            GetMetalSupported() ? VulkanSupport::SUPPORTED : VulkanSupport::UNSUPPORTED,
            {}
        };
    }

    std::wstring UTF8ToWString(const std::string_view utf8String)
    {
        NSString* str = [[NSString alloc] initWithBytes:utf8String.data() length:utf8String.size() encoding:NSUTF8StringEncoding];
        NSData* data = [str dataUsingEncoding: CFStringConvertEncodingToNSStringEncoding(kCFStringEncodingUTF32LE)];
        return std::wstring(static_cast<const wchar_t*>([data bytes]), [data length] / sizeof(wchar_t));
    }
}

#endif
