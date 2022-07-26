#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <slurm/slurm.h>

enum {
    version_major,
    version_major_minor,
    version_major_minor_micro
};

int
main(
    int         argc,
    const char* argv[]
)
{
    int         argn, variant = version_major_minor_micro;
    const char  *fmt;

    argn = 1;
    while ( argn < argc ) {
        if ( ! strcmp(argv[argn], "--major") || ! strcmp(argv[argn], "-1") ) {
	    variant = version_major;
	}
	else if ( ! strcmp(argv[argn], "--major+minor") || ! strcmp(argv[argn], "-2") ) {
	    variant = version_major_minor;
	}
	else if ( ! strcmp(argv[argn], "--major+minor+micro") || ! strcmp(argv[argn], "-3") ) {
            variant = version_major_minor_micro;
        }
	else {
	    fprintf(stderr, "ERROR:  unknown option: %s\n", argv[argn]);
	    exit(EINVAL);
	}
	argn++;
    }
    switch ( variant ) {
        case version_major:
	    fmt = "%2.2d";
	    break;
	case version_major_minor:
	    fmt = "%2.2d.%2.2d";
	    break;
	case version_major_minor_micro:
	    fmt = "%2.2d.%2.2d.%d";
	    break;
    }
    printf(fmt, \
        (int)SLURM_VERSION_MAJOR(SLURM_VERSION_NUMBER), \
        (int)SLURM_VERSION_MINOR(SLURM_VERSION_NUMBER), \
        (int)SLURM_VERSION_MICRO(SLURM_VERSION_NUMBER) \
      );
    return 0;
}

