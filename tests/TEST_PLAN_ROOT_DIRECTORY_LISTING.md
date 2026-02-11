# Test Plan: Root Directory Listing

## Issue
Root directory listing was not working correctly in dmdevfs. When a driver like `dmclk` was mounted in `/dev`, attempting to list `/dev` returned an empty list.

## Root Cause
The `read_driver_parent_directory()` function incorrectly determined the parent directory for drivers with only a major device number. It returned `"dmclk0/"` instead of `"/"`, causing the directory listing logic to skip these drivers when enumerating the root directory.

## Fix Applied
Modified `format_parent_directory_path()` to correctly handle all device number combinations:
- **Major + Minor**: Parent is `"driverN/"` (e.g., `"dmclk0/"`)
- **Only Minor**: Parent is `"driverx/"` (e.g., `"dmclkx/"`)
- **Only Major**: Parent is `"/"` (e.g., driver `"dmclk0"` in root) - **THIS WAS THE BUG**
- **Neither**: Parent is `"/"` (e.g., driver `"dmclk"` in root)

## Test Scenarios

### Test 1: List root directory with driver having no device numbers
**Setup:**
- Configure a driver (e.g., `dmclk`) with neither major nor minor device numbers
- Mount dmdevfs at `/dev`

**Expected Result:**
- Listing `/dev` should show the driver name (e.g., `dmclk`)
- Path to driver: `/dev/dmclk`
- Parent directory: `/`

**Test Command (with fs_tester):**
```bash
# Configure driver without device numbers
echo "[main]\ndriver_name=dmclk" > /tmp/config/dmclk.ini

# Mount and list
fs_tester --mount-path /dev --config-path /tmp/config
fs_tester --list-dir /dev
```

**Expected Output:**
```
dmclk
```

### Test 2: List root directory with driver having only major number
**Setup:**
- Configure a driver (e.g., `dmclk`) that returns only a major device number (e.g., major=0)
- Mount dmdevfs at `/dev`

**Expected Result:**
- Listing `/dev` should show `dmclk0`
- Path to driver: `/dev/dmclk0`
- Parent directory: `/`

**Test Command:**
```bash
# Configure driver with only major=0
echo "[main]\ndriver_name=dmclk\nmajor=0" > /tmp/config/dmclk.ini

# Mount and list
fs_tester --mount-path /dev --config-path /tmp/config
fs_tester --list-dir /dev
```

**Expected Output:**
```
dmclk0
```

### Test 3: List root directory with driver having only minor number
**Setup:**
- Configure a driver that returns only a minor device number (e.g., minor=0)
- Mount dmdevfs at `/dev`

**Expected Result:**
- Listing `/dev` should show directory `dmclkx/`
- Listing `/dev/dmclkx/` should show file `0`
- Path to driver: `/dev/dmclkx/0`
- Parent directory of file: `/dev/dmclkx/`
- Parent directory of directory: `/`

**Test Command:**
```bash
# Configure driver with only minor=0
echo "[main]\ndriver_name=dmclk\nminor=0" > /tmp/config/dmclk.ini

# Mount and list
fs_tester --mount-path /dev --config-path /tmp/config
fs_tester --list-dir /dev
fs_tester --list-dir /dev/dmclkx/
```

**Expected Output:**
```
/dev:
dmclkx/

/dev/dmclkx/:
0
```

### Test 4: List root directory with driver having both major and minor
**Setup:**
- Configure a driver that returns both major and minor device numbers (e.g., major=0, minor=1)
- Mount dmdevfs at `/dev`

**Expected Result:**
- Listing `/dev` should show directory `dmclk0/`
- Listing `/dev/dmclk0/` should show file `1`
- Path to driver: `/dev/dmclk0/1`
- Parent directory of file: `/dev/dmclk0/`
- Parent directory of directory: `/`

**Test Command:**
```bash
# Configure driver with major=0, minor=1
echo "[main]\ndriver_name=dmclk\nmajor=0\nminor=1" > /tmp/config/dmclk.ini

# Mount and list
fs_tester --mount-path /dev --config-path /tmp/config
fs_tester --list-dir /dev
fs_tester --list-dir /dev/dmclk0/
```

**Expected Output:**
```
/dev:
dmclk0/

/dev/dmclk0/:
1
```

### Test 5: List root directory with multiple drivers
**Setup:**
- Configure multiple drivers with different device number combinations
- Mount dmdevfs at `/dev`

**Example Configuration:**
```
/tmp/config/
├── driver1.ini  # driver_name=dmclk, no numbers -> /dev/dmclk
├── driver2.ini  # driver_name=dmspi, major=0 -> /dev/dmspi0
├── driver3.ini  # driver_name=dmi2c, major=1 -> /dev/dmi2c1
└── driver4.ini  # driver_name=dmuart, major=0, minor=1 -> /dev/dmuart0/1
```

**Expected Result:**
- Listing `/dev` should show all drivers:
  - `dmclk` (file)
  - `dmspi0` (file)
  - `dmi2c1` (file)
  - `dmuart0/` (directory)

**Test Command:**
```bash
fs_tester --mount-path /dev --config-path /tmp/config
fs_tester --list-dir /dev
```

**Expected Output:**
```
dmclk
dmspi0
dmi2c1
dmuart0/
```

## Manual Testing with dmvfs

When dmvfs and a test driver are available, the fix can be verified manually:

```c
#include "dmvfs.h"

int main(void) {
    // Initialize DMVFS
    dmvfs_init(16, 32);
    
    // Mount dmdevfs at /dev with config
    dmvfs_mount_fs("dmdevfs", "/dev", "/tmp/driver_config");
    
    // List root directory
    void* dp;
    dmfsi_dir_entry_t entry;
    
    if (dmvfs_opendir(&dp, "/dev") == DMFSI_OK) {
        printf("Contents of /dev:\n");
        while (dmvfs_readdir(dp, &entry) == DMFSI_OK) {
            printf("  %s%s\n", entry.name, 
                   (entry.attr & DMFSI_ATTR_DIRECTORY) ? "/" : "");
        }
        dmvfs_closedir(dp);
    }
    
    dmvfs_unmount_fs("/dev");
    dmvfs_deinit();
    return 0;
}
```

## Automated Test Implementation

When test infrastructure is available, create automated tests in `tests/test_root_listing.c`:

```c
// Test that root directory listing works with various driver configurations
void test_root_directory_listing_with_major_only(void) {
    // Setup driver with major=0
    // Mount filesystem
    // List root
    // Verify driver appears in listing
    // Assert parent directory is "/"
}

void test_root_directory_listing_with_no_numbers(void) {
    // Setup driver with no device numbers
    // Mount filesystem
    // List root
    // Verify driver appears in listing
    // Assert parent directory is "/"
}

void test_root_directory_listing_multiple_drivers(void) {
    // Setup multiple drivers with different configurations
    // Mount filesystem
    // List root
    // Verify all appropriate drivers appear
}
```

## Regression Testing

Before this fix:
- ❌ Drivers with only major number did NOT appear in root directory listing
- ❌ Drivers with no device numbers appeared correctly
- ❌ Root directory appeared empty when only major-numbered drivers existed

After this fix:
- ✅ Drivers with only major number appear in root directory listing
- ✅ Drivers with no device numbers appear in root directory listing
- ✅ Root directory shows all top-level drivers regardless of numbering scheme

## Notes for Future Testing

1. **CI Integration**: When dmf-get and device driver mocks are available, add these tests to CI pipeline
2. **Test Drivers**: Create simple mock drivers that can be configured with different device number combinations
3. **Edge Cases**: Test with empty config directories, invalid configurations, etc.
4. **Performance**: Test with large numbers of drivers to ensure listing performance is acceptable
