# DMDEVFS Testing

This directory contains tests for the DMDEVFS module.

## Test Structure

### Build Verification Tests

The primary tests verify that the DMDEVFS module builds correctly and produces the expected output files. These tests run automatically on CI.

To run the tests locally:

```bash
mkdir build_tests
cd build_tests
cmake .. -DDMOD_MODE=DMOD_MODULE -DDMDEVFS_BUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```

### Integration Tests with fs_tester

For more comprehensive testing, you can use the `fs_tester` tool from the [dmvfs repository](https://github.com/choco-technologies/dmvfs).

#### Quick Start with Integration Tests

We provide a convenience script to run integration tests:

```bash
# 1. Build dmdevfs
mkdir build && cd build
cmake .. -DDMOD_MODE=DMOD_MODULE
cmake --build .
cd ..

# 2. Clone and build dmvfs with fs_tester
git clone https://github.com/choco-technologies/dmvfs.git /tmp/dmvfs
cd /tmp/dmvfs
mkdir build && cd build
cmake .. -DDMVFS_BUILD_TESTS=ON
cmake --build .

# 3. Run integration tests
cd /path/to/dmdevfs
./tests/run_integration_tests.sh /tmp/dmvfs/build/tests/fs_tester build/dmf/dmdevfs.dmf
```

#### Manual Testing with fs_tester

The fs_tester can test DMDEVFS in read-only mode:

```bash
# Clone dmvfs if you haven't already
git clone https://github.com/choco-technologies/dmvfs.git

# Build fs_tester
cd dmvfs
mkdir build && cd build
cmake .. -DDMVFS_BUILD_TESTS=ON
cmake --build .

# Run tests on dmdevfs module (read-only mode)
./tests/fs_tester --read-only-fs path/to/dmdevfs.dmf
```

## Test Coverage

Current automated tests verify:
- Module compilation succeeds
- Module output files are generated
- Build system integration works correctly

With fs_tester integration:
- File system interface implementation
- Read operations on device drivers
- Directory operations
- File metadata operations

Future test improvements could include:
- Device driver mock for testing file operations
- Write operation tests (if supported by drivers)
- Performance benchmarks

## Note on Test Limitations

DMDEVFS is a device driver-based filesystem that depends on:
1. Device driver modules (dmdrvi implementations)
2. Configuration files specifying device mappings
3. Actual hardware or mocked drivers

Full integration testing requires these dependencies to be available. The current test suite focuses on verifying the core module builds correctly. Device driver integration testing should be done with specific driver implementations.

## CI/CD Testing

The CI pipeline automatically:
1. Builds the dmdevfs module
2. Runs build verification tests
3. Verifies the module files are created

To add fs_tester to CI in the future, the workflow would need to:
1. Clone and build dmvfs
2. Run fs_tester against the built dmdevfs module
3. Report results

This can be added once device driver mocks or test drivers are available.

