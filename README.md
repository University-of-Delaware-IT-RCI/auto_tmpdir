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

Given a base directory prefix (configured at build, e.g. `/tmp/job-`) the job 8451 would see the directories `/tmp/job-8451` and `/dev/shm/job-8451` created in the prolog.  Optionally, a shared storage path (e.g. a directory on a Lustre filesystem) can be included which users can select via an salloc/srun/sbatch flag.  Each job step will create a new mount namespace and bind-mount `/dev/shm/job-8451` as `/dev/shm`.

An arbitrary number of additional paths (typically `/tmp`, often additionally `/var/tmp`) will have directories created under `/dev/shm/job-8451` (e.g. `/dev/shm/job-8451/tmp` and `/dev/shm/job-8451/var_tmp`) to be bind-mounted in the job step.  The paths are configured in the Slurm `plugstack.conf` record for this plugin:

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
