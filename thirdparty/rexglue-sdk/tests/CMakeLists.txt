# ReXGlue Tests
# Test infrastructure for PPC instruction validation and unit tests

# Unit tests for core components
add_subdirectory(unit)

# PPC instruction tests (requires PPC toolchain)
add_subdirectory(ppc)
