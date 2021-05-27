CMAKE_MINIMUM_REQUIRED (VERSION 2.6)
PROJECT (auto_tmpdir C)

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

FIND_PATH(SLURM_INCLUDE_DIR NAMES slurm/slurm.h)
FIND_LIBRARY(SLURM_LIBRARY NAMES libslurm.so)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(SLURM DEFAULT_MSG SLURM_LIBRARY SLURM_INCLUDE_DIR)
IF (NOT SLURM_FOUND)
	MESSAGE(FATAL_ERROR "SLURM library could not be found")
ELSE (NOT SLURM_FOUND)
	SET (SLURM_LIBRARIES ${SLURM_LIBRARY})
	SET (SLURM_INCLUDE_DIRS ${SLURM_INCLUDE_DIR})
	FIND_PATH(SLURM_MODULES_DIR NAMES lib/slurm DOC "Directory containing SLURM extensions.")
	IF (NOT SLURM_MODULES_DIR)
		MESSAGE(FATAL_ERROR "SLURM extensions directory could not be found")
	ENDIF (NOT SLURM_MODULES_DIR)
ENDIF (NOT SLURM_FOUND)
MARK_AS_ADVANCED (SLURM_LIBRARIES SLURM_INCLUDE_DIRS)

#
# Extension on shared library?
#
SET (SHARED_LIB_SUFFIX ".so" CACHE STRING "String to add to end of shared library name")

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

OPTION(AUTO_TMPDIR_NO_GID_CHOWN "Do not set the owner gid on per-job temporary directories (enable by default in Slurm releases < 20)" OFF)

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
INSTALL (TARGETS auto_tmpdir DESTINATION ${SLURM_MODULES_DIR}/lib/slurm)

