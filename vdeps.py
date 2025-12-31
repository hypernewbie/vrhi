import os
import subprocess
import shutil
import glob
import sys
import platform

# --- Configuration ---

CONFIGS = [
    {"name": "debug",   "type": "Debug"},
    {"name": "release", "type": "Release"}
]

IS_WINDOWS = (sys.platform == 'win32')
PLATFORM_TAG = 'win' if IS_WINDOWS else 'linux'
LIB_EXT = '.lib' if IS_WINDOWS else '.a'

# --- Helpers ---

def get_platform_cmake_args():
    """Returns CMake arguments specific to the current operating system."""
    common_args = [
        "-DCMAKE_CXX_STANDARD=20",
        "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
    ]

    if IS_WINDOWS:
        # Windows-specific flags (MSVC default)
        return common_args + [
            "-DVK_USE_PLATFORM_WIN32_KHR=ON",
            # Enforce static runtime (MT/MTd)
            # This requires CMake 3.15+ and CMP0091 set to NEW
            "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>"
        ]
    else:
        # Linux-specific flags (Clang + Ninja + libc++)
        return common_args + [
            "-G", "Ninja",
            "-DCMAKE_C_COMPILER=clang",
            "-DCMAKE_CXX_COMPILER=clang++",
            "-DCMAKE_C_FLAGS=-w",
            "-DCMAKE_CXX_FLAGS=-w -stdlib=libc++",
            "-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -lc++abi",
            "-DCMAKE_SHARED_LINKER_FLAGS=-stdlib=libc++ -lc++abi",
        ]

def run_command(command, cwd=None, env=None):
    """Executes a shell command and exits on failure."""
    cwd_path = cwd or os.getcwd()
    print(f"[{cwd_path}] Running: {' '.join(command)}")
    result = subprocess.run(command, cwd=cwd, env=env, shell=IS_WINDOWS)
    if result.returncode != 0:
        print(f"Error: Command failed with return code {result.returncode}")
        sys.exit(1)

class Dependency:
    def __init__(self, name, rel_path, cmake_options, libs=None, executables=None, init_submodules=False):
        """
        :param name: Display name.
        :param rel_path: Relative path from 'dependencies/'.
        :param cmake_options: List of dependency-specific CMake flags.
        :param libs: List of library base names to copy (e.g. ['nvrhi']). 
                     Matches 'libnvrhi.a' (Linux) or 'nvrhi.lib'/'libnvrhi.lib' (Windows).
                     If None, copies all static libs found.
        :param executables: List of executable base names to copy (e.g. ['nvrhi-scomp']).
                            Matches 'nvrhi-scomp' (Linux) or 'nvrhi-scomp.exe' (Windows).
        :param init_submodules: Whether to init git submodules.
        """
        self.name = name
        self.rel_path = rel_path
        self.cmake_options = cmake_options
        self.libs = libs
        self.executables = executables
        self.init_submodules = init_submodules

# --- Definition of Dependencies ---

DEPENDENCIES = [
    Dependency(
        name="nvrhi",
        rel_path="nvrhi",
        init_submodules=True,
        cmake_options=[
            "-DNVRHI_INSTALL=OFF",
            "-DNVRHI_WITH_RTXMU=ON",
            "-DNVRHI_WITH_VALIDATION=ON",
            "-DNVRHI_WITH_VULKAN=ON",
            "-DNVRHI_WITH_DX11=OFF",
            "-DNVRHI_WITH_DX12=OFF",
            "-DNVRHI_WITH_NVAPI=OFF",
            "-DNVRHI_BUILD_SHARED=OFF",
            "-DRTXMU_WITH_D3D12=OFF",
            "-DRTXMU_WITH_VULKAN=ON",
            "-DVULKAN_HEADERS_ENABLE_INSTALL=OFF",
        ],
        # Base names of libraries to extract
        libs=["nvrhi_vk", "rtxmu", "nvrhi"]
    ),
    Dependency(
        name="ShaderMake",
        rel_path="ShaderMake",
        init_submodules=True,
        cmake_options=[
            "-DSHADERMAKE_FIND_DXC=OFF",
            "-DSHADERMAKE_FIND_DXC_VK=OFF",
            "-DSHADERMAKE_FIND_FXC=OFF",
            "-DSHADERMAKE_FIND_SLANG=ON",
            "-DSHADERMAKE_TOOL=OFF"
        ],
        executables=["ShaderMake"]
    ),
    Dependency(
        name="vk-bootstrap",
        rel_path="vk-bootstrap",
        init_submodules=False,
        cmake_options=[
            "-DBUILD_SHARED_LIBS=OFF",
            "-DGLFW_BUILD_TESTS=OFF",
            "-DVK_BOOTSTRAP_TEST=OFF",
            "-DVK_BOOTSTRAP_DISABLE_WARNINGS=ON",
            "-DVK_BOOTSTRAP_INSTALL=OFF",
            "-DUSE_MSVC_RUNTIME_LIBRARY_DLL=OFF"
        ],
        # If None, it grabs everything, but we can be specific
        libs=["vk-bootstrap"] 
    )
]

# --- Main Build Logic ---

def main():
    root_dir = os.path.dirname(os.path.abspath(__file__))
    deps_root_dir = os.path.join(root_dir, "vdeps")
    env = os.environ.copy()

    print(f"Platform: {sys.platform} ({PLATFORM_TAG})")
    
    for dep in DEPENDENCIES:
        dep_dir = os.path.join(deps_root_dir, dep.rel_path)
        
        if not os.path.exists(dep_dir):
            print(f"Error: Directory for {dep.name} not found at {dep_dir}")
            continue

        print(f"\n=== Processing Dependency: {dep.name} ===")

        if dep.init_submodules:
            print(f"--- Initializing Submodules for {dep.name} ---")
            if os.path.exists(os.path.join(dep_dir, ".git")) or os.path.exists(os.path.join(dep_dir, "..", ".git")):
                 run_command(["git", "submodule", "update", "--init", "--recursive"], cwd=dep_dir)
            else:
                 print("Skipping submodule update (not a git repo)")

        for config in CONFIGS:
            # Determine actual CMake build type
            # On Windows, we want RelWithDebInfo instead of Release to get PDBs
            build_type = config['type']
            if IS_WINDOWS and config['name'] == 'release':
                build_type = 'RelWithDebInfo'

            print(f"\n--- Building {dep.name} [{build_type}] ---")
            
            build_dir = os.path.join(dep_dir, f"build_{config['name']}")
            output_lib_dir = os.path.join(root_dir, "lib", f"{PLATFORM_TAG}_{config['name']}")
            output_tools_dir = os.path.join(root_dir, "tools", f"{PLATFORM_TAG}_{config['name']}")

            if not os.path.exists(output_lib_dir):
                os.makedirs(output_lib_dir)
            
            if dep.executables and not os.path.exists(output_tools_dir):
                os.makedirs(output_tools_dir)

            # CMake Configure
            cmake_args = ["cmake", "-S", ".", "-B", build_dir] + \
                         get_platform_cmake_args() + \
                         [f"-DCMAKE_BUILD_TYPE={build_type}"] + \
                         dep.cmake_options
            
            run_command(cmake_args, cwd=dep_dir, env=env)
            
            # CMake Build
            # For Multi-Config generators (like VS), we must specify --config
            build_cmd = ["cmake", "--build", build_dir]
            if IS_WINDOWS:
                build_cmd.extend(["--config", build_type])

            run_command(build_cmd, cwd=dep_dir, env=env)

            # Copy Artifacts (Libs)
            print(f"--- Copying artifacts to {output_lib_dir} ---")
            
            # Find all relevant files in build dir
            extensions = [LIB_EXT]
            if IS_WINDOWS:
                extensions.append(".pdb")
                
            found_files = glob.glob(os.path.join(build_dir, "**", "*"), recursive=True)
            
            # Also search in the 'bin' directory of the dependency if it exists (e.g. ShaderMake)
            bin_dir = os.path.join(dep_dir, "bin")
            if os.path.exists(bin_dir):
                found_files.extend(glob.glob(os.path.join(bin_dir, "**", "*"), recursive=True))

            copied_count = 0
            
            for file_path in found_files:
                if os.path.isdir(file_path): continue
                filename = os.path.basename(file_path)
                name_no_ext = os.path.splitext(filename)[0]
                ext = os.path.splitext(filename)[1]

                # --- Libs ---
                should_copy_lib = False
                if ext in extensions:
                    if dep.libs is None:
                        should_copy_lib = True
                    else:
                        for base_name in dep.libs:
                            if name_no_ext == base_name or name_no_ext == f"lib{base_name}":
                                should_copy_lib = True
                                break
                
                if should_copy_lib:
                    print(f"Copying lib {filename}...")
                    shutil.copy2(file_path, os.path.join(output_lib_dir, filename))
                    copied_count += 1
                
                # --- Executables ---
                if dep.executables:
                    should_copy_exe = False
                    exe_ext = '.exe' if IS_WINDOWS else ''
                    
                    if ext == exe_ext or (IS_WINDOWS and ext == '.pdb'):
                        # Check against executable names
                        # For PDBs on windows, we match the basename of the exe
                        for base_name in dep.executables:
                            if name_no_ext == base_name:
                                should_copy_exe = True
                                break
                    
                    # On Linux, executables have no extension, so we check name match and executable permission (implied by being a build artifact usually, but name match is key)
                    # If extension is empty and we are not on windows, match exact name
                    if not IS_WINDOWS and ext == '':
                         for base_name in dep.executables:
                            if filename == base_name:
                                should_copy_exe = True
                                break

                    if should_copy_exe:
                         print(f"Copying tool {filename}...")
                         shutil.copy2(file_path, os.path.join(output_tools_dir, filename))
                         copied_count += 1

            
            if copied_count == 0:
                print(f"Warning: No artifacts copied for {dep.name} [{config['type']}]")

    print("\n[SUCCESS] All dependencies processed.")

if __name__ == "__main__":
    main()
