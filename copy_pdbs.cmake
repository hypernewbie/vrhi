# This script manages PDB files for VRHI packages
# Usage: cmake -DCONFIG=<Config> -DLIB_PATH=<LibPath> -DBINARY_DIR=<BinaryDir> -P copy_pdbs.cmake

set(PACKAGE_LIB_DIR "${BINARY_DIR}/vrhi_${CONFIG}/lib")

if(CONFIG STREQUAL "Debug")
    # For Debug: Copy PDB files if they exist
    set(PDB_FILES
        "${BINARY_DIR}/${CONFIG}/vrhi.pdb"
        "${LIB_PATH}/nvrhi.pdb"
        "${LIB_PATH}/nvrhi_vk.pdb"
        "${LIB_PATH}/vk-bootstrap.pdb"
        "${LIB_PATH}/rtxmu.pdb"
    )
    
    foreach(PDB_FILE ${PDB_FILES})
        if(EXISTS "${PDB_FILE}")
            get_filename_component(PDB_NAME "${PDB_FILE}" NAME)
            configure_file("${PDB_FILE}" "${PACKAGE_LIB_DIR}/${PDB_NAME}" COPYONLY)
            message("Copied ${PDB_NAME}")
        endif()
    endforeach()
else()
    # For Release: Remove any PDB files that were copied with the libraries
    file(GLOB PDB_FILES_IN_PACKAGE "${PACKAGE_LIB_DIR}/*.pdb")
    foreach(PDB_FILE ${PDB_FILES_IN_PACKAGE})
        file(REMOVE "${PDB_FILE}")
        message("Removed ${PDB_FILE}")
    endforeach()
endif()