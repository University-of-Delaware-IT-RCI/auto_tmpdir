# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.1] - 2022-07-05
### Added
- LICENSE file (BSD 2-clause)
- CPack RPM generation components

### Changed
- Modified CMake spec to search for slurm/task_none.so rather than lib/slurm (better portability)
- Replaced slurm_warning() calls in fs-utils.c with slurm_info()
- Use PROJECT_* rather than CMAKE_PROJECT_* for older CMake compatibility
- CMake minimum version bumped to 3.6 for CPack (older versions did not work properly under testing)


## [1.0.0] - 2022-06-27
### Added
- CHANGELOG.md file added

### Changed
- Example build of the plugin added to README.md
- Employing different phases of the SPANK callback chain
    - Creation of bind-mountable directories in PROLOG
    - Removal of bind-mountable directories in EPILOG
    - Bind-mounts happen in POST-OPT stage (just prior to slurmstepd transitions to runtime user/group)
- Removed intervening whitespace in `#    cmakedefine` statements (CMake doesn't detect the statement unless its `#cmakedefine`)
