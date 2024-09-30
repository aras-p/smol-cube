cmake_minimum_required (VERSION 3.21)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW) # enable MSVC_RUNTIME_LIBRARY
endif()

project ("smol-cube")

add_executable (smol-cube-conv
	src/smol_cube_conv_app.cpp
	src/smol_cube.cpp
	src/smol_cube.h
)
add_executable (smol-cube-viewer
	src/smol_cube_viewer_app.cpp
	src/smol_cube.cpp
	src/smol_cube.h
)

set_property(TARGET smol-cube-conv PROPERTY CXX_STANDARD 17)
set_property(TARGET smol-cube-viewer PROPERTY CXX_STANDARD 17)

set_property(TARGET smol-cube-conv PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(TARGET smol-cube-viewer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_definitions(smol-cube-conv PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_WARNINGS NOMINMAX)
target_compile_definitions(smol-cube-viewer PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_WARNINGS NOMINMAX)

if(((CMAKE_CXX_COMPILER_ID MATCHES "Clang") OR (CMAKE_CXX_COMPILER_ID MATCHES "GNU")) AND
	((CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64") OR (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")))
	target_compile_options(smol-cube-conv PRIVATE -msse4.1)
	target_compile_options(smol-cube-viewer PRIVATE -msse4.1)
endif()

if (APPLE)
	target_compile_options(smol-cube-viewer PRIVATE -x objective-c++)
	target_link_libraries(smol-cube-viewer PRIVATE
		"-framework Cocoa"
		"-framework Metal"
		"-framework MetalKit"
		"-framework QuartzCore"
	)
endif ()
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_link_libraries(smol-cube-viewer PRIVATE X11 Xi Xcursor GL)
endif ()
