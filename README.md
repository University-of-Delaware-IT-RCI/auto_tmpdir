# auto_tmpdir

Slurm SPANK plugin for automated handling of temporary directories for jobs.

The plugin accepts the following command-line options to srun/salloc/sbatch:

```
      --tmpdir=<path>         Use the given path as the base directory for
                              temporary files.
      --no-step-tmpdir        Do not create per-step sub-directories.
      --no-rm-tmpdir          Do not automatically remove temporary directories
                              for the job/steps.
      --use-shared-tmpdir     Create temporary directories on shared storage
                              (overridden by --tmpdir).
```

Given a base directory `/tmp` the job 8451 would see the directory `/tmp/8451` created.  For batch jobs, the batch step will see `/tmp/8451` as `$TMPDIR`.  By default, each job step would have a unique directory created, e.g. the first job step that is `srun` would have a `$TMPDIR` of `/tmp/8451/0`.

The `--no-step-tmpdir` option forces all job steps to use the batch `$TMPDIR` (in this example, `/tmp/8451`).

By default, when a job step completes its per-step temporary sub-directory is removed from the filesystem; when the batch job completes the job temporary directory (`/tmp/8451`) is removed.  The temporary directories/files can be left behind using the `--no-rm-tmpdir` option.

The `--use-shared-tmpdir` option changes the default base directory from `/tmp` to a shared large-scale scratch storage path configured at build time (e.g. on a Lustre file system).
