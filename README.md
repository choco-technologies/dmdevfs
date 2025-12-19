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

## Usage

The module can be loaded and mounted using DMVFS:

```c
#include "dmvfs.h"

// Initialize DMVFS
dmvfs_init(16, 32);

// Mount the driver filesystem at /mnt
dmvfs_mount_fs("dmdevfs", "/mnt", NULL);

// Use standard file operations
void* fp;
dmvfs_fopen(&fp, "/mnt/file.txt", DMFSI_O_RDONLY, 0, 0);
// ... use file ...
dmvfs_fclose(fp);

// Unmount when done
dmvfs_unmount_fs("/mnt");
dmvfs_deinit();
```

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

### Using CMake FetchContent

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

### Manual Integration

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
