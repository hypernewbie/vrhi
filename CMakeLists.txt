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

# Enable new CMake debug-info-format abstraction (CMP0141) and emit /Z7 (embedded
# debug info, no separate PDB) so that sccache can cache MSVC builds. Set before
# any target/compile-flag logic runs.
if(MSVC)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded" CACHE STRING "MSVC debug info format" FORCE)
endif()
 
 # Default to Debug for single-config generators (Ninja, Make) if not specified
 if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
     set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
     message(STATUS "Build type not specified, defaulting to Debug")
 endif()
 
  # -----------------------------------------------------------------------------
  # Options (must be declared before dependent variables)
  # -----------------------------------------------------------------------------
  option(VDEPS_DYNAMIC_RUNTIME "Use dynamic MSVC runtime (/MD) instead of static (/MT)" OFF)
  option(VRHI_BUILD_VDEPS "Build vdeps as part of VRHI configure" OFF)

  # Sanitizers to enable on the consumer + dep build (e.g. "address", "thread",
  # "address,undefined"). Mirrors vdeps.py's --sanitize contract; empty disables.
  set( VDEPS_SANITIZE "" CACHE STRING "Sanitizers for consumer + dep build (e.g. address,undefined). Empty disables." )

  set( VDEPS_SANITIZE_ACTIVE FALSE )
  set( _VH_SANITIZE_TAG "" )
  if( VDEPS_SANITIZE )
      set( VDEPS_SANITIZE_ACTIVE TRUE )
      # Mirror vdeps.py's _normalize_sanitize_tag(): strip, drop empties, join with '_'.
      string( REPLACE "," ";" _vh_san_list "${VDEPS_SANITIZE}" )
      foreach( _part IN LISTS _vh_san_list )
          string( STRIP "${_part}" _part )
          if( _part )
              if( _VH_SANITIZE_TAG )
                  set( _VH_SANITIZE_TAG "${_VH_SANITIZE_TAG}_${_part}" )
              else()
                  set( _VH_SANITIZE_TAG "${_part}" )
              endif()
          endif()
      endforeach()

      if( WIN32 )
          if( NOT VDEPS_USE_LLVM )
              message( FATAL_ERROR "VDEPS_SANITIZE on Windows requires clang-cl (VDEPS_USE_LLVM=ON)." )
          endif()
          if( VDEPS_SANITIZE MATCHES "thread|memory" )
              message( FATAL_ERROR "thread/memory sanitizers are unavailable on Windows; use address." )
          endif()
      endif()
      if( VDEPS_SANITIZE MATCHES "memory" AND VDEPS_SANITIZE MATCHES "address" )
          message( FATAL_ERROR "memory and address sanitizers are mutually exclusive." )
      endif()
  endif()

  # -----------------------------------------------------------------------------
  # Platform Detection
  # -----------------------------------------------------------------------------
  if(WIN32)
      if(VDEPS_USE_LLVM)
          set(VRHI_LIB_PLATFORM "win_llvm")
      else()
          set(VRHI_LIB_PLATFORM "win")
      endif()
  elseif(APPLE)
      set(VRHI_LIB_PLATFORM "mac")
  else()
      set(VRHI_LIB_PLATFORM "linux")
  endif()
  
  # Library directory with config-aware path (debug/release)
  # Include _md suffix in platform subdir for dynamic runtime builds
  set(_vrhi_lib_platform "${VRHI_LIB_PLATFORM}")
if(MSVC AND VDEPS_DYNAMIC_RUNTIME)
       set(_vrhi_lib_platform "${_vrhi_lib_platform}_md")
   endif()
  if( _VH_SANITIZE_TAG )
      set( _vrhi_lib_platform "${_vrhi_lib_platform}_${_VH_SANITIZE_TAG}" )
  endif()
  set(VRHI_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/${_vrhi_lib_platform}_$<IF:$<CONFIG:RelWithDebInfo>,release,$<LOWER_CASE:$<CONFIG>>>")
 
   # -----------------------------------------------------------------------------
   # Compiler Options
   # -----------------------------------------------------------------------------
if(MSVC)
       # MSVC C Runtime: /MT or /MTd for static, /MD or /MDd for dynamic.
       # Under a sanitizer, force plain /MT (non-debug, all configs) -- clang-cl's
       # ASan rejects debug CRTs (/MTd, /MDd) outright. Mirrors vdeps.py.
       if(VDEPS_SANITIZE_ACTIVE)
          set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
      elseif(VDEPS_DYNAMIC_RUNTIME)
          set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
      else()
          set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
      endif()
    # Remove default RTC flags from Debug configuration
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTCs" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTCs" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTCu" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTCu" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
      # W3: Standard warnings, wd4100: Ignore unused params, MP: Multi-processor
      add_compile_options(/W3 /wd4100 /MP)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      # Clang: suppress warnings and use libc++
      add_compile_options(-w -stdlib=libc++)
      add_link_options(-stdlib=libc++ -lc++abi)
  else()
      # GCC or other: suppress warnings only
      add_compile_options(-w)
  endif()

  # Sanitizer flags: raw VDEPS_SANITIZE string straight into -fsanitize= so
  # comma-separated combos (e.g. address,undefined) work. Guarded above so this
  # only reaches Clang/clang-cl (plain MSVC + sanitizer is rejected earlier).
  if( VDEPS_SANITIZE_ACTIVE )
      add_compile_options( -fsanitize=${VDEPS_SANITIZE} )
      add_link_options( -fsanitize=${VDEPS_SANITIZE} )
  endif()

  # Mirror vdeps.py's get_windows_llvm_sanitizer_link_args(): lld-link skips the
  # ASan runtime that clang-cl's driver would inject, so force-link it ourselves.
  if( VDEPS_SANITIZE_ACTIVE AND WIN32 AND VDEPS_SANITIZE MATCHES "address" )
      execute_process(
          COMMAND "${CMAKE_CXX_COMPILER}" /clang:-print-resource-dir
          OUTPUT_VARIABLE _vh_clang_resource_dir
          OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if( _vh_clang_resource_dir )
          file( TO_CMAKE_PATH "${_vh_clang_resource_dir}/lib/windows" _vh_asan_lib_dir )
          file( GLOB _vh_asan_thunk_libs "${_vh_asan_lib_dir}/clang_rt.asan_static_runtime_thunk-*.lib" )
          file( GLOB _vh_asan_dynamic_libs "${_vh_asan_lib_dir}/clang_rt.asan_dynamic-*.lib" )
          if( _vh_asan_thunk_libs AND _vh_asan_dynamic_libs )
              list( GET _vh_asan_thunk_libs 0 _vh_asan_thunk_lib )
              list( GET _vh_asan_dynamic_libs 0 _vh_asan_dynamic_lib )
              get_filename_component( _vh_asan_thunk_name "${_vh_asan_thunk_lib}" NAME )
              get_filename_component( _vh_asan_dynamic_name "${_vh_asan_dynamic_lib}" NAME )
              add_link_options(
                  "/libpath:${_vh_asan_lib_dir}"
                  "/wholearchive:${_vh_asan_thunk_name}"
                  "${_vh_asan_dynamic_name}"
              )
          endif()
      endif()
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
 
  # vdeps (dependency manager) - only when VRHI_BUILD_VDEPS is ON
  if(VRHI_BUILD_VDEPS)
      add_subdirectory(vdeps)
  endif()
 
# SPIRV-Reflect (from Vulkan SDK)
find_path(SPIRV_REFLECT_DIR NAMES spirv_reflect.c
    HINTS 
        "$ENV{VULKAN_SDK}/source/SPIRV-Reflect"
        "$ENV{VULKAN_SDK}/Source/SPIRV-Reflect"
        "$ENV{VULKAN_SDK}/include/SPIRV-Reflect"
        "${Vulkan_INCLUDE_DIR}/../source/SPIRV-Reflect"
        "${Vulkan_INCLUDE_DIR}/../Source/SPIRV-Reflect"
        "${Vulkan_INCLUDE_DIR}/SPIRV-Reflect"
)

if(NOT SPIRV_REFLECT_DIR)
    message(FATAL_ERROR "SPIRV-Reflect not found in Vulkan SDK")
endif()

set(SPIRV_REFLECT_SOURCE "${SPIRV_REFLECT_DIR}/spirv_reflect.c")
set(SPIRV_REFLECT_HEADER "${SPIRV_REFLECT_DIR}/spirv_reflect.h")

# SPIRV-Tools headers (from vdeps or Vulkan SDK)
find_path(SPIRV_TOOLS_INCLUDE_DIR NAMES spirv-tools/optimizer.hpp
    HINTS
        "${CMAKE_CURRENT_SOURCE_DIR}/vdeps/SPIRV-Tools/include"
        "$ENV{VULKAN_SDK}/include"
        "${Vulkan_INCLUDE_DIR}"
)

if(NOT SPIRV_TOOLS_INCLUDE_DIR)
    message(FATAL_ERROR "SPIRV-Tools headers not found. Please run vdeps.py or install Vulkan SDK.")
endif()

# Derive Vulkan SDK lib dir for hints
get_filename_component( VULKAN_SDK_LIB_DIR "${Vulkan_LIBRARY}" DIRECTORY )

# Slang (from Vulkan SDK, fallback to bundled)
find_path(SLANG_INCLUDE_DIR NAMES slang.h
    HINTS
        "$ENV{VULKAN_SDK}/include/slang"
        "${Vulkan_INCLUDE_DIR}/slang"
)

find_library(SLANG_LIBRARY NAMES slang slang.lib libslang.so libslang.dylib
    HINTS
        "${VULKAN_SDK_LIB_DIR}"
        "$ENV{VULKAN_SDK}/lib"
        "$ENV{VULKAN_SDK}/Lib"
        "${VRHI_LIB_DIR}"
    NO_DEFAULT_PATH
)

if(NOT SLANG_INCLUDE_DIR OR NOT SLANG_LIBRARY)
    message(FATAL_ERROR "Slang not found. Please install Vulkan SDK 1.3.275 or newer.")
else()
    message(STATUS "Using Slang from Vulkan SDK: ${SLANG_LIBRARY}")
endif()

# SPIRV-Tools (built by vdeps - for SPIRV optimisation)
# Use per-config imported targets for multi-config generators (Visual Studio).

# Determine platform tag for local lib paths
if( WIN32 )
    if( VDEPS_USE_LLVM )
        set( VRHI_PLATFORM_TAG "win_llvm" )
    else()
        set( VRHI_PLATFORM_TAG "win" )
    endif()
elseif( UNIX AND NOT APPLE )
    set( VRHI_PLATFORM_TAG "linux" )
else()
    set( VRHI_PLATFORM_TAG "mac" )
endif()

# Set up per-config library paths
# vdeps.py builds to lib/${VRHI_PLATFORM_TAG}_debug and lib/${VRHI_PLATFORM_TAG}_release
# For MD builds, use lib/${VRHI_PLATFORM_TAG}_md_debug and lib/${VRHI_PLATFORM_TAG}_md_release
set(_spirv_lib_platform "${VRHI_PLATFORM_TAG}")
if(MSVC AND VDEPS_DYNAMIC_RUNTIME)
    set(_spirv_lib_platform "${_spirv_lib_platform}_md")
endif()
if( _VH_SANITIZE_TAG )
    set( _spirv_lib_platform "${_spirv_lib_platform}_${_VH_SANITIZE_TAG}" )
endif()
set( SPIRV_TOOLS_DEBUG_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/${_spirv_lib_platform}_debug" CACHE PATH "Path to SPIRV-Tools debug libraries" )
set( SPIRV_TOOLS_RELEASE_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/${_spirv_lib_platform}_release" CACHE PATH "Path to SPIRV-Tools release libraries" )

if( WIN32 )
    # Windows: use .lib files with per-config IMPORTED_LOCATION
    # Debug builds use lib/win_debug, Release builds use lib/win_release
    add_library( SPIRV-Tools-opt STATIC IMPORTED )
    set_target_properties( SPIRV-Tools-opt PROPERTIES
        IMPORTED_LOCATION "${SPIRV_TOOLS_RELEASE_LIB_DIR}/SPIRV-Tools-opt.lib"
        IMPORTED_LOCATION_DEBUG "${SPIRV_TOOLS_DEBUG_LIB_DIR}/SPIRV-Tools-opt.lib"
    )
    add_library( SPIRV-Tools STATIC IMPORTED )
    set_target_properties( SPIRV-Tools PROPERTIES
        IMPORTED_LOCATION "${SPIRV_TOOLS_RELEASE_LIB_DIR}/SPIRV-Tools.lib"
        IMPORTED_LOCATION_DEBUG "${SPIRV_TOOLS_DEBUG_LIB_DIR}/SPIRV-Tools.lib"
    )
    
    message( STATUS "Using SPIRV-Tools-opt (imported): Debug=${SPIRV_TOOLS_DEBUG_LIB_DIR}/SPIRV-Tools-opt.lib, Release=${SPIRV_TOOLS_RELEASE_LIB_DIR}/SPIRV-Tools-opt.lib" )
    message( STATUS "Using SPIRV-Tools (imported): Debug=${SPIRV_TOOLS_DEBUG_LIB_DIR}/SPIRV-Tools.lib, Release=${SPIRV_TOOLS_RELEASE_LIB_DIR}/SPIRV-Tools.lib" )
else()
    # Linux/macOS: find .a or .so files (fallback to find_library for SDK paths)
    find_library( SPIRV_TOOLS_OPT_LIBRARY NAMES SPIRV-Tools-opt
        HINTS 
            "${SPIRV_TOOLS_RELEASE_LIB_DIR}"
            "${SPIRV_TOOLS_DEBUG_LIB_DIR}"
            "${VULKAN_SDK_LIB_DIR}"
    )
    find_library( SPIRV_TOOLS_LIBRARY NAMES SPIRV-Tools
        HINTS 
            "${SPIRV_TOOLS_RELEASE_LIB_DIR}"
            "${SPIRV_TOOLS_DEBUG_LIB_DIR}"
            "${VULKAN_SDK_LIB_DIR}"
    )
    
    if( NOT SPIRV_TOOLS_OPT_LIBRARY OR NOT SPIRV_TOOLS_LIBRARY )
        message( FATAL_ERROR "SPIRV-Tools not found. Please run vdeps.py or install Vulkan SDK." )
    endif()
    
    message( STATUS "Using SPIRV-Tools-opt: ${SPIRV_TOOLS_OPT_LIBRARY}" )
    message( STATUS "Using SPIRV-Tools: ${SPIRV_TOOLS_LIBRARY}" )
endif()
 
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
     ${GENERATED_HEADER}
     src/vrhi_internal.h
     src/vrhi_utils.h
     src/vrhi_backend.h
     src/vrhi_null.h
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
 
 if(APPLE)
     list(APPEND VRHI_SOURCES src/vrhi_device.mm)
 endif()
 
  add_library(vrhi STATIC ${VRHI_SOURCES})
  
  # Set output name based on runtime and config for combined packages
  # Format: vrhi[_md][_d] where _md = dynamic runtime, _d = debug
set(_vrhi_output_name "vrhi")
   if(MSVC AND VDEPS_DYNAMIC_RUNTIME)
       set(_vrhi_output_name "${_vrhi_output_name}_md")
   endif()
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(_vrhi_output_name "${_vrhi_output_name}d")
  endif()
  set_target_properties(vrhi PROPERTIES OUTPUT_NAME "${_vrhi_output_name}")
  
  if(VRHI_BUILD_VDEPS)
      add_dependencies(vrhi vdeps_all)
  endif()
 
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
    "${SLANG_INCLUDE_DIR}"
)
 target_include_directories(vrhi PRIVATE
    "${SPIRV_REFLECT_DIR}"
    "${Vulkan_INCLUDE_DIR}"
    "${SPIRV_TOOLS_INCLUDE_DIR}"
)
 
 # Library search paths
 target_link_directories(vrhi PUBLIC "${VRHI_LIB_DIR}")
 
# Link dependencies
target_link_libraries(vrhi PUBLIC Vulkan::Vulkan ${SLANG_LIBRARY})

# Link SPIRV-Tools (needed for vhOptimiseSpirv in vrhi_shader.cpp)
# PUBLIC because vrhi is STATIC - downstream executables need to resolve these symbols.
# Uses imported targets with per-config library paths for multi-config generators.
if( WIN32 )
    target_link_libraries( vrhi PUBLIC SPIRV-Tools-opt SPIRV-Tools )
else()
    target_link_libraries( vrhi PUBLIC ${SPIRV_TOOLS_OPT_LIBRARY} ${SPIRV_TOOLS_LIBRARY} )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(vrhi PUBLIC X11 Xi Xext)
endif()

if(APPLE)
    target_link_libraries(vrhi PUBLIC "-framework QuartzCore")
endif()
 
 if(WIN32)
     target_link_libraries(vrhi PUBLIC dxgi shlwapi)
 endif()
 
 # -----------------------------------------------------------------------------
 # Test Executable
 # -----------------------------------------------------------------------------
 option(VRHI_BUILD_TESTS "Build VRHI tests" ON)
 option(VRHI_BUILD_EXAMPLES "Build VRHI examples" OFF)
 
 if(VRHI_BUILD_TESTS)
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
         test/test_rt.cpp
         test/test_bindless.cpp
         test/test_native.cpp
         test/test_window.cpp
         test/test_null_device.cpp
         test/test_psocache.cpp
         test/test_bench.cpp
         test/test_precompile.cpp
     )
 
     add_executable(vrhi_test ${TEST_SOURCES})
 
     target_link_libraries(vrhi_test PRIVATE vrhi)
     target_include_directories(vrhi_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vdeps/RGFW" "${SPIRV_TOOLS_INCLUDE_DIR}")
 endif()
 
 # NVRHI and related libraries (shared by tests and examples)
 set(NVRHI_LIBS nvrhi_vk nvrhi vk-bootstrap rtxmu)
 
 # Helper function to link NVRHI and platform dependencies
 function(vrhi_link_dependencies target)
     if(WIN32)
         target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
         target_link_libraries(${target} PRIVATE ${NVRHI_LIBS})
     elseif(APPLE)
         target_link_libraries(${target} PRIVATE
             "-framework CoreFoundation"
             "-framework CoreGraphics"
             "-framework Foundation"
             "-framework AppKit"
             ${NVRHI_LIBS}
             Vulkan::Vulkan
         )
     else() # Linux
         target_link_libraries(${target} PRIVATE ${NVRHI_LIBS} Vulkan::Vulkan)
     endif()
 endfunction()
 
 if(VRHI_BUILD_TESTS)
     vrhi_link_dependencies(vrhi_test)
     
     # -----------------------------------------------------------------------------
     # Testing
     # -----------------------------------------------------------------------------
     enable_testing()
     add_test(NAME vrhi_test COMMAND vrhi_test)
     if( VDEPS_SANITIZE_ACTIVE )
         set( _vh_test_env
             "ASAN_OPTIONS=halt_on_error=0"
             "UBSAN_OPTIONS=print_stacktrace=1"
             "TSAN_OPTIONS=halt_on_error=0 ignore_noninstrumented_modules=1"
         )
         # lld-link doesn't rpath the ASan runtime DLL it force-links, so prepend
         # its directory to PATH for the test. ; inside the PATH value is escaped
         # (\;) so CMake's ;-list ENVIRONMENT property keeps it as one element.
         if( _vh_asan_lib_dir )
             file( TO_NATIVE_PATH "${_vh_asan_lib_dir}" _vh_asan_lib_dir_native )
             string( REPLACE ";" "\;" _vh_parent_path "$ENV{PATH}" )
             list( APPEND _vh_test_env "PATH=${_vh_asan_lib_dir_native}\;${_vh_parent_path}" )
         endif()
         set_tests_properties( vrhi_test PROPERTIES ENVIRONMENT "${_vh_test_env}" )
     endif()
 endif()
 
 # -----------------------------------------------------------------------------
 # Examples
 # -----------------------------------------------------------------------------
 if(VRHI_BUILD_EXAMPLES)
     add_executable(example_hello_world examples/hello_world.cpp)
     target_link_libraries(example_hello_world PRIVATE vrhi)
     vrhi_link_dependencies(example_hello_world)
 endif()
 
 # -----------------------------------------------------------------------------
 # Packaging
 # -----------------------------------------------------------------------------
 add_custom_target(package_vrhi
     # Create output directories
     COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/include"
     COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/vrhi_$<CONFIG>/lib"
     
     COMMAND ${CMAKE_COMMAND} -E copy_if_different 
         "${CMAKE_SOURCE_DIR}/vrhi.h" 
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
          -DCMAKE_GENERATOR=${CMAKE_GENERATOR}
          -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
          -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
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
