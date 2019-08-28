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
                              (overridden by --tmpdir).  Use
                              "--use-shared-tmpdir=per-node" to create unique
                              sub-directories for each node allocated to the job
                              (e.g. <base>/job_<jobid>/<nodename>).
```

Given a base directory `/tmp` the job 8451 would see the directory `/tmp/job_8451` created.  For batch jobs, the batch step will see `/tmp/job_8451` as `$TMPDIR`.  By default, each job step would have a unique directory created, e.g. the first job step that is `srun` would have a `$TMPDIR` of `/tmp/job_8451/step_0`.

The `--no-step-tmpdir` option forces all job steps to use the batch `$TMPDIR` (in this example, `/tmp/job_8451`).

By default, when a job step completes its per-step temporary sub-directory is removed from the filesystem; when the batch job completes the job temporary directory (`/tmp/job_8451`) is removed.  The temporary directories/files can be left behind using the `--no-rm-tmpdir` option.

The `--use-shared-tmpdir` option changes the default base directory from `/tmp` to a shared scratch storage path configured at build time (e.g. on a Lustre file system).  Using the optional `per-node` value for this option alters the directory naming to include the short hostname as a directory component, e.g. `<base>/job_8451/n000/step_0`.
