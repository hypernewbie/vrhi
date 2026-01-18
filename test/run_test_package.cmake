# Test driver for packaged VRHI
# Configures, builds, and runs the standalone test project

# Validate inputs
if(NOT CONFIG)
    message(FATAL_ERROR "CONFIG not specified (Debug or Release)")
endif()

if(NOT BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR not specified")
endif()

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR not specified")
endif()

# File paths
set(PACKAGE_PATH "${BINARY_DIR}/vrhi_${CONFIG}")
set(TEST_BUILD_DIR "${BINARY_DIR}/test_pkg_build_${CONFIG}")

file(MAKE_DIRECTORY "${TEST_BUILD_DIR}")

# Check package exists
if(NOT EXISTS "${PACKAGE_PATH}/include/vrhi.h")
    message(FATAL_ERROR "Package not found at: ${PACKAGE_PATH}/include/vrhi.h")
endif()

# ========= Step 1: Configure test project =========
message(STATUS "[test_package] Configuring for ${CONFIG}...")

# Set MSVC runtime library based on config
if(CONFIG STREQUAL "Debug")
    set(MSVC_RUNTIME "MultiThreadedDebug")
else()
    set(MSVC_RUNTIME "MultiThreaded")
endif()

# Build cmake configure command with optional generator/compiler passthrough
set(CONFIGURE_ARGS
    -S "${SOURCE_DIR}/test"
    -B "${TEST_BUILD_DIR}"
    -DVRHI_PACKAGE_PATH:PATH=${PACKAGE_PATH}
    -DCMAKE_BUILD_TYPE:STRING=${CONFIG}
    -DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=${MSVC_RUNTIME}
)

# Pass through generator if specified
if(DEFINED CMAKE_GENERATOR)
    list(APPEND CONFIGURE_ARGS -G "${CMAKE_GENERATOR}")
endif()

# Pass through C/C++ compilers if specified
if(DEFINED CMAKE_C_COMPILER)
    list(APPEND CONFIGURE_ARGS -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER})
endif()
if(DEFINED CMAKE_CXX_COMPILER)
    list(APPEND CONFIGURE_ARGS -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER})
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} ${CONFIGURE_ARGS}
    RESULT_VARIABLE config_result
    OUTPUT_QUIET
    ERROR_VARIABLE config_error
)

if(config_result AND NOT config_result EQUAL 0)
    message(FATAL_ERROR "[test_package] Configuration failed:\n${config_error}")
endif()

# ========= Step 2: Build test executable =========
message(STATUS "[test_package] Building for ${CONFIG}...")

execute_process(
    COMMAND ${CMAKE_COMMAND}
        --build "${TEST_BUILD_DIR}"
        --config "${CONFIG}"
        --parallel
    RESULT_VARIABLE build_result
    OUTPUT_QUIET
    ERROR_VARIABLE build_error
)

if(build_result AND NOT build_result EQUAL 0)
    message(FATAL_ERROR "[test_package] Build failed:\n${build_error}")
endif()

# ========= Step 3: Run test executable =========
message(STATUS "[test_package] Running test for ${CONFIG}...")

# Platform-specific executable path
# Multi-config generators (MSVC) put exe in CONFIG subdir
# Single-config generators (Ninja, Make) put it at top level
if(WIN32)
    set(TEST_EXE "${TEST_BUILD_DIR}/${CONFIG}/test_package_exe.exe")
else()
    # Try single-config first, fall back to multi-config
    if(EXISTS "${TEST_BUILD_DIR}/test_package_exe")
        set(TEST_EXE "${TEST_BUILD_DIR}/test_package_exe")
    else()
        set(TEST_EXE "${TEST_BUILD_DIR}/${CONFIG}/test_package_exe")
    endif()
endif()

if(NOT EXISTS "${TEST_EXE}")
    message(FATAL_ERROR "[test_package] Test executable not found: ${TEST_EXE}")
endif()

# Set library path for shared libs (slang, slang-rt) on Unix
if(APPLE)
    set(ENV{DYLD_LIBRARY_PATH} "${PACKAGE_PATH}/lib:$ENV{DYLD_LIBRARY_PATH}")
elseif(UNIX)
    set(ENV{LD_LIBRARY_PATH} "${PACKAGE_PATH}/lib:$ENV{LD_LIBRARY_PATH}")
endif()

execute_process(
    COMMAND "${TEST_EXE}"
    RESULT_VARIABLE test_result
    OUTPUT_QUIET
    ERROR_QUIET
)

# ========= Step 4: Report results =========
if(test_result EQUAL 0)
    message(STATUS "[test_package] PASSED for ${CONFIG}")
else()
    if(test_result EQUAL 1)
        set(ERROR_DESC "vhInit failed")
    elseif(test_result EQUAL 2)
        set(ERROR_DESC "Buffer test failed")
    elseif(test_result EQUAL 3)
        set(ERROR_DESC "Texture test failed")
    elseif(test_result EQUAL 4)
        set(ERROR_DESC "Shader compilation failed")
    else()
        set(ERROR_DESC "Unknown error (exit ${test_result})")
    endif()
    
    message(FATAL_ERROR "[test_package] FAILED for ${CONFIG}: ${ERROR_DESC}")
endif()