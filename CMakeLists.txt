cmake_minimum_required (VERSION 3.21)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW) # enable MSVC_RUNTIME_LIBRARY
endif()

project ("smol-cube")

add_executable (smol-cube
	src/smol_cube_conv_app.cpp
	src/smol_cube.cpp
	src/smol_cube.h
	CMakeLists.txt
	CMakePresets.json
)
set_property(TARGET smol-cube PROPERTY CXX_STANDARD 17)
set_property(TARGET smol-cube PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_definitions(smol-cube PRIVATE
	_CRT_SECURE_NO_DEPRECATE
	_CRT_NONSTDC_NO_WARNINGS
	NOMINMAX
)

if((CMAKE_CXX_COMPILER_ID MATCHES "Clang") AND (CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64"))
	target_compile_options(smol-cube PRIVATE -msse4.1)
endif()
