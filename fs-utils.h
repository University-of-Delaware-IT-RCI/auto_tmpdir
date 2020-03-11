/*
 * fs-utils.h
 *
 * Filesystem utility routines.
 *
 */

#ifndef __AUTO_TMPDIR_FS_UTILS_H__
#define __AUTO_TMPDIR_FS_UTILS_H__

#include "auto_tmpdir_config.h"

enum {
    auto_tmpdir_fs_options_should_use_per_host      = 1 << 0,
    auto_tmpdir_fs_options_should_use_shared        = 1 << 1,
    auto_tmpdir_fs_options_should_not_delete        = 1 << 2,
    auto_tmpdir_fs_options_should_not_map_dev_shm   = 1 << 3
};
typedef uint32_t auto_tmpdir_fs_options_t;

/**/

typedef struct auto_tmpdir_fs * auto_tmpdir_fs_ref;

/**/

auto_tmpdir_fs_ref auto_tmpdir_fs_init(spank_t spank_ctxt, int argc, char* argv[], auto_tmpdir_fs_options_t options);
int auto_tmpdir_fs_bind_mount(auto_tmpdir_fs_ref fs_info);
int auto_tmpdir_fs_fini(auto_tmpdir_fs_ref fs_info, int should_not_remove_bindpoints);

/**/

int auto_tmpdir_mkdir_recurse(const char *path, mode_t mode, int should_set_owner, uid_t u_owner, gid_t g_owner);
int auto_tmpdir_rmdir_recurse(const char *path, int should_remove_children_only);

#endif /* __AUTO_TMPDIR_FS_UTILS_H__ */
