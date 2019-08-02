/*
 * auto_tmpdir
 *
 * SLURM SPANK plugin that automates the process of creating/destroying
 * temporary directories for jobs/steps.
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>

#include <slurm/spank.h>
#include <slurm/slurm.h>

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(auto_tmpdir, 1)

/*
 * Default TMPDIR prefix:
 */
static const char *default_tmpdir_prefix = "/tmp";

/*
 * Lustre-oriented TMPDIR prefix:
 */
static const char *lustre_tmpdir_prefix = "/lustre/scratch/slurm";

/*
 * Directory format strings:
 */
static const char *job_dir_sprintf_format = "%s/job_%u";
static const char *job_step_dir_sprintf_format = "%s/job_%u/step_%u";


/*
 * What's the base directory to use for temp files?
 */
static char *base_tmpdir = NULL;

/*
 * @function _opt_tmpdir
 *
 * Parse the --tmpdir=<path> option.
 *
 */
static int _opt_tmpdir(
    int         val,
    const char  *optarg,
    int         remote
)
{
    if ( *optarg != '/' ) {
        slurm_error("auto_tmpdir:  invalid path to --tmpdir: %s", optarg);
        return ESPANK_BAD_ARG;
    }
    
    if ( base_tmpdir ) free(base_tmpdir);
    base_tmpdir = strdup(optarg);
    slurm_verbose("auto_tmpdir:  temporary directories under %s", base_tmpdir);
    return ESPANK_SUCCESS;
}


/*
 * Should we remove temp directories we create?
 */
static int should_remove_tmpdir = 1;

/*
 * @function _opt_no_rm_tmpdir
 *
 * Parse the --no-rm-tmpdir option.
 *
 */
static int _opt_no_rm_tmpdir(
    int         val,
    const char  *optarg,
    int         remote
)
{
    should_remove_tmpdir = 0;
    slurm_verbose("auto_tmpdir:  will not remove tempororary directories");
    return ESPANK_SUCCESS;
}


/*
 * Should we create per-step sub-directories?
 */
static int should_create_per_step_tmpdirs = 1;

/*
 * @function _opt_no_step_tmpdir
 *
 * Parse the --no-step-tmpdir option.
 *
 */
static int _opt_no_step_tmpdir(
    int         val,
    const char  *optarg,
    int         remote
)
{
    should_create_per_step_tmpdirs = 0;
    slurm_verbose("auto_tmpdir:  will not create per-step tempororary directories");
    return ESPANK_SUCCESS;
}


/*
 * Place the tmpdir on Lustre? (overridden by --tmpdir)
 */
static int should_create_on_lustre = 0;

/*
 * @function _opt_use_lustre_tmpdir
 *
 * Parse the --use-lustre-tmpdir option.
 *
 */
static int _opt_use_lustre_tmpdir(
    int         val,
    const char  *optarg,
    int         remote
)
{
    should_create_on_lustre = 1;
    slurm_verbose("auto_tmpdir:  should create tempororary directories on /lustre/scratch");
    return ESPANK_SUCCESS;
}


/*
 * Options available to this spank plugin:
 */
struct spank_option spank_options[] =
    {
        { "tmpdir", "<path>",
            "Use the given path as the base directory for temporary files.",
            1, 0, (spank_opt_cb_f) _opt_tmpdir },

        { "no-step-tmpdir", NULL,
            "Do not create per-step sub-directories.",
            0, 0, (spank_opt_cb_f) _opt_no_step_tmpdir },

        { "no-rm-tmpdir", NULL,
            "Do not automatically remove temporary directories for the job/steps.",
            0, 0, (spank_opt_cb_f) _opt_no_rm_tmpdir },

        { "use-lustre-tmpdir", NULL,
            "Create temporary directories on /lustre/scratch (overridden by --tmpdir).",
            0, 0, (spank_opt_cb_f) _opt_use_lustre_tmpdir },

        SPANK_OPTIONS_TABLE_END
    };


/**/


/*
 * @function _get_base_tmpdir
 *
 * Returns the configured base directory for temporary directories
 * or /tmp by default.
 *
 * Privileges must have been dropped prior to this function's being called.
 *
 */
const char*
_get_base_tmpdir()
{
    const char      *path = NULL;
    int             had_initial_error = 0;
    
    if ( base_tmpdir ) {
        path = base_tmpdir;
    }
    else if ( should_create_on_lustre ) {
        path = lustre_tmpdir_prefix;
    } else {
        path = default_tmpdir_prefix;
    }

retry_test:
    if ( access(path, R_OK|W_OK|X_OK) == 0 ) {
        if ( had_initial_error ) slurm_error("auto_tmpdir: defaulting to temporary directory base path: %s", path);
        return path;
    }
    slurm_error("auto_tmpdir: no access to temporary directory base path: %s", path);
    
    if ( path == base_tmpdir ) {
        if ( should_create_on_lustre ) {
            path = lustre_tmpdir_prefix;
        } else {
            path = default_tmpdir_prefix;
        }
        had_initial_error = 1;
        goto retry_test;
    }
    else if ( path == lustre_tmpdir_prefix ) {
        path = default_tmpdir_prefix;
        had_initial_error = 1;
        goto retry_test;
    }
    
    return NULL;
}

/*
 * @function _sprint_tmpdir
 *
 * Fill a character buffer with the tmpdir name to be used.
 *
 * Privileges must have been dropped prior to this function's being called.
 *
 */
int
_sprint_tmpdir(
    char            *buffer,
    size_t          buffer_capacity,
    uint32_t        job_id,
    uint32_t        job_step_id,
    const char*     *tmpdir_prefix
)
{
    size_t          actual_len;
    const char      *tmpdir = _get_base_tmpdir();
    
    if ( tmpdir ) {
        if ( ! should_create_per_step_tmpdirs || ((job_step_id == SLURM_BATCH_SCRIPT) || (job_step_id == SLURM_EXTERN_CONT)) ) {
            actual_len = snprintf(buffer, buffer_capacity, job_dir_sprintf_format, tmpdir, job_id);
        } else {
            actual_len = snprintf(buffer, buffer_capacity, job_step_dir_sprintf_format, tmpdir, job_id, job_step_id);
        }
        if ( (actual_len > 0) && (actual_len < buffer_capacity) ) {
            if ( tmpdir_prefix ) *tmpdir_prefix = tmpdir;
            return actual_len;
        }
    }
    return -1;
}

/*
 * @function _mktmpdir
 *
 * Given a job id and step id, create the temporary directory.
 *
 * Privileges must have been dropped prior to this function's being called.
 *
 */
int
_mktmpdir(
    char            *outTmpDir,
    size_t          outTmpDirLen,
    uint32_t        job_id,
    uint32_t        job_step_id
)
{
    const char      *tmpdir = NULL;
    int             actual_len = 0;

    /* Decide which format the directory should use and determine string length: */
    actual_len = _sprint_tmpdir(outTmpDir, outTmpDirLen, job_id, job_step_id, &tmpdir);
    if ( ! tmpdir ) return (-1);

    /* If that failed then we've got big problems: */
    if ( (actual_len < 0) || (actual_len >= outTmpDirLen) ) {
        slurm_error("auto_tmpdir: Failure while creating new tmpdir path: %d", actual_len);
        return (-1);
    } else {
        struct stat   finfo;

        /* Build the path, making sure each component exists: */
        strncpy(outTmpDir, tmpdir, outTmpDirLen);
        if ( (stat(outTmpDir, &finfo) == 0) && S_ISDIR(finfo.st_mode) ) {
            /* At the least we'll need the job directory: */
            actual_len = snprintf(outTmpDir, outTmpDirLen, job_dir_sprintf_format, tmpdir, job_id);
            if ( stat(outTmpDir, &finfo) != 0 ) {
                if ( mkdir(outTmpDir, 0700) != 0 ) {
                    slurm_error("auto_tmpdir: failed creating job tmpdir: %s", outTmpDir);
                    return (-1);
                }
                stat(outTmpDir, &finfo);
            }
            if ( ! S_ISDIR(finfo.st_mode) ) {
                slurm_error("auto_tmpdir: job tmpdir is not a directory: %s", outTmpDir);
                return (-1);
            }

            /* If this isn't the batch/extern portion of a job, worry about the step subdir: */
            if ( should_create_per_step_tmpdirs && ((job_step_id != SLURM_BATCH_SCRIPT) && (job_step_id != SLURM_EXTERN_CONT)) ) {
                actual_len = snprintf(outTmpDir, outTmpDirLen, job_step_dir_sprintf_format, tmpdir, job_id, job_step_id);
                if ( stat(outTmpDir, &finfo) != 0 ) {
                    if ( mkdir(outTmpDir, 0700) != 0 ) {
                        slurm_error("auto_tmpdir: failed creating step tmpdir: %s", outTmpDir);
                        return (-1);
                    }
                    stat(outTmpDir, &finfo);
                }
                if ( ! S_ISDIR(finfo.st_mode) ) {
                    slurm_error("auto_tmpdir: step tmpdir is not a directory: %s", outTmpDir);
                    return (-1);
                }
            }
        } else {
            slurm_error("auto_tmpdir: base tmpdir is not a directory: %s", tmpdir);
            return (-1);
        }
    }
    return actual_len;
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
_rmdir_recurse(
    const char      *path
)
{
    int             rc = 0;
    
    char            *path_argv[2] = { (char*)path, NULL };

    // FTS_NOCHDIR  - Avoid changing cwd, which could cause unexpected behavior
    //                in multithreaded programs
    // FTS_PHYSICAL - Don't follow symlinks. Prevents deletion of files outside
    //                of the specified directory
    // FTS_XDEV     - Don't cross filesystem boundaries
    FTS             *ftsPtr = fts_open(path_argv, FTS_NOCHDIR | FTS_PHYSICAL | FTS_XDEV, NULL);
    FTSENT          *ftsItem;

    if ( ! ftsPtr ) {
        slurm_error("auto_tmpdir: _rmdir_recurse(): Failed to open file traversal context on %s: %s", path, strerror(errno));
        return (-1);
    }

    //
    // Read the room item -- should be a directory owned by ownerUid:
    //
    if ( (ftsItem = fts_read(ftsPtr)) ) {
        switch ( ftsItem->fts_info ) {
            case FTS_NS:
            case FTS_DNR:
            case FTS_ERR: {
                slurm_verbose("auto_tmpdir: _rmdir_recurse(%s): directory does not exist", path);
                break;
            }
            case FTS_D: {
                //
                // We're entering a directory -- exactly what we want!
                //
                while ( (ftsItem = fts_read(ftsPtr)) ) {
                    switch ( ftsItem->fts_info ) {
                        case FTS_NS:
                        case FTS_DNR:
                        case FTS_ERR:
                            slurm_error("auto_tmpdir: _rmdir_recurse(): Error in fts_read(%s): %s\n", ftsItem->fts_accpath, strerror(ftsItem->fts_errno));
                            rc = -1;
                            break;

                        case FTS_DC:
                        case FTS_DOT:
                        case FTS_NSOK:
                            // Not reached unless FTS_LOGICAL, FTS_SEEDOT, or FTS_NOSTAT were
                            // passed to fts_open()
                            break;

                        case FTS_D:
                            // Do nothing. Need depth-first search, so directories are deleted
                            // in FTS_DP
                            break;

                        case FTS_DP:
                        case FTS_F:
                        case FTS_SL:
                        case FTS_SLNONE:
                        case FTS_DEFAULT:
                            if ( remove(ftsItem->fts_accpath) < 0 ) {
                                slurm_error("auto_tmpdir: _rmdir_recurse(): Failed to remove %s: %s\n", ftsItem->fts_path, strerror(errno));
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


/*
 * @function slurm_spank_init
 *
 * In the ALLOCATOR context, the 'spank_options' don't get automatically
 * registered as they do under LOCAL and REMOTE.  So under that context
 * we explicitly register our cli options.
 *
 * In the REMOTE context, go ahead and create the temp directory and
 * assign appropriate ownership.
 *
 */
int
slurm_spank_init(
    spank_t       spank_ctxt,
    int           argc,
    char          *argv[]
)
{
    int                     rc = ESPANK_SUCCESS;
    int                     i;

    switch ( spank_context() ) {

        case S_CTX_ALLOCATOR: {
            struct spank_option   *o = spank_options;

            while ( o->name && (rc == ESPANK_SUCCESS) ) rc = spank_option_register(spank_ctxt, o++);
            break;
        }

    }
    return rc;
}


/*
 * @function slurm_spank_task_init
 *
 * Set job-specific TMPDIR in environment.  For batch scripts the path
 * uses just the job id; for all others, the path uses the job id, a dot,
 * and the job step id.  The value of TMPDIR handed to us by SLURM is
 * the base path for the new TMPDIR; if SLURM doesn't hand us a TMPDIR
 * then we default to using /tmp as our base directory.
 *
 * This function does not actually create the directory.
 *
 * (Called from slurmstepd after it starts.)
 */
int
slurm_spank_task_init(
    spank_t       spank_ctxt,
    int           argc,
    char          *argv[]
)
{
    int           rc = ESPANK_SUCCESS;
    char          tmpdir[PATH_MAX];
    int           tmpdirlen = 0;
    uint32_t      job_id, job_step_id;
    uid_t         jobUid = -1, savedUid = geteuid();
    gid_t         jobGid = -1, savedGid = getegid();
    int           didSetUid = 0, didSetGid = 0;
            
    /* Get the job id and step id: */
    if ( (rc = spank_get_item(spank_ctxt, S_JOB_ID, &job_id)) != ESPANK_SUCCESS ) {
        slurm_error("auto_tmpdir: no job id associated with job??");
        return rc;
    }
    if ( (rc = spank_get_item(spank_ctxt, S_JOB_STEPID, &job_step_id)) != ESPANK_SUCCESS ) {
        slurm_error("auto_tmpdir: no step id associated with job %u??", job_id);
        return rc;
    }
    
    slurm_verbose("slurm_spank_task_init(%u, %u)", job_id, job_step_id);

    /* What user should we function as? */
    if ((rc = spank_get_item (spank_ctxt, S_JOB_UID, &jobUid)) != ESPANK_SUCCESS) {
        slurm_error ("auto_tmpdir: unable to get job's user id");
        return rc;
    }
    if ((rc = spank_get_item (spank_ctxt, S_JOB_GID, &jobGid)) != ESPANK_SUCCESS) {
        slurm_error ("auto_tmpdir: unable to get job's group id");
        return rc;
    }
            
    /* Drop privileges: */
    if ( jobGid != savedGid ) {
        if ( setegid(jobGid) != 0 ) {
            if ( didSetUid ) seteuid(savedUid);
            slurm_error("auto_tmpdir: unable to %d -> setegid(%d) (errno = %d)", savedGid, jobGid, errno);
            return ESPANK_ERROR;
        }
        didSetGid = 1;
        slurm_verbose("auto_tmpdir:  changed to gid %d", jobGid);
    }
    if ( jobUid != savedUid ) {
        if ( seteuid(jobUid) != 0 ) {
            slurm_error("auto_tmpdir: unable to %d -> seteuid(%d) (errno = %d)", savedUid, jobUid, errno);
            return ESPANK_ERROR;
        }
        didSetUid = 1;
        slurm_verbose("auto_tmpdir:  changed to uid %d", jobUid);
    }
    
    tmpdirlen = _mktmpdir(tmpdir, sizeof(tmpdir), job_id, job_step_id);
    
    /* Restore privileges: */
    if ( didSetUid ) seteuid(savedUid);
    if ( didSetGid ) setegid(savedGid);
    
    if ( tmpdirlen > 0 ) {
        if ( (rc = spank_setenv(spank_ctxt, "TMPDIR", tmpdir, tmpdirlen)) != ESPANK_SUCCESS ) {
            slurm_error("setenv(TMPDIR, \"%s\"): %m", tmpdir);
            return rc;
        }
        slurm_verbose("auto_tmpdir: TMPDIR = %s", tmpdir);
    }
    return ESPANK_SUCCESS;
}


/*
 * @function __cleanup_tmpdir
 *
 * Remove each job step's TMPDIR as it exits.
 */
int
__cleanup_tmpdir(
    spank_t       spank_ctxt,
    uint32_t      job_id,
    uint32_t      job_step_id
)
{
    int           rc = ESPANK_SUCCESS;
    char          tmpdir[PATH_MAX];
    int           tmpdirlen = 0;
    uid_t         jobUid = -1, savedUid = geteuid();
    gid_t         jobGid = -1, savedGid = getegid();
    int           didSetUid = 0, didSetGid = 0;

    /* What user should we function as? */
    if ((rc = spank_get_item (spank_ctxt, S_JOB_UID, &jobUid)) != ESPANK_SUCCESS) {
        slurm_error ("auto_tmpdir: __cleanup_tmpdir: unable to get job's user id");
        return rc;
    }
    if ((rc = spank_get_item (spank_ctxt, S_JOB_GID, &jobGid)) != ESPANK_SUCCESS) {
        slurm_error ("auto_tmpdir: __cleanup_tmpdir: unable to get job's group id");
        return rc;
    }
            
    /* Drop privileges: */
    if ( jobGid != savedGid ) {
        if ( setegid(jobGid) != 0 ) {
            slurm_error("auto_tmpdir: __cleanup_tmpdir: unable to %d -> setegid(%d) (errno = %d)", savedGid, jobGid, errno);
            if ( didSetUid ) seteuid(savedUid);
            return ESPANK_ERROR;
        }
        didSetGid = 1;
        slurm_verbose("auto_tmpdir: __cleanup_tmpdir: changed to gid %d", jobGid);
    }
    if ( jobUid != savedUid ) {
        if ( seteuid(jobUid) != 0 ) {
            slurm_error("auto_tmpdir: __cleanup_tmpdir: unable to %d -> seteuid(%d) (errno = %d)", savedUid, jobUid, errno);
            return ESPANK_ERROR;
        }
        didSetUid = 1;
        slurm_verbose("auto_tmpdir: __cleanup_tmpdir: changed to uid %d", jobUid);
    }
    
    /* Create the path for this job sub-step: */
    tmpdirlen = _sprint_tmpdir(tmpdir, sizeof(tmpdir), job_id, job_step_id, NULL);
    if ( tmpdirlen > 0 ) {
        /* If we're ignoring sub-step directories, then this can ONLY be deleted when
         * the extern/batch step exits:
         */
        if ( should_create_per_step_tmpdirs || ((job_step_id == SLURM_BATCH_SCRIPT) || (job_step_id == SLURM_EXTERN_CONT)) ) {
            struct stat   finfo;
            
            if ( (stat(tmpdir, &finfo) == 0) && S_ISDIR(finfo.st_mode) ) {
                if ( _rmdir_recurse(tmpdir) != 0 ) {
                    slurm_error("auto_tmpdir: __cleanup_tmpdir: Unable to remove TMPDIR at exit (failure in _rmdir_recurse(%s))", tmpdir);
                    rc = ESPANK_ERROR;
                } else {
                    slurm_verbose("auto_tmpdir: __cleanup_tmpdir: rm -rf %s", tmpdir);
                }
            } else {
                slurm_verbose("auto_tmpdir: __cleanup_tmpdir: failed stat check of %s (st_mode = %x, errno = %d)", tmpdir, finfo.st_mode, errno);
            }
        }
    }
    
    /* Restore privileges: */
    if ( didSetUid ) seteuid(savedUid);
    if ( didSetGid ) setegid(savedGid);

    return rc;
}


/*
 * @function slurm_spank_task_exit
 *
 * Remove each job step's TMPDIR as it exits.
 */
int
slurm_spank_task_exit(
    spank_t       spank_ctxt,
    int           ac,
    char          **av
)
{
    int           rc = ESPANK_SUCCESS;

    if ( should_remove_tmpdir ) {
        uint32_t      job_id, job_step_id;
                
        /* Get the job id and step id: */
        if ( (rc = spank_get_item(spank_ctxt, S_JOB_ID, &job_id)) != ESPANK_SUCCESS ) {
            slurm_error("auto_tmpdir: slurm_spank_exit: no job id associated with job??");
            return rc;
        }
        if ( (rc = spank_get_item(spank_ctxt, S_JOB_STEPID, &job_step_id)) != ESPANK_SUCCESS ) {
            slurm_error("auto_tmpdir: slurm_spank_exit: no step id associated with job %u??", job_id);
            return rc;
        }
        
        slurm_verbose("slurm_spank_task_exit(%u, %u)", job_id, job_step_id);
        if (!((job_step_id == SLURM_BATCH_SCRIPT) || (job_step_id == SLURM_EXTERN_CONT))) rc = __cleanup_tmpdir(spank_ctxt, job_id, job_step_id);
    }
    return rc;
}


/*
 * @function slurm_spank_exit
 *
 * Remove the overall job TMPDIR as it exits.
 *
 * (Called as root user after tasks have exited.)
 */
int
slurm_spank_exit(
    spank_t       spank_ctxt,
    int           ac,
    char          **av
)
{
    int           rc = ESPANK_SUCCESS;

    if ( should_remove_tmpdir ) {
        if ( spank_remote(spank_ctxt) ) {
            uint32_t    job_id, job_step_id;
                    
            /* Get the job id and step id: */
            if ( (rc = spank_get_item(spank_ctxt, S_JOB_ID, &job_id)) != ESPANK_SUCCESS ) {
                slurm_error("auto_tmpdir: slurm_spank_exit: no job id associated with job??");
                return rc;
            }
            if ( (rc = spank_get_item(spank_ctxt, S_JOB_STEPID, &job_step_id)) != ESPANK_SUCCESS ) {
                slurm_error("auto_tmpdir: slurm_spank_exit: no step id associated with job %u??", job_id);
                return rc;
            }
            
            slurm_verbose("slurm_spank_exit(%u, %u)", job_id, job_step_id);
            if ((job_step_id == SLURM_BATCH_SCRIPT) || (job_step_id == SLURM_EXTERN_CONT)) rc = __cleanup_tmpdir(spank_ctxt, job_id, job_step_id);
        }
    }
    return rc;
}
