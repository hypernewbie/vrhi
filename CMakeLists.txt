# =============================================================================
# Vrhi
# =============================================================================
#
# Copyright 2026 UAA Software
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
# associated documentation files (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge, publish, distribute,
# sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all copies or substantial
# portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
# NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
# OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# Force Clang on non-Windows platforms
if(NOT WIN32)
    set(CMAKE_C_COMPILER clang CACHE STRING "")
    set(CMAKE_CXX_COMPILER clang++ CACHE STRING "")
endif()

project(vrhi_test C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Platform Detection
# -----------------------------------------------------------------------------
if(WIN32)
    set(VRHI_LIB_PLATFORM "win")
elseif(APPLE)
    set(VRHI_LIB_PLATFORM "mac")
else()
    set(VRHI_LIB_PLATFORM "linux")
endif()

# Library directory with config-aware path (debug/release)
set(VRHI_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/${VRHI_LIB_PLATFORM}_$<LOWER_CASE:$<CONFIG>>")

# -----------------------------------------------------------------------------
# Compiler Options
# -----------------------------------------------------------------------------
if(MSVC)
    # Static C Runtime (/MT for Release, /MTd for Debug)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # W3: Standard warnings, wd4100: Ignore unused params, MP: Multi-processor
    add_compile_options(/W3 /wd4100 /MP)
else()
    # Suppress warnings and use libc++
    add_compile_options(-w -stdlib=libc++)
    add_link_options(-stdlib=libc++ -lc++abi)
endif()

# -----------------------------------------------------------------------------
# Helper Functions
# -----------------------------------------------------------------------------

# Suppress warnings for vendored/external source files
function(vrhi_suppress_vendor_warnings)
    foreach(source IN LISTS ARGN)
        if(MSVC)
            set_source_files_properties(${source} PROPERTIES 
                COMPILE_OPTIONS "/wd4244;/wd4267;/wd4013")
        else()
            set_source_files_properties(${source} PROPERTIES 
                COMPILE_OPTIONS "-Wno-missing-field-initializers")
        endif()
    endforeach()
endfunction()

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

# Vulkan SDK
if(DEFINED ENV{VULKAN_SDK})
    set(Vulkan_ROOT $ENV{VULKAN_SDK})
endif()
find_package(Vulkan REQUIRED)

# Python (optional - for code generation)
find_package(Python3 COMPONENTS Interpreter)

# SPIRV-Reflect (from Vulkan SDK)
find_path(SPIRV_REFLECT_DIR NAMES spirv_reflect.c
    HINTS 
        "$ENV{VULKAN_SDK}/source/SPIRV-Reflect"
        "$ENV{VULKAN_SDK}/Source/SPIRV-Reflect"
        "${Vulkan_INCLUDE_DIR}/../source/SPIRV-Reflect"
        "${Vulkan_INCLUDE_DIR}/../Source/SPIRV-Reflect"
        "${Vulkan_INCLUDE_DIR}/SPIRV-Reflect"
)

if(NOT SPIRV_REFLECT_DIR)
    message(FATAL_ERROR "SPIRV-Reflect not found in Vulkan SDK")
endif()

set(SPIRV_REFLECT_SOURCE "${SPIRV_REFLECT_DIR}/spirv_reflect.c")
set(SPIRV_REFLECT_HEADER "${SPIRV_REFLECT_DIR}/spirv_reflect.h")

# -----------------------------------------------------------------------------
# Code Generation
# -----------------------------------------------------------------------------
set(GENERATED_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/vrhi_generated.h")

if(Python3_Interpreter_FOUND)
    add_custom_command(
        OUTPUT "${GENERATED_HEADER}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/vidl.py" 
                "${CMAKE_CURRENT_SOURCE_DIR}/vrhi.h" "${GENERATED_HEADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vidl.py" 
                "${CMAKE_CURRENT_SOURCE_DIR}/vrhi.h"
        COMMENT "Generating vrhi_generated.h via vidl.py..."
        VERBATIM
    )
endif()

# -----------------------------------------------------------------------------
# VRHI Library
# -----------------------------------------------------------------------------
set(VRHI_SOURCES
    vrhi.h
    vrhi_defines.h
    ${GENERATED_HEADER}
    src/vrhi_internal.h
    src/vrhi_utils.h
    src/vrhi_backend.h
    src/vrhi_internal.cpp
    src/vrhi_backend.cpp
    src/vrhi_device.cpp
    src/vrhi_texture.cpp
    src/vrhi_buffer.cpp
    src/vrhi_shader.cpp
    src/vrhi_state.cpp
    ${SPIRV_REFLECT_SOURCE}
    ${SPIRV_REFLECT_HEADER}
    vdeps/OffsetAllocator/offsetAllocator.cpp
    vdeps/OffsetAllocator/offsetAllocator.hpp
)

add_library(vrhi STATIC ${VRHI_SOURCES})

# Suppress warnings on vendored code
vrhi_suppress_vendor_warnings(
    ${SPIRV_REFLECT_SOURCE}
    vdeps/OffsetAllocator/offsetAllocator.cpp
)

# Compile definitions
target_compile_definitions(vrhi PUBLIC RGFW_NO_X11_CURSOR RGFW_NO_DPI)
target_compile_definitions(vrhi PRIVATE VRHI_UNIT_TEST)
if(WIN32)
    target_compile_definitions(vrhi PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# Include directories
target_include_directories(vrhi PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/vdeps/OffsetAllocator"
)
target_include_directories(vrhi PRIVATE "${SPIRV_REFLECT_DIR}")

# Library search paths
target_link_directories(vrhi PUBLIC "${VRHI_LIB_DIR}")

# Link dependencies
target_link_libraries(vrhi PUBLIC Vulkan::Vulkan)

if(UNIX AND NOT APPLE)
    target_link_libraries(vrhi PUBLIC X11 Xi Xext)
endif()

if(WIN32)
    target_link_libraries(vrhi PUBLIC dxgi shlwapi)
endif()

# -----------------------------------------------------------------------------
# Test Executable
# -----------------------------------------------------------------------------
set(TEST_SOURCES
    test/test.h
    test/utest.h
    test/test.cpp
    test/test_backend.cpp
    test/test_device.cpp
    test/test_texture.cpp
    test/test_buffer.cpp
    test/test_shader.cpp
    test/test_state.cpp
    test/test_compute.cpp
    test/test_gfx.cpp
    test/test_window.cpp
)

add_executable(vrhi_test ${TEST_SOURCES})

target_link_libraries(vrhi_test PRIVATE vrhi)
target_include_directories(vrhi_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vdeps/RGFW")

# NVRHI and related libraries (linked by all platforms)
# Order matters on Linux: dependents first, then their dependencies
set(NVRHI_LIBS nvrhi_vk nvrhi vk-bootstrap rtxmu)

# Platform-specific test configuration
if(WIN32)
    target_compile_definitions(vrhi_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(vrhi_test PRIVATE ${NVRHI_LIBS})
elseif(APPLE)
    target_link_libraries(vrhi_test PRIVATE
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework Foundation"
        "-framework AppKit"
        ${NVRHI_LIBS}
        Vulkan::Vulkan
    )
else() # Linux
    target_link_libraries(vrhi_test PRIVATE ${NVRHI_LIBS} Vulkan::Vulkan)
endif()

# -----------------------------------------------------------------------------
# Testing
# -----------------------------------------------------------------------------
enable_testing()
add_test(NAME vrhi_test COMMAND vrhi_test)

# -----------------------------------------------------------------------------
# Packaging
# -----------------------------------------------------------------------------
add_custom_target(package_vrhi
    # Create output directories
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/lib"
    
    # Copy main VRHI headers
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "${CMAKE_SOURCE_DIR}/vrhi.h" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "${CMAKE_SOURCE_DIR}/vrhi_defines.h" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "${CMAKE_SOURCE_DIR}/src/vrhi_generated.h" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/"
    
    # Copy dependency headers
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
        "${CMAKE_SOURCE_DIR}/include/glm" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/glm"
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
        "${CMAKE_SOURCE_DIR}/include/nvrhi" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/nvrhi"
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
        "${CMAKE_SOURCE_DIR}/include/vk-bootstrap" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/vk-bootstrap"
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
        "${CMAKE_SOURCE_DIR}/include/rtxmu" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/rtxmu"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "${CMAKE_SOURCE_DIR}/include/renderdoc_app.h" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/"
    
    # Copy compiled libraries
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
        "${VRHI_LIB_DIR}/" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/lib/"
    
    # Copy VRHI library from build output
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "$<TARGET_FILE:vrhi>" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/lib/"
    
    # Copy PDB files (Windows only, handled by script)
    COMMAND ${CMAKE_COMMAND} 
        -DCONFIG=$<CONFIG> 
        -DLIB_PATH=${VRHI_LIB_DIR} 
        -DBINARY_DIR=${CMAKE_BINARY_DIR} 
        -P ${CMAKE_SOURCE_DIR}/copy_pdbs.cmake
    
    # Copy documentation
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "${CMAKE_SOURCE_DIR}/README.md" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "${CMAKE_SOURCE_DIR}/LICENSE" 
        "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/"
    
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS vrhi
    COMMENT "Creating VRHI release package in ${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/"
    VERBATIM
)

# Package validation target
add_custom_target(test_package_vrhi
    DEPENDS package_vrhi
    COMMAND ${CMAKE_COMMAND}
        -DCONFIG=$<CONFIG>
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/test/run_test_package.cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Testing packaged VRHI library..."
    VERBATIM
)

message(STATUS "")
message(STATUS "VRHI Build Configuration:")
message(STATUS "  Platform: ${VRHI_LIB_PLATFORM}")
message(STATUS "  Library path: ${VRHI_LIB_DIR}")
message(STATUS "")
message(STATUS "Available targets:")
message(STATUS "  vrhi_test        - Build and run tests")
message(STATUS "  package_vrhi     - Create release package")
message(STATUS "  test_package_vrhi - Validate packaged library (manual)")
