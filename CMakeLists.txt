CMAKE_MINIMUM_REQUIRED (VERSION 3.6)
PROJECT (auto_tmpdir VERSION 1.0.1 LANGUAGES C)

#
# For finding packages:
#
INCLUDE(CheckIncludeFiles)
INCLUDE(FindPackageHandleStandardArgs)

IF (NOT SLURM_PREFIX)
    SET (SLURM_PREFIX "/usr/local" CACHE PATH "Directory in which SLURM is installed.")
ENDIF (NOT SLURM_PREFIX)
IF (SLURM_PREFIX)
    LIST(APPEND CMAKE_PREFIX_PATH ${SLURM_PREFIX})
ENDIF (SLURM_PREFIX)

#
# Extension on shared library?
#
SET (SHARED_LIB_SUFFIX ".so" CACHE STRING "String to add to end of shared library name")

FIND_PATH(SLURM_INCLUDE_DIR NAMES slurm/slurm.h)
FIND_LIBRARY(SLURM_LIBRARY NAMES libslurm.so)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(SLURM DEFAULT_MSG SLURM_LIBRARY SLURM_INCLUDE_DIR)
IF (NOT SLURM_FOUND)
    MESSAGE(FATAL_ERROR "SLURM library could not be found")
ENDIF (NOT SLURM_FOUND)
SET (SLURM_LIBRARIES ${SLURM_LIBRARY})
SET (SLURM_INCLUDE_DIRS ${SLURM_INCLUDE_DIR})
IF (NOT SLURM_MODULES_DIR) 
    # The plugins directory should be in the same directory as the library:
    GET_FILENAME_COMPONENT(SLURM_LIBRARY_DIR ${SLURM_LIBRARY} DIRECTORY)
    FIND_PATH(
        SLURM_MODULES_DIR
        NAMES slurm/task_none.so
        PATHS "${SLURM_LIBRARY_DIR}"
        DOC "Directory containing SLURM extensions."
    )
    IF (NOT SLURM_MODULES_DIR)
        MESSAGE(FATAL_ERROR "SLURM extensions directory could not be found")
    ENDIF (NOT SLURM_MODULES_DIR)
    SET(SLURM_MODULES_DIR "${SLURM_MODULES_DIR}/slurm")
ENDIF ()
SET(SLURM_MODULES_DIR "${SLURM_MODULES_DIR}" CACHE PATH "Directory containing SLURM modules." FORCE)
MESSAGE(STATUS "SLURM modules directory: ${SLURM_MODULES_DIR}")

#
# Figure out Slurm version (for CPack):
#
TRY_RUN(DOES_SLURM_VERSION_RUN DOES_SLURM_VERSION_COMPILE
        "${CMAKE_CURRENT_BINARY_DIR}/slurm-version-check" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/slurm-version-check.c"
        CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${SLURM_INCLUDE_DIRS}"
        COMPILE_OUTPUT_VARIABLE SLURM_VERSION_COMPILE_OUTPUT
        RUN_OUTPUT_VARIABLE SLURM_VERSION
    )
IF (NOT DOES_SLURM_VERSION_COMPILE)
    MESSAGE(FATAL_ERROR "Unable to compile Slurm version-printing helper.")
ENDIF ()
IF (DOES_SLURM_VERSION_RUN EQUAL "FAILED_TO_RUN")
    MESSAGE(FATAL_ERROR "Unable to execute Slurm version-printing helper.")
ENDIF ()
MESSAGE(STATUS "SLURM version detected as ${SLURM_VERSION}")

#
# Our configurable CMake variables:
#
IF (EXISTS "/dev/shm" )
    SET (AUTO_TMPDIR_DEV_SHM "/dev/shm" CACHE PATH "Directory at which POSIX shm is mounted.")
    SET (AUTO_TMPDIR_DEV_SHM_PREFIX "/dev/shm/slurm-" CACHE PATH "Path to which the Slurm job id will be appended to create a /dev/shm bind mountpoint")
ELSE (EXISTS "/dev/shm")
    SET (AUTO_TMPDIR_DEV_SHM "" CACHE PATH "Directory at which POSIX shm is mounted.")
    SET (AUTO_TMPDIR_DEV_SHM_PREFIX "" CACHE PATH "Path to which the Slurm job id will be appended to create a /dev/shm bind mountpoint")
ENDIF (EXISTS "/dev/shm")

SET (AUTO_TMPDIR_DEFAULT_LOCAL_PREFIX "/tmp/slurm-" CACHE PATH "Path to which the Slurm job id will be appended to create a local directory to hold all bind mountpoints (e.g. /tmp, /var/tmp)")

OPTION (AUTO_TMPDIR_ENABLE_SHARED_TMPDIR "Enable a global shared directory space into which temp directories can be created." OFF)
IF ( AUTO_TMPDIR_ENABLE_SHARED_TMPDIR )
    SET (AUTO_TMPDIR_DEFAULT_SHARED_PREFIX "" CACHE PATH "Path to which the Slurm job id will be appended to create a shared directory to hold all bind mountpoints (e.g. /tmp, /var/tmp)")
ENDIF ( AUTO_TMPDIR_ENABLE_SHARED_TMPDIR )

OPTION(AUTO_TMPDIR_NO_GID_CHOWN "Do not set the owner gid on per-job temporary directories (always enabled for Slurm releases < 20)" OFF)

#
# Generate canned header inclusions, etc.
#
CONFIGURE_FILE(auto_tmpdir_config.h.in auto_tmpdir_config.h)

#
# Build the plugin as a library (that's what it is):
#
ADD_LIBRARY (auto_tmpdir MODULE fs-utils.c auto_tmpdir.c)
TARGET_INCLUDE_DIRECTORIES (auto_tmpdir PUBLIC ${SLURM_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
SET_TARGET_PROPERTIES (auto_tmpdir PROPERTIES PREFIX "" SUFFIX ${SHARED_LIB_SUFFIX} OUTPUT_NAME "auto_tmpdir")
IF (ENABLE_SHARED_STORAGE)
    TARGET_COMPILE_DEFINITIONS (auto_tmpdir PUBLIC WITH_SHARED_STORAGE SHARED_STORAGE_PATH=${SHARED_STORAGE_PATH})
ENDIF (ENABLE_SHARED_STORAGE)
INSTALL (TARGETS auto_tmpdir DESTINATION ${SLURM_MODULES_DIR})

#
# CPack package generation
#
OPTION (AUTO_TMPDIR_RPM_IGNORE_SLURM_VERSION "If enabled the RPM produced by CPack will only depend on some version of the 'slurm' package, not the version compiled against" OFF)
SET(CPACK_PACKAGE_NAME ${PROJECT_NAME})
SET(CPACK_PACKAGE_CONTACT "Jeffrey Frey <jtfrey@users.noreply.github.com>")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Slurm auto_tmpdir SPANK plugin")
SET(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
IF (AUTO_TMPDIR_RPM_IGNORE_SLURM_VERSION)
    IF (NOT AUTO_TMPDIR_RPM_RELEASE)
        SET (AUTO_TMPDIR_RPM_RELEASE "1" CACHE STRING "Release number for RPM packaging")
    ENDIF ()
    SET(CPACK_PACKAGE_RELEASE "${AUTO_TMPDIR_RPM_RELEASE}")
    SET(CPACK_REQUIRE_SLURM_VERSION "")
ELSE ()
    SET(CPACK_PACKAGE_RELEASE "${SLURM_VERSION}")
    SET(CPACK_REQUIRE_SLURM_VERSION " == ${SLURM_VERSION}")
ENDIF ()
SET(CPACK_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
IF (NOT CPACK_GENERATOR)
    SET(CPACK_GENERATOR "RPM")
ENDIF ()
SET(CPACK_PACKAGING_INSTALL_PREFIX ${SLURM_MODULES_DIR})
SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
SET(CPACK_DEBIAN_PACKAGE_DEPENDS "slurmd (${CPACK_REQUIRE_SLURM_VERSION})")
SET(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
SET(CPACK_DEBIAN_PACKAGE_SECTION "admin")
SET(CPACK_RPM_USER_BINARY_SPECFILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.spec")
CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}.spec.in" "${CPACK_RPM_USER_BINARY_SPECFILE}" @ONLY)
INCLUDE(CPack)
