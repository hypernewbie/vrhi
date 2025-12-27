import os
import subprocess
import shutil
import glob

def run_command(command, cwd=None, env=None):
    print(f"Running: {' '.join(command)} in {cwd or os.getcwd()}")
    result = subprocess.run(command, cwd=cwd, env=env)
    if result.returncode != 0:
        print(f"Error: Command failed with return code {result.returncode}")
        sys.exit(1)

def main():
    root_dir = os.path.dirname(os.path.abspath(__file__))
    deps_dir = os.path.join(root_dir, "dependencies")
    nvrhi_dir = os.path.join(deps_dir, "nvrhi")
    
    if not os.path.exists(nvrhi_dir):
        print(f"Error: NVRHI directory not found at {nvrhi_dir}")
        return

    # 1. Initialize Submodules (Recursively)
    print("--- Initializing Submodules ---")
    run_command(["git", "submodule", "update", "--init", "--recursive"], cwd=nvrhi_dir)

    # Configurations to build
    configs = [
        {"name": "debug",   "type": "Debug"},
        {"name": "release", "type": "Release"}
    ]

    # Environment variables for Clang + Ninja
    env = os.environ.copy()
    # Ensure we are using the user's shell environment (Vulkan SDK etc)
    
    for config in configs:
        print(f"\n--- Building NVRHI [{config['type']}] ---")
        
        build_dir = os.path.join(nvrhi_dir, f"build_{config['name']}")
        output_lib_dir = os.path.join(root_dir, "lib", f"linux_{config['name']}")
        
        # Ensure output directory exists
        if not os.path.exists(output_lib_dir):
            os.makedirs(output_lib_dir)

        # CMake Configuration
        cmake_args = [
            "cmake",
            "-S", ".",
            "-B", build_dir,
            "-G", "Ninja",
            "-DCMAKE_C_COMPILER=clang",
            "-DCMAKE_CXX_COMPILER=clang++",
            f"-DCMAKE_BUILD_TYPE={config['type']}",
            "-DCMAKE_CXX_STANDARD=20",
            "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
            
            # User Specified Flags
            "-DNVRHI_INSTALL=OFF",
            "-DNVRHI_WITH_RTXMU=ON",
            "-DNVRHI_WITH_VALIDATION=ON",
            "-DNVRHI_WITH_VULKAN=ON",
            
            # Suppress all warnings and use libc++
            "-DCMAKE_C_FLAGS=-w",
            "-DCMAKE_CXX_FLAGS=-w -stdlib=libc++",
            "-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -lc++abi",
            "-DCMAKE_SHARED_LINKER_FLAGS=-stdlib=libc++ -lc++abi",
            
            # Explicitly Disable Windows/DX stuff
            "-DNVRHI_WITH_DX11=OFF",
            "-DNVRHI_WITH_DX12=OFF",
            "-DNVRHI_WITH_NVAPI=OFF",
            "-DNVRHI_BUILD_SHARED=OFF",
        ]
        
        run_command(cmake_args, cwd=nvrhi_dir, env=env)

        # Build
        run_command(["cmake", "--build", build_dir], cwd=nvrhi_dir, env=env)

        # Copy Artifacts
        print(f"--- Copying artifacts to {output_lib_dir} ---")
        
        # We need to find the built libraries. 
        # Typically they end up in build_dir/ or build_dir/lib/
        # We look for *.a files.
        # NVRHI usually produces: libnvrhi_vk.a (static)
        # RTXMU usually produces: librtxmu.a
        
        # Recursive search for .a files in the build directory
        libs_found = glob.glob(os.path.join(build_dir, "**", "*.a"), recursive=True)
        
        for lib in libs_found:
            filename = os.path.basename(lib)
            # Filter out standard CMake check files if any, though unlikely with glob *.a
            if filename in ["libnvrhi_vk.a", "librtxmu.a", "libnvrhi.a"]:
                print(f"Copying {filename}...")
                shutil.copy2(lib, os.path.join(output_lib_dir, filename))

    print("\n[SUCCESS] Builds complete.")

if __name__ == "__main__":
    import sys
    main()