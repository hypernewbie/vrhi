cmake_minimum_required(VERSION 3.20)

if(NOT WIN32)
    set(CMAKE_C_COMPILER clang CACHE STRING "")
    set(CMAKE_CXX_COMPILER clang++ CACHE STRING "")
endif()

project(vrhi_test C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use Static C Runtime (/MT for Release, /MTd for Debug)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W3 /wd4100 /MP) # W3: Standard warning level, wd4100: Ignore unused parameters, MP: Multi-processor compilation
else()
    add_compile_options(-w -stdlib=libc++) # Squish all warnings and use libc++
    add_link_options(-stdlib=libc++ -lc++abi)
endif()

# Find Python Interpreter (Optional)
find_package(Python3 COMPONENTS Interpreter)

# Find Vulkan (REQUIRED) - moved up to ensure Vulkan_INCLUDE_DIR is set
if(DEFINED ENV{VULKAN_SDK})
    set(Vulkan_ROOT $ENV{VULKAN_SDK})
endif()
find_package(Vulkan REQUIRED)

set(GENERATED_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/vrhi_generated.h")

# Code Generation (only if Python is found)
if(Python3_Interpreter_FOUND)
    add_custom_command(
        OUTPUT "${GENERATED_HEADER}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/vidl.py" "${CMAKE_CURRENT_SOURCE_DIR}/vrhi.h" "${GENERATED_HEADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vidl.py" "${CMAKE_CURRENT_SOURCE_DIR}/vrhi.h"
        COMMENT "Generating vrhi_generated.h via vidl.py..."
        VERBATIM
    )
endif()

# VRHI Library Sources
find_path(SPIRV_REFLECT_DIR NAMES spirv_reflect.c
    HINTS 
        "$ENV{VULKAN_SDK}/source/SPIRV-Reflect"
        "$ENV{VULKAN_SDK}/Source/SPIRV-Reflect"
        "${Vulkan_INCLUDE_DIR}/../source/SPIRV-Reflect"
        "${Vulkan_INCLUDE_DIR}/../Source/SPIRV-Reflect"
        "${Vulkan_INCLUDE_DIR}/SPIRV-Reflect"
)

if(SPIRV_REFLECT_DIR)
    set(SPIRV_REFLECT_SOURCE "${SPIRV_REFLECT_DIR}/spirv_reflect.c")
    set(SPIRV_REFLECT_HEADER "${SPIRV_REFLECT_DIR}/spirv_reflect.h")
else()
    message(FATAL_ERROR "SPIRV-Reflect not found in Vulkan SDK")
endif()

set(VRHI_SOURCES
    vrhi.h
    src/vrhi_internal.h
    vrhi_defines.h
    ${GENERATED_HEADER}
    src/vrhi_utils.h
    src/vrhi_backend.h

    src/vrhi_internal.cpp
    ${SPIRV_REFLECT_SOURCE}
    ${SPIRV_REFLECT_HEADER}
    src/vrhi_backend.cpp
    src/vrhi_device.cpp
    src/vrhi_texture.cpp
    src/vrhi_buffer.cpp
    src/vrhi_shader.cpp
    src/vrhi_state.cpp
    vdeps/OffsetAllocator/offsetAllocator.cpp
    vdeps/OffsetAllocator/offsetAllocator.hpp
)

# Test Sources
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

# Add VRHI Static Library
add_library(vrhi STATIC ${VRHI_SOURCES})

if(MSVC)
    set_source_files_properties(${SPIRV_REFLECT_SOURCE} PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4267;/wd4013")
    set_source_files_properties(vdeps/OffsetAllocator/offsetAllocator.cpp PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4267;/wd4013")
else()
    set_source_files_properties(${SPIRV_REFLECT_SOURCE} PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
    set_source_files_properties(vdeps/OffsetAllocator/offsetAllocator.cpp PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
endif()

target_compile_definitions(vrhi PUBLIC 
    RGFW_NO_X11_CURSOR
    RGFW_NO_DPI
)

target_compile_definitions(vrhi PRIVATE 
    VRHI_UNIT_TEST
)

if(WIN32)
    target_compile_definitions(vrhi PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

target_include_directories(vrhi PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/vdeps/OffsetAllocator"
)

# Add Vulkan SDK Source/SPIRV-Reflect to include paths so we can directly inline inc the .c file
target_include_directories(vrhi PRIVATE
    "${SPIRV_REFLECT_DIR}"
)

# Library search paths
# Generator expressions select the correct path based on build configuration
if(WIN32)
    target_link_directories(vrhi PUBLIC
        $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_debug>
        $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_release>
        $<$<CONFIG:RelWithDebInfo>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_release>
        $<$<CONFIG:MinSizeRel>:${CMAKE_CURRENT_SOURCE_DIR}/lib/win_release>
    )
elseif(APPLE)
    target_link_directories(vrhi PUBLIC
        # Mac paths
        $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/mac_debug>
        $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/lib/mac_release>
        $<$<CONFIG:RelWithDebInfo>:${CMAKE_CURRENT_SOURCE_DIR}/lib/mac_release>
        $<$<CONFIG:MinSizeRel>:${CMAKE_CURRENT_SOURCE_DIR}/lib/mac_release>
    )
else() # Linux
    target_link_directories(vrhi PUBLIC
        # Linux paths
        $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_debug>
        $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_release>
        $<$<CONFIG:RelWithDebInfo>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_release>
        $<$<CONFIG:MinSizeRel>:${CMAKE_CURRENT_SOURCE_DIR}/lib/linux_release>
    )
endif()

# NVRHI and helper libraries
target_link_libraries(vrhi PRIVATE ${VRHI_LIBS})
target_link_libraries(vrhi PUBLIC Vulkan::Vulkan)

if(UNIX AND NOT APPLE)
    target_link_libraries(vrhi PUBLIC X11 Xi Xext)
endif()

# System libraries required by NVRHI backends on Windows
if(WIN32)
    target_link_libraries(vrhi PUBLIC
        dxgi
        shlwapi
    )
endif()

# Add Executable
add_executable(vrhi_test ${TEST_SOURCES})

# Link VRHI Library (without transitive dependencies for executable)
target_link_libraries(vrhi_test PRIVATE vrhi)

# Platform detection for library paths (moved before vrhi_test usage)
if(WIN32)
    set(VRHI_LIB_PLATFORM "win")
elseif(APPLE)
    set(VRHI_LIB_PLATFORM "mac")
else()  # Linux
    set(VRHI_LIB_PLATFORM "linux")
endif()

if(WIN32)
    target_compile_definitions(vrhi_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

target_include_directories(vrhi_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vdeps/RGFW")

if(APPLE)
    target_link_libraries(vrhi_test PRIVATE
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework Foundation"
        "-framework AppKit"
    )
    # Directly link NVRHI libraries with absolute paths to avoid propagation issues
    target_link_libraries(vrhi_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/${VRHI_LIB_PLATFORM}_debug/libvk-bootstrap.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/${VRHI_LIB_PLATFORM}_debug/libnvrhi_vk.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/${VRHI_LIB_PLATFORM}_debug/libnvrhi.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/${VRHI_LIB_PLATFORM}_debug/librtxmu.a
    )
    # Also link Vulkan from SDK
    target_link_libraries(vrhi_test PRIVATE Vulkan::Vulkan)
elseif(LINUX)
    # Directly link NVRHI libraries with absolute paths to avoid propagation issues
    target_link_libraries(vrhi_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/${VRHI_LIB_PLATFORM}_debug/libvk-bootstrap.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/${VRHI_LIB_PLATFORM}_debug/libnvrhi_vk.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/${VRHI_LIB_PLATFORM}_debug/libnvrhi.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/${VRHI_LIB_PLATFORM}_debug/librtxmu.a
    )
    # Also link Vulkan from SDK
    target_link_libraries(vrhi_test PRIVATE Vulkan::Vulkan)
endif()

# Enable CTest
enable_testing()
add_test(NAME vrhi_test COMMAND vrhi_test)

# VRHI Release Packaging
# Platform detection for library paths
if(WIN32)
    set(VRHI_LIB_PLATFORM "win")
elseif(APPLE)
    set(VRHI_LIB_PLATFORM "mac")
else()  # Linux
    set(VRHI_LIB_PLATFORM "linux")
endif()

# Library source path with configuration
set(VRHI_LIB_SOURCE_PATH "${CMAKE_SOURCE_DIR}/lib/${VRHI_LIB_PLATFORM}_$<LOWER_CASE:$<CONFIG>>")

# Package target - creates self-contained release package
add_custom_target(package_vrhi
    # Create output directories
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/lib"
    
    # Copy main VRHI headers
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/vrhi.h" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/vrhi_defines.h" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/src/vrhi_generated.h" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/"
    
    # Copy dependency headers
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/include/glm" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/glm"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/include/nvrhi" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/nvrhi"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/include/vk-bootstrap" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/vk-bootstrap"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/include/rtxmu" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/rtxmu"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/include/renderdoc_app.h" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include/"
    
    # Copy compiled libraries
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VRHI_LIB_SOURCE_PATH}/" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/lib/"
    
    # Copy VRHI library from build output
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:vrhi>" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/lib/"
    
    # Copy PDB files (only if they exist - script handles missing files)
    COMMAND ${CMAKE_COMMAND} -DCONFIG=$<CONFIG> -DLIB_PATH=${VRHI_LIB_SOURCE_PATH} -DBINARY_DIR=${CMAKE_BINARY_DIR} -P ${CMAKE_SOURCE_DIR}/copy_pdbs.cmake
    
    # Copy documentation
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/README.md" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/"
    
    # Set working directory and dependencies
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS vrhi
    COMMENT "Creating VRHI release package in ${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/"
    VERBATIM
)

# === VRHI Package Test Target ===
# Tests that the packaged VRHI library is complete and usable
# - Runs AFTER package_vrhi completes (DEPENDS)
# - FAILS the build if tests fail (FATAL_ERROR via script)
# - MANUAL target only (not in default build)

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
message(STATUS "Added test_package_vrhi target (validates packaged VRHI)")
message(STATUS "  Usage: cmake --build <build-dir> --config <Config> --target test_package_vrhi")
message(STATUS "  Note: This target is MANUAL ONLY (not in default build)")
message(STATUS "  Note: Test failures will STOP the build (FATAL_ERROR)")
