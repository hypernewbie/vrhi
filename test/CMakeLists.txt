cmake_minimum_required(VERSION 3.20)
project(test_package_exe)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler options
if(MSVC)
    # Static runtime (/MT or /MTd)
    if(NOT CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Clang: use libc++
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++ -lc++abi)
else()
    # GCC or other: no special options needed
endif()

# Find Vulkan
if(DEFINED ENV{VULKAN_SDK})
    set(Vulkan_ROOT $ENV{VULKAN_SDK})
endif()
find_package(Vulkan REQUIRED)

# Find SPIRV-Tools for the package test
get_filename_component( VULKAN_LIB_DIR_PKG "${Vulkan_LIBRARY}" DIRECTORY )
find_library( SPIRV_TOOLS_OPT_PKG NAMES SPIRV-Tools-opt 
    HINTS 
        "${VRHI_PACKAGE_PATH}/lib"
        "${VULKAN_LIB_DIR_PKG}" 
)
find_library( SPIRV_TOOLS_PKG NAMES SPIRV-Tools 
    HINTS 
        "${VRHI_PACKAGE_PATH}/lib"
        "${VULKAN_LIB_DIR_PKG}" 
)

# Slang (from Vulkan SDK, fallback to package)
find_path(SLANG_INCLUDE_DIR NAMES slang.h
    HINTS
        "$ENV{VULKAN_SDK}/include/slang"
        "${Vulkan_INCLUDE_DIR}/slang"
        "${VRHI_PACKAGE_PATH}/include"
)

find_library(SLANG_LIBRARY NAMES slang slang.lib libslang.so libslang.dylib
    HINTS
        "${VULKAN_LIB_DIR_PKG}"
        "$ENV{VULKAN_SDK}/lib"
        "$ENV{VULKAN_SDK}/Lib"
        "${VRHI_PACKAGE_PATH}/lib"
    NO_DEFAULT_PATH
)

if(NOT SLANG_INCLUDE_DIR OR NOT SLANG_LIBRARY)
    message(FATAL_ERROR "Slang not found. Package validation requires Slang from Vulkan SDK or package.")
endif()

# VRHI Package path (passed from driver script)
if(NOT VRHI_PACKAGE_PATH)
    message(FATAL_ERROR "VRHI_PACKAGE_PATH not specified")
endif()

# Main executable
add_executable(test_package_exe test_package.cpp)

# Include paths
target_include_directories(test_package_exe PRIVATE
    "${VRHI_PACKAGE_PATH}/include"
    "${SLANG_INCLUDE_DIR}"
)

# Library paths
target_link_directories(test_package_exe PRIVATE
    "${VRHI_PACKAGE_PATH}/lib"
)

# Link libraries
target_link_libraries(test_package_exe PRIVATE
    vrhi
    nvrhi_vk
    nvrhi
    vk-bootstrap
    rtxmu
    ${SLANG_LIBRARY}
    Vulkan::Vulkan
    ${SPIRV_TOOLS_OPT_PKG}
    ${SPIRV_TOOLS_PKG}
)

# Platform-specific system libraries
if(WIN32)
    target_link_libraries(test_package_exe PRIVATE
        dxgi
        shlwapi
    )
elseif(APPLE)
    target_link_libraries(test_package_exe PRIVATE
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework Foundation"
        "-framework AppKit"
    )
else()
    # Linux
    target_link_libraries(test_package_exe PRIVATE
        X11
        Xi
        Xext
    )
endif()

# Platform defines
if(WIN32)
    target_compile_definitions(test_package_exe PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()