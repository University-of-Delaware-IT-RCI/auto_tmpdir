/*
 * fs-utils.c
 *
 * Filesystem utility routines.
 *
 */

#include "fs-utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <fts.h>
#include <sched.h>

/**/

#ifdef AUTO_TMPDIR_NO_GID_CHOWN
#   define NEEDS_CHOWN(F,U,G) ((F).st_uid != (U)) 
#   define __auto_tmpdir_chown(P,U,G) (chown((P), (U), -1))
#else
#   define NEEDS_CHOWN(F,U,G) (((F).st_uid != (U)) || ((F).st_gid != (G))) 
#   define __auto_tmpdir_chown(P,U,G) (chown((P), (U), (G)))
#endif

typedef struct auto_tmpdir_fs_bindpoint {
    struct auto_tmpdir_fs_bindpoint *link, *back_link;
    int                 is_bind_mounted, should_always_remove;
    const char          *bind_this_path;
    const char          *to_this_path;
} auto_tmpdir_fs_bindpoint_t;

/**/

auto_tmpdir_fs_bindpoint_t*
auto_tmpdir_fs_bindpoint_alloc(
    const char      *bind_this_path,
    const char      *to_this_path,
    int             should_always_remove
)
{
    auto_tmpdir_fs_bindpoint_t  *new_rec = (auto_tmpdir_fs_bindpoint_t*)malloc(sizeof(auto_tmpdir_fs_bindpoint_t));

    if ( new_rec ) {
        new_rec->link = new_rec->back_link = NULL;
        new_rec->is_bind_mounted = 0;
        new_rec->should_always_remove = should_always_remove;
        new_rec->bind_this_path = bind_this_path;
        new_rec->to_this_path = to_this_path;
    }
    return new_rec;
}

/**/

auto_tmpdir_fs_bindpoint_t*
auto_tmpdir_fs_bindpoint_find_to_path(
    auto_tmpdir_fs_bindpoint_t  *bindpoint,
    const char                  *path_of_interest,
    size_t                      path_of_interest_len
)
{
    while ( bindpoint ) {
        if ( strncmp(bindpoint->to_this_path, path_of_interest, path_of_interest_len) == 0 ) return bindpoint;
        bindpoint = bindpoint->link;
    }
    return NULL;
}

/**/

int
auto_tmpdir_fs_bindpoint_dealloc(
    auto_tmpdir_fs_bindpoint_t  *bindpoint,
    int                         should_not_delete,
    int                         should_dealloc_only
)
{
    int             rc = 0;

    while ( bindpoint) {
        auto_tmpdir_fs_bindpoint_t  *next = bindpoint->link;
        int                         is_okay = 1;

        slurm_debug("auto_tmpdir::auto_tmpdir_fs_bindpoint_dealloc: `%s` -> `%s` (%d|%d) %p", bindpoint->bind_this_path, bindpoint->to_this_path, bindpoint->is_bind_mounted, bindpoint->should_always_remove, next);
        if ( ! should_dealloc_only ) {
            if ( bindpoint->is_bind_mounted ) {
                if ( umount2(bindpoint->to_this_path, MNT_FORCE) != 0 ) {
                    slurm_warning("auto_tmpdir::auto_tmpdir_fs_bindpoint_dealloc: unable to unmount bind point `%s` -> `%s`", bindpoint->to_this_path, bindpoint->bind_this_path);
                    rc = -1;
                    is_okay = 0;
                    /*  Attempt to remove the bound path itself to drop all content: */
                    if ( ! should_dealloc_only && (bindpoint->should_always_remove || ! should_not_delete) ) {
                        slurm_debug("auto_tmpdir::auto_tmpdir_fs_bindpoint_dealloc: failed to unmount, removing content of directory `%s`", bindpoint->to_this_path);
                        auto_tmpdir_rmdir_recurse(bindpoint->to_this_path, 1);
                    }
                } else {
                    bindpoint->is_bind_mounted = 0;
                }
            }
            if ( is_okay ) {
                /* Remove the directory being bind mounted: */
                if ( bindpoint->should_always_remove || ! should_not_delete ) {
                    struct stat         finfo;

                    if ( stat(bindpoint->bind_this_path, &finfo) == 0 ) {
                        slurm_debug("auto_tmpdir::auto_tmpdir_fs_bindpoint_dealloc: removing directory `%s`", bindpoint->bind_this_path);
                        if ( auto_tmpdir_rmdir_recurse(bindpoint->bind_this_path, 0) != 0 ) rc = -1;
                    } else {
                        slurm_debug("auto_tmpdir::auto_tmpdir_fs_bindpoint_dealloc: directory `%s` no longer exists", bindpoint->bind_this_path);
                    }
                }
            }
        }
        
        /* Deallocate this node: */
        free((void*)bindpoint->bind_this_path);
        free((void*)bindpoint->to_this_path);
        free((void*)bindpoint);

        slurm_debug("auto_tmpdir::auto_tmpdir_fs_bindpoint_dealloc: moving to next directory %p", next);
        bindpoint = next;
    }
    return rc;
}

/**/

typedef struct auto_tmpdir_fs {
    auto_tmpdir_fs_options_t    options;
    const char                  *tmpdir;
    const char                  *base_dir, *base_dir_parent;
    auto_tmpdir_fs_bindpoint_t  *bind_mounts, *bind_mounts_tail;
} auto_tmpdir_fs;

/**/

const char*
__auto_tmpdir_fs_get_hostname(void)
{
    static int      is_inited = 0;
    static char     hostname[64];

    if ( ! is_inited ) {
        int         i = 0;

        /* 64 characters should be plenty -- DNS label length maxes at 63 */
        gethostname(hostname, sizeof(hostname));

        /* Find the first dot (.) and NUL-terminate there (or at the end of the buffer): */
        while ( (i < sizeof(hostname)) && (hostname[i] && (hostname[i] != '.')) ) i++;
        hostname[i] = '\0';
        is_inited = 1;
    }
    return hostname;
}

/**/

const char*
__auto_tmpdir_fs_path_create(
    const char                  *prefix,
    auto_tmpdir_fs_options_t    options,
    uint32_t                    job_id
)
{
    const char      *hostname = "";
    size_t          out_path_len = strlen(prefix) + 10 + 1;
    char            *out_path;
    int             has_hostname = 0;

    if ( (options & auto_tmpdir_fs_options_should_use_per_host) == auto_tmpdir_fs_options_should_use_per_host ) {
        hostname = __auto_tmpdir_fs_get_hostname();
        out_path_len += strlen(hostname) + 1;
        has_hostname = 1;
    }
    out_path = malloc(out_path_len);
    if ( ! out_path ) {
        slurm_info("auto_tmpdir: unable to allocate job path relative to `%s`", prefix);
        return NULL;
    }
    if ( has_hostname ) {
        snprintf(out_path, out_path_len, "%1$s%2$u/%3$s", prefix, job_id, hostname);
    } else {
        snprintf(out_path, out_path_len, "%1$s%2$u", prefix, job_id);
    }
    return out_path;
}

/**/

int
__auto_tmpdir_fs_create_bindpoint(
    auto_tmpdir_fs      *fs_info,
    const char          *bind_this_path,
    const char          *to_this_path,
    int                 should_always_remove,
    int                 force_head_of_list,
    uid_t               u_owner,
    gid_t               g_owner
)
{
    struct stat         finfo;

    /*
     * If the directory exists, no need to create it:
     */
    if ( lstat(bind_this_path, &finfo) != 0 ) {
        /*
         * Create the directory:
         */
force_mkdir:
        if ( mkdir(bind_this_path, S_IRWXU) != 0 ) {
            slurm_error("auto_tmpdir::__auto_tmpdir_fs_create_bindpoint: unable to create directory `%s` (%m)", bind_this_path);
            return -1;
        }
        slurm_debug("auto_tmpdir::__auto_tmpdir_fs_create_bindpoint: created directory `%s`", bind_this_path);

        /*
         * Fixup ownership:
         */
force_chown:
        if ( __auto_tmpdir_chown(bind_this_path, u_owner, g_owner) ) {
            slurm_error("auto_tmpdir::__auto_tmpdir_fs_create_bindpoint: unable to fixup ownership on directory `%s` (%m)", bind_this_path);
            auto_tmpdir_rmdir_recurse(bind_this_path, 0);
            return -1;
        }
        slurm_debug("auto_tmpdir::__auto_tmpdir_fs_create_bindpoint: set ownership %d:%d on directory `%s`", u_owner, g_owner, bind_this_path);
    } else if ( ! S_ISDIR(finfo.st_mode) ) {
        slurm_warning("auto_tmpdir::__auto_tmpdir_fs_create_bindpoint: path `%s` exists but is not a directory", bind_this_path);

        /*
         * Attempt to remove the offending file, socket, whatever:
         */
        if ( unlink(bind_this_path) != 0 ) {
            slurm_error("auto_tmpdir::__auto_tmpdir_fs_create_bindpoint: path `%s` is not a directory and could not be removed (%m)", bind_this_path);
            return -1;
        }

        /*
         * Now go back and try to create the directory:
         */
        goto force_mkdir;
    } else if ( NEEDS_CHOWN(finfo, u_owner, g_owner) ) {
        /*
         * Go back and try to change ownership:
         */
        goto force_chown;
    }

    /*
     * Create the bind mount record:
     */
    auto_tmpdir_fs_bindpoint_t      *bindpoint = auto_tmpdir_fs_bindpoint_alloc(bind_this_path, to_this_path, should_always_remove);

    if ( ! bindpoint ) {
        slurm_error("auto_tmpdir::__auto_tmpdir_fs_create_bindpoint: unable to create bind mount record for `%s`", bind_this_path);
        auto_tmpdir_rmdir_recurse(bind_this_path, 0);
        return -1;
    }
    slurm_debug("auto_tmpdir::__auto_tmpdir_fs_create_bindpoint: added bindpoint `%s` -> `%s`", bind_this_path, to_this_path);

    if ( force_head_of_list || (fs_info->base_dir_parent && (strcmp(fs_info->base_dir_parent, to_this_path) == 0)) ) {
        /*
         * Add this bind point at the FRONT of the list, so that it's mounted LAST and unmounted FIRST:
         */
        if ( fs_info->bind_mounts ) {
            bindpoint->link = fs_info->bind_mounts;
            fs_info->bind_mounts->back_link = bindpoint;
            fs_info->bind_mounts = bindpoint;
        } else {
            fs_info->bind_mounts = fs_info->bind_mounts_tail = bindpoint;
        }
    } else {
        if ( fs_info->bind_mounts_tail ) {
            fs_info->bind_mounts_tail->link = bindpoint;
            bindpoint->back_link = fs_info->bind_mounts_tail;
            fs_info->bind_mounts_tail = bindpoint;
        } else {
            fs_info->bind_mounts = fs_info->bind_mounts_tail = bindpoint;
        }
    }
    return 0;
}

/**/

static const char *auto_tmpdir_fs_dev_shm = AUTO_TMPDIR_DEV_SHM;
static const char *auto_tmpdir_fs_dev_shm_prefix = AUTO_TMPDIR_DEV_SHM_PREFIX;
static const char *auto_tmpdir_fs_default_local_prefix = AUTO_TMPDIR_DEFAULT_LOCAL_PREFIX;
static const char *auto_tmpdir_fs_default_shared_prefix = AUTO_TMPDIR_DEFAULT_SHARED_PREFIX;

/**/

auto_tmpdir_fs_ref
auto_tmpdir_fs_init(
    spank_t                     spank_ctxt,
    int                         argc,
    char*                       argv[],
    auto_tmpdir_fs_options_t    options
)
{
    auto_tmpdir_fs              *new_fs;
    int                         should_check_bind_order = 1, i;
    uint32_t                    job_id = NO_VAL;
    uid_t                       u_owner;
    gid_t                       g_owner;
    const char                  *local_prefix = auto_tmpdir_fs_default_local_prefix, *shared_prefix = auto_tmpdir_fs_default_shared_prefix;
    const char                  *tmpdir = NULL;
    int                         rc;
    size_t                      prefix_len;

    /* What user should we function as? */
    if ((rc = spank_get_item (spank_ctxt, S_JOB_UID, &u_owner)) != ESPANK_SUCCESS) {
        slurm_error ("auto_tmpdir: auto_tmpdir_fs_init: unable to get job's user id");
        return NULL;
    }
    
#ifndef AUTO_TMPDIR_NO_GID_CHOWN
    if ((rc = spank_get_item (spank_ctxt, S_JOB_GID, &g_owner)) != ESPANK_SUCCESS) {
        slurm_error ("auto_tmpdir: auto_tmpdir_fs_init: unable to get job's group id");
        return NULL;
    }
#else
    g_owner = -1;
#endif

    /* Get the base job id: */
    if ( (rc = spank_get_item(spank_ctxt, S_JOB_ID, &job_id)) != ESPANK_SUCCESS ) {
        slurm_error("auto_tmpdir: auto_tmpdir_fs_init: no job id associated with job??");
        return NULL;
    }

    slurm_debug("auto_tmpdir::auto_tmpdir_fs_init: %u for owner %d:%d", job_id, u_owner, g_owner);

    /*
     * First pass through the arguments to the plugin -- pull the local and/or shared prefix if present:
     */
    i = 0;
    while ( i < argc ) {
        if ( strncmp(argv[i], "local_prefix=", 13) == 0 ) {
            local_prefix = argv[i] + 13;
            if ( *local_prefix != '/' ) {
                slurm_error("auto_tmpdir::auto_tmpdir_fs_init: invalid local_prefix in plugstack configuration (%s)", local_prefix);
                return NULL;
            }
        }
        else if ( strncmp(argv[i], "shared_prefix=", 14) == 0 ) {
            shared_prefix = argv[i] + 14;
            if ( *shared_prefix != '/' ) {
                slurm_error("auto_tmpdir::auto_tmpdir_fs_init: invalid shared_prefix in plugstack configuration (%s)", shared_prefix);
                return NULL;
            }
        }
        else if ( strncmp(argv[i], "tmpdir=", 7) == 0 ) {
            tmpdir = argv[i] + 7;
            if ( *tmpdir != '/' ) {
                slurm_error("auto_tmpdir::auto_tmpdir_fs_init: invalid tmpdir in plugstack configuration (%s)", tmpdir);
                return NULL;
            }
        }
        else if ( strcmp(argv[i], "no_dev_shm") == 0 ) {
                slurm_debug("auto_tmpdir::auto_tmpdir_fs_init: no_dev_shm set, will not add /dev/shm bind mounts");
            options |= auto_tmpdir_fs_options_should_not_map_dev_shm;
        }
        else if ( strcmp(argv[i], "no_rm_shared_only") == 0 ) {
            if ( (options & auto_tmpdir_fs_options_should_use_shared) != auto_tmpdir_fs_options_should_use_shared ) {
                slurm_debug("auto_tmpdir::auto_tmpdir_fs_init: no_rm_shared_only set, ensuring no should_not_delete bit in options");
                options &= ~auto_tmpdir_fs_options_should_not_delete;
            }
        }
        else if ( strcmp(argv[i], "no_bind_order_check") == 0 ) {
            slurm_debug("auto_tmpdir::auto_tmpdir_fs_init: no_bind_order_check set, will not check bind mount order");
            should_check_bind_order = 0;
        }
        i++;
    }

    slurm_debug("auto_tmpdir::auto_tmpdir_fs_init: local_prefix=%s", local_prefix);
    if ( shared_prefix ) slurm_debug("auto_tmpdir::auto_tmpdir_fs_init: shared_prefix=%s", shared_prefix);
    if ( tmpdir ) slurm_debug("auto_tmpdir::auto_tmpdir_fs_init: tmpdir=%s", tmpdir);

    /*
     * All set:
     */
    if ( (new_fs = (auto_tmpdir_fs*)malloc(sizeof(auto_tmpdir_fs))) ) {
        new_fs->options = options;
        new_fs->tmpdir = tmpdir ? strdup(tmpdir) : NULL;
        new_fs->base_dir = new_fs->base_dir_parent = NULL;
        new_fs->bind_mounts = new_fs->bind_mounts_tail = NULL;

        /*
         * Go through the config arguments and create each mount point specified:
         */
        i = 0;
        while ( i < argc ) {
            if ( strncmp(argv[i], "mount=", 6) == 0 ) {
                const char      *bind_to = argv[i] + 6;
                size_t          bind_to_len = strlen(bind_to);
                
                if ( *bind_to != '/' ) {
                    slurm_error("auto_tmpdir::auto_tmpdir_fs_init: invalid mount in plugstack configuration (%s)", bind_to);
                    goto error_out;
                }
                while ( bind_to_len && (bind_to[bind_to_len - 1] == '/') ) bind_to_len--;
                if ( bind_to_len == 0 ) {
                    slurm_error("auto_tmpdir::auto_tmpdir_fs_init: invalid mount in plugstack configuration (%s)", bind_to);
                    goto error_out;
                }
                
                /*
                 * Make sure we haven't already registered it:
                 */
                if ( auto_tmpdir_fs_bindpoint_find_to_path(new_fs->bind_mounts, bind_to, bind_to_len) ) {
                    slurm_warning("auto_tmpdir::auto_tmpdir_fs_init: ignoring repeated mount in plugstack configuration (%s)", bind_to);
                    i++;
                    continue;
                }

                /*
                 * First time through we need to pick a prefix path and get the parent directory for all
                 * bind mounts created:
                 */
                if ( ! new_fs->base_dir ) {
                    const char      *prefix = local_prefix;

                    if ( (options & auto_tmpdir_fs_options_should_use_shared) == auto_tmpdir_fs_options_should_use_shared ) {
                        if ( ! shared_prefix ) {
                            slurm_error("auto_tmpdir::auto_tmpdir_fs_init: shared tmp directory requested but not configured");
                            goto error_out;
                        }
                        prefix = shared_prefix;
                    }
                    
                    /*
                     * Find the parent directory of the base_dir:
                     */
                    if ( should_check_bind_order ) {
                        const char          *end = prefix + strlen(prefix);
                        char                *base_dir_parent;
                        
                        while ( (end > prefix) ) {
                            if ( *(--end) == '/' ) break;
                        }
                        if ( end <= prefix ) {
                            slurm_error("auto_tmpdir::auto_tmpdir_fs_init: using the root directory is not supported");
                            goto error_out;
                        }
                        base_dir_parent = (char*)malloc(end - prefix + 1);
                        if ( ! base_dir_parent ) {
                            slurm_error("auto_tmpdir::auto_tmpdir_fs_init: unable to allocate base directory parent");
                            goto error_out;
                        }
                        strncpy(base_dir_parent, prefix, (end - prefix));
                        base_dir_parent[end - prefix] = '\0';
                        new_fs->base_dir_parent = (const char*)base_dir_parent;
                    }
                    new_fs->base_dir = __auto_tmpdir_fs_path_create(
                                                            prefix,
                                                            options,
                                                            job_id
                                                        );
                    if ( ! new_fs->base_dir ) {
                        slurm_error("auto_tmpdir::auto_tmpdir_fs_init: unable to allocate base directory");
                        goto error_out;
                    }
                    prefix_len = strlen(new_fs->base_dir);

                    /* Create the parent tmp directory: */
                    if ( auto_tmpdir_mkdir_recurse(new_fs->base_dir, 0700, 1, u_owner, g_owner) ) {
                        slurm_error("auto_tmpdir::auto_tmpdir_fs_init: unable to create base directory `%s`", new_fs->base_dir);
                        goto error_out;
                    }
                }
                
                /*
                 * Make a copy of the mountpoint (to dir):
                 */
                char                    *to_dir = (char*)malloc(bind_to_len + 1);
                
                if ( ! to_dir ) {
                    slurm_error("auto_tmpdir::auto_tmpdir_fs_init: unable to allocate copy of mountpoint for `%s`", bind_to);
                    goto error_out;
                }

                /*
                 * Create a temp directory under the base_dir to hold the bind mountpoint (note that
                 * bind_to leads with a slash, which we'll discard in the directory name we map bind_to
                 * to, so that slash becomes the NUL character in the final path length):
                 */
                char                    *dir_path = (char*)malloc(prefix_len + 1 + bind_to_len);

                if ( ! dir_path ) {
                    slurm_error("auto_tmpdir::auto_tmpdir_fs_init: unable to allocate tmp path template for `%s`", bind_to);
                    free((void*)to_dir);
                    goto error_out;
                }
                strncpy(dir_path, new_fs->base_dir, prefix_len);
                dir_path[prefix_len] = '/';

                int                     i_bind_to = 1, i_dir_path = prefix_len + 1;

                /*
                 * Map any slashes to underscores to flatten the bind_to path to a single name as we
                 * fill-in the rest of dir_path:
                 */
                to_dir[0] = bind_to[0];
                while ( i_bind_to < bind_to_len ) {
                    char        c = bind_to[i_bind_to];
                    to_dir[i_bind_to++] = c;
                    dir_path[i_dir_path++] = ((c == '/') ? '_' : c);
                }
                to_dir[i_bind_to] = '\0';
                dir_path[i_dir_path] = '\0';

                /*
                 * Add the mountpoint:
                 */
                if ( __auto_tmpdir_fs_create_bindpoint(new_fs, dir_path, to_dir, 0, 0, u_owner, g_owner) != 0 ) {
                    free((void*)dir_path);
                    free((void*)to_dir);
                    goto error_out;
                }
            }
            i++;
        }

        /*
         * Attempt to setup a mapped /dev/shm if desired:
         */
        if ( (options & auto_tmpdir_fs_options_should_not_map_dev_shm) != auto_tmpdir_fs_options_should_not_map_dev_shm ) {
            struct stat             finfo;

            if ( stat(auto_tmpdir_fs_dev_shm, &finfo) == 0 ) {
                /*
                 * Create our own /dev/shm space:
                 */
                const char          *dev_shm_dir = __auto_tmpdir_fs_path_create(
                                                            auto_tmpdir_fs_dev_shm_prefix,
                                                            (options & ~auto_tmpdir_fs_options_should_use_per_host),
                                                            job_id
                                                        );
                const char          *to_dir = strdup(auto_tmpdir_fs_dev_shm);
                
                if ( ! dev_shm_dir ) goto error_out;
                if ( ! to_dir ) {
                    slurm_error("auto_tmpdir::auto_tmpdir_fs_init: unable to allocate copy of to path `%s`", auto_tmpdir_fs_dev_shm);
                    goto error_out;
                }

                /*
                 * Add the moundpoint:
                 */
                if ( __auto_tmpdir_fs_create_bindpoint(new_fs, dev_shm_dir, to_dir, 1, 1, u_owner, g_owner) != 0 ) {
                    free((void*)dev_shm_dir);
                    free((void*)to_dir);
                    goto error_out;
                }
            } else {
                slurm_warning("auto_tmpdir::auto_tmpdir_fs_init: shm base directory `%s` does not exist", auto_tmpdir_fs_dev_shm);
                goto error_out;
            }
        }
    }
    return new_fs;

error_out:
    if ( new_fs ) {
        if ( new_fs->bind_mounts ) {
            auto_tmpdir_fs_bindpoint_dealloc(
                    new_fs->bind_mounts,
                    ((new_fs->options & auto_tmpdir_fs_options_should_not_delete) == auto_tmpdir_fs_options_should_not_delete),
                    0
                );
        }
        if ( new_fs->base_dir ) {
            if ( (new_fs->options & auto_tmpdir_fs_options_should_not_delete) != auto_tmpdir_fs_options_should_not_delete ) auto_tmpdir_rmdir_recurse(new_fs->base_dir, 0);
            free((void*)new_fs->base_dir);
        }
        if ( new_fs->base_dir_parent ) free((void*)new_fs->base_dir_parent);
        if ( new_fs->tmpdir ) free((void*)new_fs->tmpdir);
        free((void*)new_fs);
    }
    return NULL;
}


int
auto_tmpdir_fs_bind_mount(
    auto_tmpdir_fs_ref  fs_info
)
{
    int                         rc = 0;
    auto_tmpdir_fs_bindpoint_t  *bindpoint = fs_info->bind_mounts;
    
    if ( bindpoint ) {
        /*
         * Skip ahead to the end, then go in reverse order:
         */
        while ( bindpoint->link ) bindpoint = bindpoint->link;

        /*
         * Allow mount points to be shared into a child namespace:
         */
        if ( mount("", "/", "dontcare", MS_REC | MS_SHARED, "") != 0 ) {
		    slurm_error("auto_tmpdir::auto_tmpdir_fs_bind_mount: failed to mark mountpoints for sharing (%m)");
		    return -1;
		}

		/*
		 * Create a new mount namespace:
		 */
		if ( unshare(CLONE_NEWNS) != 0 ) {
		    slurm_error("auto_tmpdir::auto_tmpdir_fs_bind_mount: failed to create new mount namespace (%m)");
		    return -1;
		}

		/*
		 * Copy parent namespace mounts into this namespace:
		 */
		if ( mount("", "/", "dontcare", MS_REC | MS_SLAVE, "") != 0 ) {
		    slurm_error("auto_tmpdir::auto_tmpdir_fs_bind_mount: failed to copy parent mountpoints into new mount namespace (%m)");
		    return -1;
		}

	    /*
	     * Loop over all our bind mount points:
	     */
	    while ( (rc == 0) && bindpoint ) {
	        if ( ! bindpoint->is_bind_mounted ) {
                slurm_debug("auto_tmpdir::auto_tmpdir_fs_bind_mount: bind-mounting `%s` -> `%s` (pid %d)", bindpoint->bind_this_path, bindpoint->to_this_path, getpid());
                if ( mount(bindpoint->bind_this_path, bindpoint->to_this_path, "none", MS_BIND, NULL) != 0 ) {
                    slurm_error("auto_tmpdir::auto_tmpdir_fs_bind_mount: failed to bind-mount `%s` -> `%s` (%m)", bindpoint->bind_this_path, bindpoint->to_this_path);
                    rc = -1;
                } else {
                    bindpoint->is_bind_mounted = 1;
                }
	        }
	        bindpoint = bindpoint->back_link;
	    }
	}
	return rc;
}


const char*
auto_tmpdir_fs_get_tmpdir(
    auto_tmpdir_fs_ref  fs_info
)
{
    return fs_info->tmpdir ? fs_info->tmpdir : "/tmp";
}


int
auto_tmpdir_fs_fini(
    auto_tmpdir_fs_ref  fs_info,
     int                should_dealloc_only
)
{
    int     rc = 0;

    if ( fs_info ) {
        if ( fs_info->bind_mounts ) {
            int     local_rc = auto_tmpdir_fs_bindpoint_dealloc(
                                        fs_info->bind_mounts,
                                        ((fs_info->options & auto_tmpdir_fs_options_should_not_delete) == auto_tmpdir_fs_options_should_not_delete),
                                        should_dealloc_only
                                    );
            if ( local_rc != 0 ) rc = local_rc;
        }
        if ( fs_info->base_dir ) {
            if ( ! should_dealloc_only && (fs_info->options & auto_tmpdir_fs_options_should_not_delete) != auto_tmpdir_fs_options_should_not_delete ) {
                int local_rc;

                slurm_debug("auto_tmpdir::auto_tmpdir_fs_bindpoint_dealloc: removing directory `%s`", fs_info->base_dir);
                local_rc = auto_tmpdir_rmdir_recurse(fs_info->base_dir, 0);
                if ( local_rc != 0 ) rc = local_rc;
            }
            free((void*)fs_info->base_dir);
        }
        if ( fs_info->base_dir_parent ) free((void*)fs_info->base_dir_parent);
        if ( fs_info->tmpdir ) free((void*)fs_info->tmpdir);
        free((void*)fs_info);
    }
    return rc;
}


/*
 * @function auto_tmpdir_mkdir_recurse
 *
 * Recursively create all directories in a path.
 */
int
auto_tmpdir_mkdir_recurse(
    const char  *path,
    mode_t      mode,
    int         should_set_owner,
    uid_t       u_owner,
    gid_t       g_owner
)
{
    struct stat     finfo;

    if ( ! path || ! *path ) {
        slurm_info("auto_tmpdir::auto_tmpdir_mkdir_recurse: cannot mkdir an empty path");
        return -1;
    }
    if ( stat(path, &finfo) != 0 ) {
        /* There's at least one directory we need to create: */
        size_t      path_len = strlen(path);
        char        local_path[path_len + 1];
        int         i = 1;

        local_path[0] = path[0];
        while ( i < path_len ) {
            /* Find next '/' in path, copying the partial path into another
             * buffer that we can use to check existence and create missing
             * directories as we go:
             */
            while ( (i < path_len) && path[i] && (path[i] != '/') ) {
                local_path[i] = path[i];
                i++;
            }
            local_path[i] = '\0';

            /* Does this partial path exist? */
            if ( stat(local_path, &finfo) ) {
                if ( mkdir(local_path, mode) ) {
                    slurm_info("auto_tmpdir::auto_tmpdir_mkdir_recurse: unable to create directory `%s` (%m)", local_path);
                    return -1;
                }
                if ( should_set_owner ) {
                    if ( __auto_tmpdir_chown(local_path, u_owner, g_owner) ) {
                        slurm_info("auto_tmpdir::auto_tmpdir_mkdir_recurse: unable to chown directory `%s` (%m)", local_path);
                        return -1;
                    }
                }
            } else if ( ! S_ISDIR(finfo.st_mode) ) {
                slurm_info("auto_tmpdir::auto_tmpdir_mkdir_recurse: not a directory: `%s`", local_path);
                return -1;
            }

            while ( (i < path_len) && (path[i] == '/') ) {
                local_path[i] = path[i];
                i++;
            }
        }
    } else if ( ! S_ISDIR(finfo.st_mode) ) {
        slurm_info("auto_tmpdir::auto_tmpdir_mkdir_recurse: not a directory: `%s`", path);
        return -1;
    }
    return 0;
}


/*
 * @function _rmdir_recurse
 *
 * Recursively remove a file path.
 *
 * Privileges must have been dropped prior to this function's being called.
 *
 */
int
auto_tmpdir_rmdir_recurse(
    const char      *path,
    int             should_remove_children_only
)
{
    int             rc = 0;

    char            *path_argv[2] = { (char*)path, NULL };

    /* FTS_NOCHDIR  - Avoid changing cwd, which could cause unexpected behavior
     *                in multithreaded programs
     * FTS_PHYSICAL - Don't follow symlinks. Prevents deletion of files outside
     *                of the specified directory
     * FTS_XDEV     - Don't cross filesystem boundaries
     */
    FTS             *ftsPtr = fts_open(path_argv, FTS_NOCHDIR | FTS_PHYSICAL | FTS_XDEV, NULL);
    FTSENT          *ftsItem;

    if ( ! ftsPtr ) {
        slurm_info("auto_tmpdir::auto_tmpdir_rmdir_recurse: failed to open file traversal context on `%s` (%m)", path);
        return (-1);
    }
    if ( (ftsItem = fts_read(ftsPtr)) ) {
        switch ( ftsItem->fts_info ) {
            case FTS_NS:
            case FTS_DNR:
            case FTS_ERR: {
                slurm_info("auto_tmpdir::auto_tmpdir_rmdir_recurse: directory `%s` does not exist", path);
                break;
            }
            case FTS_D: {
                /*
                 * We're entering a directory -- exactly what we want!
                 */
                while ( (ftsItem = fts_read(ftsPtr)) ) {
                    switch ( ftsItem->fts_info ) {
                        case FTS_NS:
                        case FTS_DNR:
                        case FTS_ERR:
                            slurm_info("auto_tmpdir::auto_tmpdir_rmdir_recurse: error in fts_read() of `%s` (%s)\n", ftsItem->fts_accpath, strerror(ftsItem->fts_errno));
                            rc = -1;
                            break;

                        case FTS_DC:
                        case FTS_DOT:
                        case FTS_NSOK:
                            /* Not reached unless FTS_LOGICAL, FTS_SEEDOT, or FTS_NOSTAT were
                             * passed to fts_open()
                             */
                            break;

                        case FTS_D:
                            /* Do nothing. Need depth-first search, so directories are deleted
                             * in FTS_DP
                             */
                            break;

                        case FTS_DP:
                            /* Remove the directory on post-order traversal (should be empty now): */
                            if ( should_remove_children_only && (strcmp(ftsItem->fts_accpath, path) == 0) ) break;
                            if ( rmdir(ftsItem->fts_accpath) < 0 ) {
                                slurm_info("auto_tmpdir::auto_tmpdir_rmdir_recurse: failed to remove directory `%s` (%s)\n", ftsItem->fts_accpath, strerror(ftsItem->fts_errno));
                                rc = -1;
                            }
                            break;

                        case FTS_F:
                        case FTS_SL:
                        case FTS_SLNONE:
                        case FTS_DEFAULT:
                            /* Remove a non-directory item: */
                            if ( unlink(ftsItem->fts_accpath) < 0 ) {
                                slurm_info("auto_tmpdir::auto_tmpdir_rmdir_recurse: failed to remove `%s` (%s)", ftsItem->fts_accpath, strerror(ftsItem->fts_errno));
                                rc = -1;
                            }
                            break;
                    }
                }
                break;
            }
        }
    }
    fts_close(ftsPtr);
    return rc;
}

/**/

const char*
__auto_tmpdir_fs_default_state_file(
    spank_t             spank_ctxt,
    int                 argc,
    char*               argv[]
)
{
    static char         *state_file = NULL;
    
    if ( ! state_file ) {
        int             i;
        uint32_t        job_id = NO_VAL;
        const char      *state_dir = "/tmp";
        int             rc;

        /* Get the base job id: */
        if ( (rc = spank_get_item(spank_ctxt, S_JOB_ID, &job_id)) != ESPANK_SUCCESS ) {
            slurm_error("auto_tmpdir: __auto_tmpdir_fs_default_state_file: no job id associated with job??");
            return NULL;
        }

        slurm_debug("auto_tmpdir::__auto_tmpdir_fs_default_state_file: %u", job_id);

        /*
         * First pass through the arguments to the plugin -- pull the state_dir if present:
         */
        i = 0;
        while ( i < argc ) {
            if ( strncmp(argv[i], "state_dir=", 10) == 0 ) {
                state_dir = argv[i] + 10;
                if ( *state_dir != '/' ) {
                    slurm_error("auto_tmpdir::__auto_tmpdir_fs_default_state_file: invalid state_dir in plugstack configuration (%s)", state_dir);
                    return NULL;
                }
                break;
            }
            i++;
        }

        slurm_debug("auto_tmpdir::__auto_tmpdir_fs_default_state_file: state_dir=%s", state_dir);
        
        /*
         * Path should be <state_dir>/auto_tmpdir_fs-<job-id>{_<job-task-id>}.cache
         */
        rc = snprintf(NULL, 0, "%s/auto_tmpdir_fs-%u.cache", state_dir, job_id);
        if ( rc > 0 ) {
            state_file = malloc(rc + 1);
            if ( state_file ) {
                snprintf(state_file, rc + 1, "%s/auto_tmpdir_fs-%u.cache", state_dir, job_id);
            }
        }
    }
    return state_file;
}

#define AUTO_TMPDIR_FS_SERIALIZE(FIELD) \
            out_bytes += write(state_file_fd, (void*)&(FIELD), sizeof(FIELD)); expect_bytes += sizeof(FIELD); \
            if ( out_bytes != expect_bytes ) { \
                slurm_error("auto_tmpdir::auto_tmpdir_fs_serialize_to_file: failed to write %s to `%s` (errno = %d)", #FIELD, filepath, errno); \
                goto early_exit; \
            }
#define AUTO_TMPDIR_FS_SERIALIZE_CSTR(FIELD) \
            size_bytes = (FIELD) ? strlen(FIELD) : 0; \
            out_bytes += write(state_file_fd, (void*)&size_bytes, sizeof(size_bytes)); expect_bytes += sizeof(size_bytes); \
            if ( out_bytes != expect_bytes ) { \
                slurm_error("auto_tmpdir::auto_tmpdir_fs_serialize_to_file: failed to write %s string length to `%s` (errno = %d)", #FIELD, filepath, errno); \
                goto early_exit; \
            } \
            if ( FIELD ) { out_bytes += write(state_file_fd, (void*)FIELD, size_bytes);  expect_bytes += size_bytes; } \
            if ( out_bytes != expect_bytes ) { \
                slurm_error("auto_tmpdir::auto_tmpdir_fs_serialize_to_file: failed to write %s string to `%s` (errno = %d)", #FIELD, filepath, errno); \
                goto early_exit; \
            }

int
auto_tmpdir_fs_serialize_to_file(
    auto_tmpdir_fs_ref  fs_info,
    spank_t             spank_ctxt,
    int                 argc,
    char*               argv[],
    const char          *filepath
)
{
    int                 rc, state_file_fd;
    
    if ( ! filepath ) {
        filepath = __auto_tmpdir_fs_default_state_file(spank_ctxt, argc, argv);
        if ( ! filepath ) {
            slurm_error("auto_tmpdir::auto_tmpdir_fs_serialize_to_file: unable to get default state file path");
            return ENOMEM;
        }
    }
    
    /* Attempt to open the file: */
    state_file_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if ( state_file_fd >= 0 ) {
        ssize_t     out_bytes = 0, expect_bytes = 0;
        size_t      size_bytes = 0;
        
        /*
         * Write the header fields:
         */
        AUTO_TMPDIR_FS_SERIALIZE(fs_info->options);
        AUTO_TMPDIR_FS_SERIALIZE_CSTR(fs_info->tmpdir);
        AUTO_TMPDIR_FS_SERIALIZE_CSTR(fs_info->base_dir);
        AUTO_TMPDIR_FS_SERIALIZE_CSTR(fs_info->base_dir_parent);
        
        auto_tmpdir_fs_bindpoint_t  *bindpoint_node = fs_info->bind_mounts_tail;
        
        while ( bindpoint_node ) {
            /* State flags: */
            AUTO_TMPDIR_FS_SERIALIZE(bindpoint_node->is_bind_mounted);
            AUTO_TMPDIR_FS_SERIALIZE(bindpoint_node->should_always_remove);
            AUTO_TMPDIR_FS_SERIALIZE_CSTR(bindpoint_node->bind_this_path);
            AUTO_TMPDIR_FS_SERIALIZE_CSTR(bindpoint_node->to_this_path);
            
            /* Move to previous node: */
            bindpoint_node = bindpoint_node->back_link;
        }
        slurm_debug("auto_tmpdir::auto_tmpdir_fs_serialize_to_file: serialized to `%s`", filepath);
        
early_exit:
        close(state_file_fd);
        rc =  ( out_bytes == expect_bytes ) ? 0 : errno;
    } else {
        slurm_error("auto_tmpdir::auto_tmpdir_fs_serialize_to_file: unable to open state file `%s` (errno = %d)", filepath, errno);
        rc = errno;
    }
    return rc;
}

#define AUTO_TMPDIR_FS_UNSERIALIZE(FIELD) \
            in_bytes += read(state_file_fd, (void*)&(FIELD), sizeof(FIELD)); expect_bytes += sizeof(FIELD); \
            if ( in_bytes != expect_bytes ) { \
                slurm_error("auto_tmpdir::auto_tmpdir_fs_init_with_file: failed to read %s from `%s` (errno = %d)", #FIELD, filepath, errno); \
                rc = 1; goto early_exit; \
            }
#define AUTO_TMPDIR_FS_UNSERIALIZE_CSTR(FIELD) \
            in_bytes += read(state_file_fd, (void*)&size_bytes, sizeof(size_bytes)); expect_bytes += sizeof(size_bytes); \
            if ( in_bytes != expect_bytes ) { \
                slurm_error("auto_tmpdir::auto_tmpdir_fs_init_with_file: failed to read %s string length from `%s` (errno = %d)", #FIELD, filepath, errno); \
                rc = 1; goto early_exit; \
            } \
            if ( size_bytes ) { \
                FIELD = malloc(size_bytes + 1); \
                if ( ! FIELD ) { \
                    slurm_error("auto_tmpdir::auto_tmpdir_fs_init_with_file: failed to allocate %s string (errno = %d)", #FIELD, errno); \
                    rc = 1; goto early_exit; \
                } \
                in_bytes += read(state_file_fd, (void*)FIELD, size_bytes); expect_bytes += size_bytes; \
                if ( in_bytes != expect_bytes ) { \
                    slurm_error("auto_tmpdir::auto_tmpdir_fs_serialize_to_file: failed to write %s string to `%s` (errno = %d)", #FIELD, filepath, errno); \
                    rc = 1; goto early_exit; \
                } \
                ((char*)FIELD)[size_bytes] = '\0'; \
            } else { \
                FIELD = NULL; \
            }


auto_tmpdir_fs_ref
auto_tmpdir_fs_init_with_file(
    spank_t                     spank_ctxt,
    int                         argc,
    char*                       argv[],
    auto_tmpdir_fs_options_t    options,
    const char                  *filepath,
    int                         remove_state_file
)
{
    auto_tmpdir_fs              *new_fs = NULL;
    int                         state_file_fd, rc = 0;
    
    if ( ! filepath ) {
        filepath = __auto_tmpdir_fs_default_state_file(spank_ctxt, argc, argv);
        if ( ! filepath ) {
            slurm_error("auto_tmpdir::auto_tmpdir_fs_init_with_file: unable to get default state file path");
            return NULL;
        }
    }
    
    /* Attempt to open the file: */
    state_file_fd = open(filepath, O_RDONLY);
    if ( state_file_fd >= 0 ) {
        ssize_t     in_bytes = 0, expect_bytes = 0;
        size_t      size_bytes = 0;
        
        new_fs = malloc(sizeof(auto_tmpdir_fs));
        if ( new_fs ) {
            /* Read the header: */
            AUTO_TMPDIR_FS_UNSERIALIZE(new_fs->options);
            AUTO_TMPDIR_FS_UNSERIALIZE_CSTR(new_fs->tmpdir);
            AUTO_TMPDIR_FS_UNSERIALIZE_CSTR(new_fs->base_dir);
            AUTO_TMPDIR_FS_UNSERIALIZE_CSTR(new_fs->base_dir_parent);
            new_fs->bind_mounts = NULL;
            new_fs->bind_mounts_tail = NULL;
            
            while ( 1 ) {
                int         is_bind_mounted;
                ssize_t     initial_in_bytes;
                
                /* Try to read an int to see if we have a record present: */
                initial_in_bytes = read(state_file_fd, &is_bind_mounted, sizeof(is_bind_mounted));
                if ( initial_in_bytes == 0 ) break;
                in_bytes += initial_in_bytes; expect_bytes += sizeof(is_bind_mounted);
                
                /* Allocate a node: */
                auto_tmpdir_fs_bindpoint_t  *bindpoint_node = malloc(sizeof(auto_tmpdir_fs_bindpoint_t));
                if ( ! bindpoint_node ) {
                    slurm_error("auto_tmpdir::auto_tmpdir_fs_init_with_file: failed to allocate auto_tmpdir_fs bindpoint node");
                    rc = 1; goto early_exit;
                }
                
                /* Read the rest of the fields: */
                bindpoint_node->is_bind_mounted = is_bind_mounted;
                AUTO_TMPDIR_FS_UNSERIALIZE(bindpoint_node->should_always_remove);
                AUTO_TMPDIR_FS_UNSERIALIZE_CSTR(bindpoint_node->bind_this_path);
                AUTO_TMPDIR_FS_UNSERIALIZE_CSTR(bindpoint_node->to_this_path);
                bindpoint_node->link = bindpoint_node->back_link = NULL;
                
                if ( ! new_fs->bind_mounts_tail ) new_fs->bind_mounts_tail = bindpoint_node;
                if ( new_fs->bind_mounts ) new_fs->bind_mounts->back_link = bindpoint_node;
                bindpoint_node->link = new_fs->bind_mounts;
                new_fs->bind_mounts = bindpoint_node;
            }
            
        } else {
            slurm_error("auto_tmpdir::auto_tmpdir_fs_init_with_file: failed to allocate auto_tmpdir_fs");
            goto early_exit;
        }
        
early_exit:
        close(state_file_fd);
        if ( rc ) {
            /* Dispose of new_fs: */
            if ( new_fs ) {
                auto_tmpdir_fs_bindpoint_t  *bindpoint_node = new_fs->bind_mounts;
                
                while ( bindpoint_node ) {
                    auto_tmpdir_fs_bindpoint_t  *next = bindpoint_node->link;
                    
                    if ( bindpoint_node->bind_this_path ) free((void*)bindpoint_node->bind_this_path);
                    if ( bindpoint_node->to_this_path ) free((void*)bindpoint_node->to_this_path);
                    free((void*)bindpoint_node);
                    bindpoint_node = next;
                }
                if ( new_fs->tmpdir ) free((void*)new_fs->tmpdir);
                if ( new_fs->base_dir ) free((void*)new_fs->base_dir);
                if ( new_fs->base_dir_parent ) free((void*)new_fs->base_dir_parent);
                free((void*)new_fs);
                new_fs = NULL;
            }
        }
    } else {
        slurm_error("auto_tmpdir::auto_tmpdir_fs_init_with_file: unable to open state file `%s` (errno = %d)", filepath, errno);
    }
    
    if ( remove_state_file && filepath ) {
        unlink(filepath);
    }
    
    return new_fs;
}
