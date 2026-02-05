# dmdevfs

DMOD Driver File System - A driver-based file system module for embedded systems.

## Overview

`dmdevfs` (DMOD Driver File System) is a file system implementation that provides an interface to access files through hardware drivers or external storage. It is built on the **DMOD (Dynamic Modules)** framework and implements the **DMFSI (DMOD File System Interface)**, making it compatible with **DMVFS (DMOD Virtual File System)** and other DMOD-based applications.

### Key Features

- **Driver-Based**: Interfaces with hardware drivers for storage access
- **DMFSI Compatible**: Implements the standard DMOD file system interface
- **DMVFS Integration**: Can be mounted as a file system in DMVFS
- **Modular Design**: Built on DMOD framework for easy integration

## Architecture

DMDEVFS is part of a modular embedded file system architecture built on DMOD:

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│          (Your embedded application code)                    │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                 DMVFS (Virtual File System)                  │
│  • Unified file system interface                             │
│  • Multiple mount points                                     │
│  • Path resolution                                           │
│  https://github.com/choco-technologies/dmvfs                │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│          DMFSI (DMOD File System Interface)                  │
│  • Standardized POSIX-like API                               │
│  • Common interface for all file systems                     │
│  https://github.com/choco-technologies/dmfsi                │
└─────────────────────────────────────────────────────────────┘
                           │
         ┌─────────────────┼─────────────────┐
         ▼                 ▼                 ▼
┌────────────────┐  ┌─────────────┐  ┌─────────────┐
│ DMDEVFS (Driver│  │ DMFFS (Flash│  │ DMRAMFS (RAM│
│ Driver-based   │  │ Read-only   │  │ Temporary   │
│ (This Project) │  │             │  │             │
└────────────────┘  └─────────────┘  └─────────────┘
         │                 │                 │
         ▼                 ▼                 ▼
┌────────────────┐  ┌─────────────┐  ┌─────────────┐
│ Hardware Driver│  │ Flash Memory│  │  RAM        │
└────────────────┘  └─────────────┘  └─────────────┘
```

### Component Relationships

- **[DMOD](https://github.com/choco-technologies/dmod)**: The foundation providing dynamic module loading, inter-module communication, and resource management
- **[DMFSI](https://github.com/choco-technologies/dmfsi)**: Defines the standard file system interface that DMDEVFS implements
- **[DMVFS](https://github.com/choco-technologies/dmvfs)**: Virtual file system layer that can mount DMDEVFS at any path in a unified directory tree
- **DMDEVFS**: This project - implements DMFSI to provide access to driver-based storage

## Building

```bash
mkdir build
cd build
cmake .. -DDMOD_MODE=DMOD_MODULE
cmake --build .
```

### Building with Tests

To build with test support:

```bash
mkdir build
cd build
cmake .. -DDMOD_MODE=DMOD_MODULE -DDMDEVFS_BUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```

See the [tests/README.md](tests/README.md) for more information about testing.

## Usage

The module can be loaded and mounted using DMVFS. **Important:** DMDEVFS requires a configuration path to be specified during mounting.

```c
#include "dmvfs.h"

// Initialize DMVFS
dmvfs_init(16, 32);

// Mount the driver filesystem at /mnt with configuration path
// The configuration path must point to a directory containing driver configuration files
dmvfs_mount_fs("dmdevfs", "/mnt", "/etc/dmdevfs");

// Use standard file operations
void* fp;
dmvfs_fopen(&fp, "/mnt/device0/file.txt", DMFSI_O_RDONLY, 0, 0);
// ... use file ...
dmvfs_fclose(fp);

// Unmount when done
dmvfs_unmount_fs("/mnt");
dmvfs_deinit();
```

**Note:** The configuration parameter cannot be NULL or empty. It must contain a valid path to a directory with driver configuration files. See the [Configuration Files](#configuration-files) section below for detailed information.

## Configuration Files

DMDEVFS uses configuration files to define and initialize device drivers. This is a critical mechanism that allows the filesystem to dynamically discover and configure hardware drivers at mount time.

### Overview

When DMDEVFS is mounted with a configuration path, it:
1. Scans the configuration directory for `.ini` files
2. Reads each configuration file to determine which driver to load
3. Initializes the driver with parameters from the configuration
4. Maps the driver to a device path within the filesystem

### Configuration File Format

Configuration files use the INI format with a `[main]` section:

```ini
[main]
driver_name = your_driver_name
# Additional driver-specific parameters
parameter1 = value1
parameter2 = value2
```

#### Required Fields

- **`driver_name`**: The name of the DMOD driver module to load (must implement dmdrvi interface)

#### Optional Fields

Any additional parameters in the configuration file are passed to the driver's initialization function. The interpretation of these parameters depends on the specific driver implementation.

### Configuration Directory Structure

DMDEVFS supports both flat and hierarchical configuration layouts:

#### Flat Structure
```
/etc/dmdevfs/
├── spi_flash.ini     # Configuration for SPI flash driver
├── i2c_eeprom.ini    # Configuration for I2C EEPROM driver
└── uart_storage.ini  # Configuration for UART storage driver
```

#### Hierarchical Structure
```
/etc/dmdevfs/
├── flash/
│   ├── spi0.ini      # SPI flash on bus 0
│   └── spi1.ini      # SPI flash on bus 1
└── eeprom/
    ├── i2c0.ini      # EEPROM on I2C bus 0
    └── i2c1.ini      # EEPROM on I2C bus 1
```

The hierarchical structure is useful for organizing drivers by type or bus.

### Example Configuration Files

#### Example 1: SPI Flash Driver

**File:** `/etc/dmdevfs/spi_flash.ini`

```ini
[main]
driver_name = dmspiflash
spi_bus = 0
chip_select = 1
speed_hz = 1000000
mode = 0
```

#### Example 2: I2C EEPROM Driver

**File:** `/etc/dmdevfs/eeprom.ini`

```ini
[main]
driver_name = dmi2ceeprom
i2c_bus = 0
device_address = 0x50
page_size = 64
total_size = 8192
```

#### Example 3: UART Storage Driver

**File:** `/etc/dmdevfs/uart_storage.ini`

```ini
[main]
driver_name = dmuartstorage
uart_port = 2
baud_rate = 115200
data_bits = 8
parity = none
stop_bits = 1
```

### How Configuration Files Are Interpreted

1. **File Discovery**: DMDEVFS recursively scans the configuration directory
2. **INI Parsing**: Each `.ini` file is parsed using the dmini module
3. **Driver Loading**: The `driver_name` parameter determines which driver module to load
4. **Driver Initialization**: The entire INI context is passed to the driver's `dmdrvi_create()` function
5. **Device Mapping**: The driver is registered and becomes accessible through the filesystem

### Configuration File Naming

The filename (without `.ini` extension) can serve as a fallback driver name if `driver_name` is not specified in the file:

```ini
# File: my_custom_driver.ini
[main]
# driver_name is optional if filename matches the module name
parameter1 = value1
```

In this case, DMDEVFS will attempt to load a module named `my_custom_driver`.

### Best Practices

1. **Use Descriptive Names**: Name configuration files to reflect their purpose (e.g., `spi_flash_boot.ini`)
2. **Document Parameters**: Add comments in INI files to explain parameter meanings
3. **Validate Configuration**: Ensure driver modules exist before deploying configuration files
4. **Organize by Function**: Use subdirectories to group related drivers
5. **Test Individually**: Test each driver configuration independently before integrating

### Troubleshooting

Common configuration issues:

- **"Config path is NULL/empty"**: Ensure you provide a valid path when mounting
- **"Failed to open config directory"**: Verify the configuration directory exists and is accessible
- **"Failed to read driver for config"**: Check INI file syntax and format
- **"Failed to configure driver"**: Verify the driver module is available and implements dmdrvi interface
- **Driver module not found**: Ensure the driver module is loaded or available in the module repository

### Dynamic Reconfiguration

Currently, DMDEVFS loads configuration at mount time. To apply new configurations:

1. Unmount the filesystem: `dmvfs_unmount_fs("/mnt")`
2. Update configuration files
3. Remount the filesystem: `dmvfs_mount_fs("dmdevfs", "/mnt", "/etc/dmdevfs")`

## API

The module implements the full DMFSI interface:

### File Operations
- `_fopen` - Open a file
- `_fclose` - Close a file
- `_fread` - Read from a file
- `_fwrite` - Write to a file
- `_lseek` - Seek to a position
- `_tell` - Get current position
- `_eof` - Check for end of file
- `_size` - Get file size
- `_getc` - Read a single character
- `_putc` - Write a single character
- `_sync` - Sync file
- `_fflush` - Flush buffers

### Directory Operations
- `_mkdir` - Create a directory
- `_opendir` - Open a directory for reading
- `_readdir` - Read directory entries
- `_closedir` - Close a directory
- `_direxists` - Check if directory exists

### File Management
- `_stat` - Get file/directory statistics
- `_unlink` - Delete a file
- `_rename` - Rename a file

## Project Structure

```
dmdevfs/
├── include/
│   └── dmdevfs.h            # Public header
├── src/
│   └── dmdevfs.c            # Main DMDEVFS implementation
├── CMakeLists.txt           # CMake build configuration
├── manifest.dmm             # DMOD manifest file
├── README.md                # This file
└── LICENSE                  # License file
```

## Development Status

**Note**: This is an initial implementation with the basic structure in place. The actual driver integration and file operations are marked with TODO comments and need to be implemented based on specific hardware driver requirements.

## Integration into Your Project

### Method 1: Using `dmod_link_modules` (Recommended)

The most convenient way to add dmdevfs to your DMOD-based project (in module mode) is to use the `dmod_link_modules` CMake macro:

```cmake
# In your CMakeLists.txt, after setting up your DMOD module

# Link DMDEVFS module to your project
dmod_link_modules(your_module_name
    dmdevfs
    # ... other modules
)
```

This macro automatically:
- Downloads the dmdevfs module from the repository
- Configures it for DMOD module mode
- Links it to your target
- Handles all dependencies (dmfsi, dmdrvi, dmini, dmlist)

### Method 2: Using CMake FetchContent

```cmake
include(FetchContent)

# Fetch DMDEVFS
FetchContent_Declare(
    dmdevfs
    GIT_REPOSITORY https://github.com/choco-technologies/dmdfs.git
    GIT_TAG        main
)

# Set DMOD mode
set(DMOD_MODE "DMOD_MODULE" CACHE STRING "DMOD build mode")

FetchContent_MakeAvailable(dmdevfs)

# Link to your target
target_link_libraries(your_target PRIVATE dmdevfs)
```

### Method 3: Manual Integration

1. Clone the repository: `git clone https://github.com/choco-technologies/dmdfs.git`
2. Add as subdirectory: `add_subdirectory(dmdevfs)`
3. Link library: `target_link_libraries(your_target dmdevfs)`

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes with tests
4. Submit a pull request

## Related Projects

- **[DMOD](https://github.com/choco-technologies/dmod)** - Dynamic module system
- **[DMFSI](https://github.com/choco-technologies/dmfsi)** - File system interface specification
- **[DMVFS](https://github.com/choco-technologies/dmvfs)** - Virtual file system implementation
- **[DMFFS](https://github.com/choco-technologies/dmffs)** - Flash file system implementation
- **[DMRAMFS](https://github.com/choco-technologies/dmramfs)** - RAM file system implementation

## License

See [LICENSE](LICENSE) file for details.

## Author

Patryk Kubiak

## Support

For questions, issues, or feature requests, please open an issue on GitHub:
https://github.com/choco-technologies/dmdfs/issues
