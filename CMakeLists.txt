# Copyright © 2026 CCP ehf.

cmake_minimum_required(VERSION 3.30)

# Allow setting MSVC runtime via `CMAKE_MSVC_RUNTIME_LIBRARY`
cmake_policy(SET CMP0091 NEW)

file (STRINGS "BuildNumber.txt" BUILD_NUMBER)
string(REPLACE "\"" "" BUILD_NUMBER ${BUILD_NUMBER})
project(pdm VERSION ${BUILD_NUMBER})

option(DLL_EXPORT "Export symbols (for building DLL)" OFF)

list(INSERT CMAKE_MODULE_PATH 0 ${CMAKE_SOURCE_DIR}/cmake)

set(SRC_FILES
	src/pdm.cpp
	src/gatherer.cpp
	src/output.cpp
	src/utilities.cpp
	src/cpu_vm.cpp
	src/arch/x86_64.cpp
	src/arch/arm64.cpp
	src/macos/cpu_macos.cpp
	src/linux/cpu_linux.cpp
	src/linux/linux_data.cpp
	src/wine/wine.cpp
	src/windows/windows_data.cpp
	src/windows/d3d_info.cpp
	src/windows/vulkan_info.cpp
)

# For IDE support
set(HDR_FILES
	include/version.h
	include/pdm.h
	include/pdm_data.h
	src/defines.h
	src/gatherer.h
	src/cpu.h
	src/platform_cpu.h
	src/output.h
	src/windows/d3d_info.h
)

add_library(pdm STATIC
	${SRC_FILES}
	${HDR_FILES}
 )
add_library(ccp::pdm ALIAS pdm)

target_include_directories(pdm
	PUBLIC
		$<INSTALL_INTERFACE:include>
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(DLL_EXPORT)
	target_compile_definitions(pdm PRIVATE PDM_DLL_EXPORT)
endif()

add_executable(pdmCLI
	${SRC_FILES}
	${HDR_FILES}
	src/main.cpp
)

if (APPLE)
	find_library(IOKit IOKit)
	find_library(AppKit AppKit)
	find_library(Metal Metal)
	target_sources(pdmCLI
		PRIVATE
			src/macos/macos_data.mm
	)
	target_sources(pdm
		PRIVATE
			src/macos/macos_data.mm
	)
	target_link_libraries(pdmCLI ${IOKit} ${AppKit} ${Metal})
	target_link_libraries(pdm    ${IOKit} ${AppKit} ${Metal})

	# Silence no symbol warnings
	SET(CMAKE_C_ARCHIVE_CREATE   "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
	SET(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
	SET(CMAKE_C_ARCHIVE_FINISH   "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
	SET(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
endif()

if (UNIX AND NOT APPLE)
	# Vulkan is loaded at runtime, glibc before 2.34 needs libdl for that
	target_link_libraries(pdmCLI ${CMAKE_DL_LIBS})
	target_link_libraries(pdm    ${CMAKE_DL_LIBS})
endif()

# Set up testing
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
	option(BUILD_TESTING "Build and run tests. Enabled by default." ON)
	if(BUILD_TESTING)
		add_subdirectory(tests)
	endif()
endif()

# Install rules
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
	install(
		TARGETS pdm
		EXPORT pdmTargets
		CONFIGURATIONS ${CMAKE_CONFIGURATION_TYPES}
		ARCHIVE DESTINATION lib/
		LIBRARY DESTINATION lib/
	)
	# Install rule for available public headers
	install(DIRECTORY include/ DESTINATION include)
	install(
		EXPORT pdmTargets
		DESTINATION ${CMAKE_INSTALL_PREFIX}/share/carbon-pdm  # Calling this carbon-pdm
		FILE carbon-pdmConfig.cmake
	)
endif()
