# auto_tmpdir

Slurm SPANK plugin for automated handling of temporary directories for jobs.

The plugin accepts the following command-line options to srun/salloc/sbatch:

```
      --no-rm-tmpdir          Do not automatically remove temporary directories
                              for the job/steps.
      --use-shared-tmpdir     Create temporary directories on shared storage.
                              Use "--use-shared-tmpdir=per-node" to create
                              unique sub-directories for each node allocated to
                              the job (e.g. <base><job-id>/<nodename>).
```

Given a base directory prefix (configured at build, e.g. `/tmp/job-`) the job 8451 would see the directories `/tmp/job-8451` and `/dev/shm/job-8451` created in the prolog.  Optionally, a shared storage path (e.g. a directory on a Lustre filesystem) can be included which users can select via an salloc/srun/sbatch flag.  Additionally, each job will by default create a new mount namespace and bind-mount `/dev/shm/job-8451` as `/dev/shm`.

An arbitrary number of additional paths (typically `/tmp`, often additionally `/var/tmp`) will have directories created under `/tmp/job-8451` (e.g. `/tmp/job-8451/tmp` and `/tmp/job-8451/var_tmp`) to be bind-mounted in the job step (as `/tmp` and `/var/tmp`, respectively).  Bind-mounting takes an existing directory and makes it appear to exist at a different path:  e.g. programs inside the job see the contents of `/tmp/job-8451/tmp` at the path `/tmp`.  Naughty programs that cannot be convinced to create temporary files inside `$TMPDIR` (perhaps they're hard-coded to create files in `/tmp`) end up creating those files inside `/tmp/job-8451/tmp`; when the job exits, removal of `/tmp/job-8451` disposes of everything!

Bind-mounting of `/dev/shm` is especially important since the files in `/dev/shm` occupy virtual memory space.  If files are orphaned and not purged, the system can begin to run low on memory resources (Slurm does not look at actual free memory for a node when making scheduling decisions).  Removal of the bind-mounted `/dev/shm/job-8451` directory at job completion ensures all space is recovered.

The bind-mounted paths are configured in the Slurm `plugstack.conf` record for this plugin:

```
#
# SLURM plugin stack configuration
#
# req/opt   plugin                  arguments
# ~~~~~~~   ~~~~~~~~~~~~~~~~~~~~~~  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
required    auto_tmpdir.so          mount=/tmp mount=/var/tmp
```

The `TMPDIR` environment variable is set to `/tmp` by default by this plugin.  The value of `TMPDIR` can be overridden in the plugin configuration:

```
required    auto_tmpdir.so          mount=/tmp mount=/var/tmp tmpdir=/var/tmp
```

The compiled-in local- and shared-directory prefixes can also be overridden in the plugin configuration using the `local_prefix` or `shared_prefix` directives:

```
required    auto_tmpdir.so          mount=/tmp mount=/var/tmp local_prefix=/tmp/slurm- shared_prefix=/scratch/slurm/job-
```

The creation and bind-mount of `/dev/shm` can also be disabled:

```
required    auto_tmpdir.so          mount=/tmp mount=/var/tmp no_dev_shm
```

The scope of the `--no-rm-tmpdir` functionality can be limited to jobs that request `--use-shared-tmpdir`:

```
required    auto_tmpdir.so          mount=/tmp mount=/var/tmp no_rm_shared_only
```

In order to make it quicker for the slurmstepd and epilog contexts to reconstruct the hierarchy of directories that are to be bind-mounted, the plugin creates a state file.  By default, this file will be in `/tmp` and be named according to the job properties:  `/tmp/auto_tmpdir_fs-<job-id>{_<job-task-id>}.cache`.  The base directory used for these files is configurable:

```
required    auto_tmpdir.so          mount=/tmp mount=/var/tmp state_dir=/var/tmp/auto_tmpdir_cache
```

## Order of mount= options

Please note that the *order* of the `mount=` options can be significant:

1. The mount points are constructed in the order they appear
2. The mount points are bind-mounted in the **reverse** of the order they appear
3. The mount points are unmounted and removed in the order they appear

Using the following configuration

```
required    auto_tmpdir.so          mount=/var/tmp mount=/tmp
```

for job 8451 would have `/tmp/job-8451/tmp` bind-mounted at `/tmp` first; trying to bind mount `/tmp/job-8451/var_tmp` will subsequently fail because it's at that point referencing the directory `/tmp/job-8451/tmp/job-8451/var_tmp`.  In practice, if the directory in which you're creating the job's temporary directory of bind-mountpoints will be masked by a bind mount inside the job, make sure it is the first `mount=` option you present.

## Operation

Assume the configuation:

```
required    auto_tmpdir.so          mount=/tmp mount=/var/tmp local_prefix=/tmp/slurm- shared_prefix=/scratch/slurm/job-
```

In the prolog stage for job 8451, each participating slurmd instance will create the following directories (mode 0700, owned by the job owner's uid and gid):

- `/tmp/slurm-8451`
- `/tmp/slurm-8451/tmp`
- `/tmp/slurm-8451/var_tmp`
- `/dev/shm/job-8451`

When the slurmd starts a slurmstepd associated with the job, the slurmstepd creates a new mount namespace does the following bind-mounts:

- `/tmp/slurm-8451/tmp` → `/tmp`
- `/tmp/slurm-8451/var_tmp` → `/var/tmp`
- `/dev/shm/job-8451` → `/dev/shm`

In the epilog stage for job 8451, each participating slurmd instance will remove the directories that were created.  The  directories/files can be left behind using the `--no-rm-tmpdir` option.  If a `/dev/shm` bind-mount was created, it is always removed.

The `--use-shared-tmpdir` option changes the default base directory to a shared scratch storage path configured at build time (e.g. on a Lustre file system).  Using the optional `per-node` value for this option alters the directory naming to include the short hostname as a directory component, e.g. `<base>/job-8451/n000`.

## Building the plugin

The build system is configured via CMake.  The [CMakeLists.txt](./CMakeLists.txt) file outlines the variables that affect the build:

| Variable | Discussion | Default |
| --- | --- | --- |
| `SLURM_PREFIX` | Path at which Slurm is installed; the plugin will get installed to `${SLURM_PREFIX}/lib/slurm`, linked against libraries in `${SLURM_PREFIX}/lib`, and headers will be used from `${SLURM_PREFIX}/include`. | `/usr/local` |
| `SHARED_LIB_SUFFIX` | Suffix used on the plugin binary | `.so` |
| `AUTO_TMPDIR_DEV_SHM` | Directory under which shared memory files are created. | `/dev/shm` if it exists |
| `AUTO_TMPDIR_DEFAULT_LOCAL_PREFIX` | Path prefix to which job id is appended to create the per-job temp directory.  E.g. `/tmp/slurm-` yields directories like `/tmp/slurm-<jobid>` while `/tmp/slurm/` would produce the deeper path `/tmp/slurm/<jobid>` | `/tmp/slurm-` |
| `AUTO_TMPDIR_ENABLE_SHARED_TMPDIR` | Enables an alternate directory hierarchy (typically on network-shared media) available for temp directories at the user's request. | OFF |
| `AUTO_TMPDIR_DEFAULT_SHARED_PREFIX` | If the alternate directory hierarchy is enabled, this is its equivalent to `AUTO_TMPDIR_DEFAULT_LOCAL_PREFIX` | |
| `AUTO_TMPDIR_NO_GID_CHOWN` | The temporary directories created by the plugin will *not* be reowned to the job's gid; this option is always ON for Slurm releases < 20 | OFF |

On our clusters we build and install Slurm to `/opt/shared/slurm/<version>` and have local SSD storage on compute nodes mounted as `/tmp`.  CentOS does present the `/dev/shm` mountpoint for shared memory files.  We also have a special area set aside on our Lustre file system for shared temp directories.  Thus, setup of a build environment for Slurm looks like this:

```
$ git clone https://github.com/University-of-Delaware-IT-RCI/auto_tmpdir.git
$ cd auto_tmpdir
$ mkdir build-20220627
$ cd build-20220627
$ cmake -DSLURM_PREFIX=/opt/shared/slurm/20.11.5 -DCMAKE_BUILD_TYPE=Release \
    -DAUTO_TMPDIR_ENABLE_SHARED_TMPDIR=YES \
    -DAUTO_TMPDIR_DEFAULT_SHARED_PREFIX=/lustre/slurm \
	..
	
-- The C compiler identification is GNU 4.8.5
-- Check for working C compiler: /usr/bin/cc
-- Check for working C compiler: /usr/bin/cc -- works
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Found SLURM: /opt/shared/slurm/20.11.5/lib/libslurm.so  
-- Configuring done
-- Generating done
-- Build files have been written to: /opt/shared/slurm/add-ons/auto_tmpdir/build-20.11.5
```

The default local path prefix, `/tmp/slurm-`, is fine by us.  The build is accomplished via `make`:

```
$ make
Scanning dependencies of target auto_tmpdir
[ 50%] Building C object CMakeFiles/auto_tmpdir.dir/fs-utils.c.o
[100%] Building C object CMakeFiles/auto_tmpdir.dir/auto_tmpdir.c.o
Linking C shared module auto_tmpdir.so
[100%] Built target auto_tmpdir
```

It's fine to install the plugin at any time — Slurm won't use it until it has been configured to do so.

```
$ make install
[100%] Built target auto_tmpdir
Install the project...
-- Install configuration: "Release"
-- Installing: /opt/shared/slurm/20.11.5/lib/slurm/auto_tmpdir.so
```

The plugin is enabled by adding it to your `plugstack.conf` file on all nodes.

With each upgrade to Slurm a new `build-<version>` directory is created and the build is done therein.
