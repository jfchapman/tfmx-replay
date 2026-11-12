cmake_minimum_required(VERSION 3.16)
project(TFMXreplay VERSION 0.2.0 LANGUAGES C CXX)

option(TFMX_BUILD_SHARED "Build a shared library (DLL / .so / .dylib) instead of a static one" OFF)
option(TFMX_BUILD_TOOLS "Build tfmxrender and tfmxtrace" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

set(TFMX_SOURCES
	src/module.cpp
	src/paula.cpp
	src/replayer.cpp
	src/tfmxreplay.cpp
	src/variant.cpp
)

if(TFMX_BUILD_SHARED)
	add_library(tfmxreplay SHARED ${TFMX_SOURCES})
	target_compile_definitions(tfmxreplay PUBLIC TFMX_SHARED)
	set_target_properties(tfmxreplay PROPERTIES CXX_VISIBILITY_PRESET hidden VERSION ${PROJECT_VERSION} SOVERSION 0)
else()
	add_library(tfmxreplay STATIC ${TFMX_SOURCES})
endif()
add_library(TFMXreplay::tfmxreplay ALIAS tfmxreplay)

target_include_directories(tfmxreplay
	PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
	PRIVATE src)
set_target_properties(tfmxreplay PROPERTIES PUBLIC_HEADER include/tfmxreplay.h)

if(MSVC)
	target_compile_options(tfmxreplay PRIVATE /W4)
	# Link the C runtime statically by default, like most audio codec builds for Windows plugins.
	set_property(TARGET tfmxreplay PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
	target_compile_options(tfmxreplay PRIVATE -Wall -Wextra)
endif()

if(TFMX_BUILD_TOOLS)
	add_executable(tfmxrender tools/tfmxrender.c)
	target_link_libraries(tfmxrender PRIVATE tfmxreplay)
	# tfmxtrace uses the library's internal classes, which a shared library doesn't export, so
	# it builds the sources itself in that case.
	if(TFMX_BUILD_SHARED)
		add_executable(tfmxtrace tools/tfmxtrace.cpp ${TFMX_SOURCES})
		target_include_directories(tfmxtrace PRIVATE include src)
	else()
		add_executable(tfmxtrace tools/tfmxtrace.cpp)
		target_link_libraries(tfmxtrace PRIVATE tfmxreplay)
		target_include_directories(tfmxtrace PRIVATE src)
	endif()
	if(MSVC)
		set_property(TARGET tfmxrender tfmxtrace PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		target_compile_definitions(tfmxrender PRIVATE _CRT_SECURE_NO_WARNINGS)
	endif()
	# The self-contained tests, for ctest. They need Python, so they're left out without it.
	enable_testing()
	find_package(Python3 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		add_test(NAME selftest COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/selftest.py
			--tool $<TARGET_FILE:tfmxtrace> --render $<TARGET_FILE:tfmxrender>)
	endif()
endif()

include(GNUInstallDirs)
install(TARGETS tfmxreplay EXPORT TFMXreplayTargets
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT TFMXreplayTargets NAMESPACE TFMXreplay:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TFMXreplay)
include(CMakePackageConfigHelpers)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/TFMXreplayConfig.cmake
	"include(\"\${CMAKE_CURRENT_LIST_DIR}/TFMXreplayTargets.cmake\")\n")
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/TFMXreplayConfigVersion.cmake COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/TFMXreplayConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/TFMXreplayConfigVersion.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TFMXreplay)
if(TFMX_BUILD_TOOLS)
	install(TARGETS tfmxrender RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
