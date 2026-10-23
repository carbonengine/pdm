# Copyright © 2026 CCP ehf.

find_package(GTest CONFIG REQUIRED)
add_executable(Tests
	Tests.cpp
)

target_link_libraries(Tests PRIVATE pdm GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(Tests DISCOVERY_MODE PRE_TEST)

if(CMAKE_IMPORT_LIBRARY_SUFFIX)
	add_custom_command(TARGET Tests POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:Tests> $<TARGET_FILE_DIR:Tests>
		COMMAND_EXPAND_LISTS
		VERBATIM
	)
endif()