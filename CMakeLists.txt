cmake_minimum_required (VERSION 3.21)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW) # enable MSVC_RUNTIME_LIBRARY
endif()
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW) # enable override of options from parent CMakeLists.txt
endif()
if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW) # avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24+
endif()
if(POLICY CMP0092)
    cmake_policy(SET CMP0092 NEW) # enables /W4 override for MSVC
endif()

include(FetchContent)

# zstd
FetchContent_Declare(
	zstd
	URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.6.zip # latest (2024 Mar) at time of writing
	SOURCE_SUBDIR build/cmake
)
set(ZSTD_LEGACY_SUPPORT OFF)
set(ZSTD_MULTITHREAD_SUPPORT OFF)
set(ZSTD_BUILD_TESTS OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_BUILD_CONTRIB OFF)
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
FetchContent_MakeAvailable(zstd)

project ("smol-cube")

add_executable (smol-cube
	src/main.cpp
	src/compression_helpers.cpp
	src/compression_helpers.h
		
	src/filters.cpp
	src/filters.h
	src/simd.h
	src/smol_cube.cpp
	src/smol_cube.h
	src/systeminfo.cpp
	src/systeminfo.h

	CMakeLists.txt
	CMakePresets.json
)
set_property(TARGET smol-cube PROPERTY CXX_STANDARD 20)

target_include_directories(smol-cube PRIVATE 
	${zstd_SOURCE_DIR}/lib
)

target_link_libraries(smol-cube PRIVATE
	libzstd_static
)

target_compile_definitions(smol-cube PRIVATE
	_CRT_SECURE_NO_DEPRECATE
	_CRT_NONSTDC_NO_WARNINGS
	NOMINMAX
)

if((CMAKE_CXX_COMPILER_ID MATCHES "Clang") AND (CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64"))
	target_compile_options(smol-cube PRIVATE -msse4.1)
endif()


# Enable debug symbols (RelWithDebInfo is not only that; it also turns on
# incremental linking, disables some inlining, etc. etc.)
set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
set(CMAKE_XCODE_ATTRIBUTE_COPY_PHASE_STRIP "NO")
set(CMAKE_XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT "NO")

# note: this does not work; have to pass -g instead
#set(CMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES")

if (CMAKE_BUILD_TYPE STREQUAL "Release")
	if (MSVC)
		target_compile_options(smol-cube PRIVATE /Zi)
		target_link_options(smol-cube PRIVATE /DEBUG /OPT:ICF /OPT:REF)
	endif()
	if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(smol-cube PRIVATE -g)
		target_link_options(smol-cube PRIVATE -g)
	endif()
endif ()
