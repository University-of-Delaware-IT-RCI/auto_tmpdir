/*
 * fs-utils.h
 *
 * Filesystem utility routines.
 *
 */

#ifndef __AUTO_TMPDIR_FS_UTILS_H__
#define __AUTO_TMPDIR_FS_UTILS_H__

#include "auto_tmpdir_config.h"

/*
 * @enum auto_tmpdir options
 *
 * Options that affect how the fs-utils infrastructure works.
 *
 * @constant auto_tmpdir_fs_options_should_use_per_host
 *     Create per-host subdirectories (e.g. good for shared TMPDIR)
 * @constant auto_tmpdir_fs_options_should_use_shared
 *     Create directories under the defined shared storage root
 * @constant auto_tmpdir_fs_options_should_not_delete
 *     Do not delete directories we create in the epilog
 * @constant auto_tmpdir_fs_options_should_not_map_dev_shm
 *     Do not create a bind-mounted /dev/shm
 */
enum {
    auto_tmpdir_fs_options_should_use_per_host      = 1 << 0,
    auto_tmpdir_fs_options_should_use_shared        = 1 << 1,
    auto_tmpdir_fs_options_should_not_delete        = 1 << 2,
    auto_tmpdir_fs_options_should_not_map_dev_shm   = 1 << 3
};
/*
 * @typedef auto_tmpdir_fs_options_t
 *
 * Type of the auto_tmpdir options.
 */
typedef uint32_t auto_tmpdir_fs_options_t;

/*
 * @typedef auto_tmpdir_fs_ref
 *
 * Type of a reference to an auto_tmpdir_fs directory hierarchy.
 */
typedef struct auto_tmpdir_fs * auto_tmpdir_fs_ref;

/*
 * @function auto_tmpdir_fs_init
 *
 * Create a new directory hierarchy.  Job info comes from the SPANK context and
 * any options from plugstack.conf are passed in argc and argv.
 *
 * Returns NULL on error, and slurm_error() is used to log any errors.
 */
auto_tmpdir_fs_ref auto_tmpdir_fs_init(spank_t spank_ctxt, int argc, char* argv[], auto_tmpdir_fs_options_t options);

/*
 * @function auto_tmpdir_fs_bind_mount
 *
 * Attempt to bind-mount all of the directories in the hierarchy contained in
 * fs_info.
 *
 * Returns 0 on success.  Any errors will be logged via slurm_error().
 */
int auto_tmpdir_fs_bind_mount(auto_tmpdir_fs_ref fs_info);

/*
 * @function auto_tmpdir_fs_get_tmpdir
 *
 * If the hierarchy contained in fs_info has a tmpdir set in it, that path
 * will be returned as a C string pointer.  The pointer is owned by fs_info
 * and should not be free'd by the caller.
 */
const char* auto_tmpdir_fs_get_tmpdir(auto_tmpdir_fs_ref fs_info);

/*
 * @function auto_tmpdir_fs_fini
 *
 * Destroy the directory hierarchy contained in fs_info (if should_dealloc_only
 * is 0) and deallocate all data structures.
 *
 * Returns 0 if successful, non-zero otherwise.  Error messages will be logged
 * via slurm_error().
 */
int auto_tmpdir_fs_fini(auto_tmpdir_fs_ref fs_info, int should_dealloc_only);

/*
 * @function auto_tmpdir_fs_serialize_to_file
 *
 * Serialize the fs_info hierarchy to a file on disk.  If filepath is NULL then
 * a default filepath manufactured from the job info will be used.
 *
 * Reurns 0 on success.  If not successful, error messages will be logged via
 * slurm_error().
 */
int auto_tmpdir_fs_serialize_to_file(auto_tmpdir_fs_ref fs_info, spank_t spank_ctxt, int argc, char* argv[], const char *filepath);

/*
 * @function auto_tmpdir_fs_init_with_file
 *
 * Reconstitute a directory hierarchy from a file on disk.  If the filepath is
 * NULL then the default filepath manufactured from the job info is used.
 *
 * If remove_state_file is non-zero, then the file will be deleted after the
 * hierarchy has been read-in.
 *
 * Returns NULL on error, and slurm_error() is used to log any errors.
 */
auto_tmpdir_fs_ref auto_tmpdir_fs_init_with_file(spank_t spank_ctxt, int argc, char* argv[], auto_tmpdir_fs_options_t options, const char *filepath, int remove_state_file);

/*
 * @function auto_tmpdir_mkdir_recurse
 *
 * A recursive mkdir function.  All component paths leading down to path will be
 * created if they do not exist with the given permissions mode.
 *
 * If should_set_owner is non-zero, all created directories will have their uid/gid
 * set to u_owner/g_owner.
 *
 * Returns 0 if successful.
 */
int auto_tmpdir_mkdir_recurse(const char *path, mode_t mode, int should_set_owner, uid_t u_owner, gid_t g_owner);

/*
 * @function auto_tmpdir_rmdir_recurse
 *
 * A recursive rmdir function.  All files and directories under path are removed
 * before path itself is removed.  If should_remove_children_only is non-zero,
 * the path directory is not removed.
 *
 * Returns 0 if successful.
 */
int auto_tmpdir_rmdir_recurse(const char *path, int should_remove_children_only);

#endif /* __AUTO_TMPDIR_FS_UTILS_H__ */
