#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmfsi.h"
#include "dmdevfs.h"
#include "dmdevfs_types.h"
#include "dmdevfs_context.h"
#include "dmdevfs_scan.h"

/**
 * @brief Initialize the file system
 * @param config Configuration string (FS-specific)
 * @return Context pointer on success, NULL on failure
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  dmfsi_context_t, _init, (const char* config) )
{
    dmfsi_context_t context = context_new();

    bool success = scan(context, config, NULL);
    if(success)
    {
        
    }

    return context;
}

/**
 * @brief Deinitialize the file system
 * @param ctx File system context
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _deinit, (dmfsi_context_t ctx) )
{
    return 0;
}

/**
 * @brief Validate the file system context
 * @param ctx File system context
 * @return 1 if context is valid, 0 otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _context_is_valid, (dmfsi_context_t ctx) )
{
    return 0;
}

/**
 * @brief Open a file
 * @param ctx File system context
 * @param fp Pointer to store the file handle
 * @param path Path to the file
 * @param mode Open mode (DMFSI_O_*)
 * @param attr File attributes (DMFSI_ATTR_*)
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _fopen, (dmfsi_context_t ctx, void** fp, const char* path, int mode, int attr) )
{
    return 0;
}

/**
 * @brief Close a file
 * @param ctx File system context
 * @param fp File handle
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _fclose, (dmfsi_context_t ctx, void* fp) )
{
    return 0;
}

/**
 * @brief Read from a file
 * @param ctx File system context
 * @param fp File handle
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @param read Pointer to store the number of bytes actually read
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _fread, (dmfsi_context_t ctx, void* fp, void* buffer, size_t size, size_t* read) )
{
    return -1;
}

/**
 * @brief Write to a file
 * @param ctx File system context
 * @param fp File handle
 * @param buffer Buffer to write from
 * @param size Number of bytes to write
 * @param written Pointer to store the number of bytes actually written
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _fwrite, (dmfsi_context_t ctx, void* fp, const void* buffer, size_t size, size_t* written) )
{
    return -1;
}

/**
 * @brief Seek to a position in a file
 * @param ctx File system context
 * @param fp File handle
 * @param offset Offset to seek to
 * @param whence Seek mode (DMFSI_SEEK_*)
 * @return The new position, or negative error code
 */
dmod_dmfsi_dif_api_declaration( 2.0, dmdevfs, dmfsi_offset_t, _lseek, (dmfsi_context_t ctx, void* fp, dmfsi_offset_t offset, int whence) )
{
    return 0;
}

/**
 * @brief Perform I/O control operation
 * @param ctx File system context
 * @param fp File handle
 * @param request Request code
 * @param arg Argument (request-specific)
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _ioctl, (dmfsi_context_t ctx, void* fp, int request, void* arg) )
{
    return -1;
}

/**
 * @brief Synchronize file data to storage
 * @param ctx File system context
 * @param fp File handle
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _sync, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

/**
 * @brief Get a character from file
 * @param ctx File system context
 * @param fp File handle
 * @return Character read, or negative error code
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _getc, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

/**
 * @brief Put a character to file
 * @param ctx File system context
 * @param fp File handle
 * @param c Character to write
 * @return Character written, or negative error code
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _putc, (dmfsi_context_t ctx, void* fp, int c) )
{
    return -1;
}

/**
 * @brief Get current file position
 * @param ctx File system context
 * @param fp File handle
 * @return Current position, or negative error code
 */
dmod_dmfsi_dif_api_declaration( 2.0, dmdevfs, dmfsi_offset_t, _tell, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

/**
 * @brief Check if at end of file
 * @param ctx File system context
 * @param fp File handle
 * @return 1 if at EOF, 0 otherwise, negative on error
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _eof, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

/**
 * @brief Get file size
 * @param ctx File system context
 * @param fp File handle
 * @return File size in bytes, or negative error code
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  dmfsi_size_t, _size, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

/**
 * @brief Flush file buffers
 * @param ctx File system context
 * @param fp File handle
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _fflush, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

/**
 * @brief Get last error code
 * @param ctx File system context
 * @param fp File handle
 * @return Error code
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _error, (dmfsi_context_t ctx, void* fp) )
{
    return -1;
}

/**
 * @brief Open a directory
 * @param ctx File system context
 * @param dp Pointer to store the directory handle
 * @param path Path to the directory
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _opendir, (dmfsi_context_t ctx, void** dp, const char* path) )
{
    return -1;
}

/**
 * @brief Close a directory
 * @param ctx File system context
 * @param dp Directory handle
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _closedir, (dmfsi_context_t ctx, void* dp) )
{
    return -1;
}

/**
 * @brief Read directory entry
 * @param ctx File system context
 * @param dp Directory handle
 * @param entry Pointer to store the directory entry
 * @return DMFSI_OK on success, error code otherwise (DMFSI_ERR_NOT_FOUND at end)
 */
dmod_dmfsi_dif_api_declaration( 2.0, dmdevfs, int, _readdir, (dmfsi_context_t ctx, void* dp, dmfsi_dir_entry_t* entry) )
{
    return -1;
}

/**
 * @brief Get file/directory statistics
 * @param ctx File system context
 * @param path Path to the file/directory
 * @param stat Pointer to store the statistics
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 2.0, dmdevfs, int, _stat, (dmfsi_context_t ctx, const char* path, dmfsi_stat_t* stat) )
{
    return -1;
}

/**
 * @brief Delete a file
 * @param ctx File system context
 * @param path Path to the file
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _unlink, (dmfsi_context_t ctx, const char* path) )
{
    return -1;
}

/**
 * @brief Rename a file or directory
 * @param ctx File system context
 * @param oldpath Current path
 * @param newpath New path
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _rename, (dmfsi_context_t ctx, const char* oldpath, const char* newpath) )
{
    return -1;
}

/**
 * @brief Change file mode/permissions
 * @param ctx File system context
 * @param path Path to the file
 * @param mode New mode/permissions
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _chmod, (dmfsi_context_t ctx, const char* path, int mode) )
{
    return -1;
}

/**
 * @brief Change file access and modification times
 * @param ctx File system context
 * @param path Path to the file
 * @param atime Access time
 * @param mtime Modification time
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _utime, (dmfsi_context_t ctx, const char* path, uint32_t atime, uint32_t mtime) )
{
    return -1;
}

/**
 * @brief Create a directory
 * @param ctx File system context
 * @param path Path to the directory
 * @param mode Directory mode/permissions
 * @return DMFSI_OK on success, error code otherwise
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _mkdir, (dmfsi_context_t ctx, const char* path, int mode) )
{
    return -1;
}

/**
 * @brief Check if directory exists
 * @param ctx File system context
 * @param path Path to the directory
 * @return 1 if exists, 0 if not, negative on error
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  int, _direxists, (dmfsi_context_t ctx, const char* path) )
{
    return -1;
}

/**
 * @brief Notify a file system that it has been fully mounted
 *
 * Called by the mounter (e.g. dmvfs) once the mount is fully registered,
 * handing the file system its own absolute mount path directly (the mounter
 * already has it in hand - it's the same path it was told to mount at). A
 * file system that needs to resolve its own absolute mount path for
 * something it already set up during _init() (e.g. a driver-backed file
 * system pushing paths down to its drivers, see dmdrvi_path_ready() in
 * dmdrvi.h) should cache it from here, not try to resolve it from inside
 * _init() itself, where the mount is not registered anywhere yet.
 *
 * Implementing this dif is optional - a file system that doesn't need its
 * own mount path can simply not implement it.
 *
 * @param ctx File system context
 * @param mount_path Absolute, null-terminated path at which ctx is mounted
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs,  void, _mounted, (dmfsi_context_t ctx, const char* mount_path) )
{
    
}
