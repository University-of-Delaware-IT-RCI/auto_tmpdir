#include <stdio.h>
#include <slurm/slurm.h>

int
main()
{
    printf("%2.2d.%2.2d.%d", \
        (int)SLURM_VERSION_MAJOR(SLURM_VERSION_NUMBER), \
        (int)SLURM_VERSION_MINOR(SLURM_VERSION_NUMBER), \
        (int)SLURM_VERSION_MICRO(SLURM_VERSION_NUMBER) \
      );
    return 0;
}

