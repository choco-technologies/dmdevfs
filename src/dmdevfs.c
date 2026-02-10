/**
 * @file dmdevfs.c
 * @brief DMOD Driver File System - Implementation
 * @author Patryk Kubiak
 * 
 * This is a driver-based file system that provides an interface to access
 * files through hardware drivers or external storage.
 */

#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod.h"
#include "dmdevfs.h"
#include "dmfsi.h"
#include "dmlist.h"
#include "dmini.h"
#include "dmdrvi.h"
#include <string.h>

/** 
 * @brief Magic number for DMDEVFS context validation
 */
#define DMDEVFS_CONTEXT_MAGIC 0x444D4456  // 'DMDV'
#define ROOT_DIRECTORY_NAME "/"
#define MAX_PATH_LENGTH     (DMOD_MAX_MODULE_NAME_LENGTH + 20)

/**
 * @brief Type definition for path strings
 */
typedef char path_t[MAX_PATH_LENGTH];

typedef struct 
{
    dmdrvi_context_t driver_context;    // Driver-specific context
    Dmod_Context_t*  driver;            // Driver module context
    dmdrvi_dev_num_t dev_num;           // Device number assigned to the driver
    bool was_loaded;                    // Indicates if the driver was loaded by dmdevfs
    bool was_enabled;                   // Indicates if the driver was enabled by dmdevfs
    path_t path;                        // Path associated with the driver
} driver_node_t;

typedef struct
{
    driver_node_t* driver;   // Last driver
    char* directory_path;   // Directory path
} directory_node_t;

/**
 * @brief File handle structure for file operations
 */
typedef struct
{
    driver_node_t* driver;      // Driver associated with this file
    void* driver_handle;        // Driver device handle
    const char* path;           // File path
    int mode;                   // File open mode
    int attr;                   // File attributes
} file_handle_t;

/**
 * @brief File system context structure
 */
struct dmfsi_context
{
    uint32_t    magic;
    char* config_path;          // Path with the configuration files
    dmlist_context_t* drivers;  // List of loaded drivers
};


// ============================================================================
//                      Local prototypes
// ============================================================================
static int configure_drivers(dmfsi_context_t ctx, const char* driver_name, const char* config_path);
static driver_node_t* configure_driver(const char* driver_name, dmini_context_t config_ctx);
static int unconfigure_drivers(dmfsi_context_t ctx);
static bool is_file(const char* path);
static bool is_driver( const char* name);
static void read_base_name(const char* path, char* base_name, size_t name_size);
static dmini_context_t read_driver_for_config(const char* config_path, char* driver_name, size_t name_size, const char* default_driver);
static Dmod_Context_t* prepare_driver_module(const char* driver_name, bool* was_loaded, bool* was_enabled);
static void cleanup_driver_module(const char* driver_name, bool was_loaded, bool was_enabled);
static int read_driver_parent_directory( const driver_node_t* node, char* path_buffer, size_t buffer_size );
static int read_driver_node_path( const driver_node_t* node, char* path_buffer, size_t buffer_size );
static int compare_driver_directory( const void* data, const void* user_data );
static int compare_driver_node_path( const void* data, const void* user_data );
static int compare_driver(const void* data, const void* user_data );
static bool is_directory( dmfsi_context_t ctx, const char* path );
static driver_node_t* get_next_driver_node( dmfsi_context_t ctx, driver_node_t* current, const char* dir_path );

static driver_node_t* find_driver_node( dmfsi_context_t ctx, const char* path );
static int driver_stat( driver_node_t* context, const char* path, dmdrvi_stat_t* stat );

// ============================================================================
//                      Module Interface Implementation
// ============================================================================

/**
 * @brief Module pre-initialization (optional)
 */
void dmod_preinit(void)
{
    // Nothing to do
}

/**
 * @brief Module initialization
 */
int dmod_init(const Dmod_Config_t *Config)
{
    // Nothing to do
    return 0;
}

/**
 * @brief Module deinitialization
 */
int dmod_deinit(void)
{
    // Nothing to do
    return 0;
}

// ============================================================================
//                      DMFSI Interface Implementation
// ============================================================================

/**
 * @brief Initialize the file system
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, dmfsi_context_t, _init, (const char* config) )
{
    if(config == NULL)
    {
        DMOD_LOG_ERROR("Config path is NULL\n");
        return NULL;
    }

    if(strlen(config) == 0)
    {
        DMOD_LOG_ERROR("Config path is empty\n");
        return NULL;
    }

    dmfsi_context_t ctx = Dmod_Malloc(sizeof(struct dmfsi_context));
    if (ctx == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate memory for context\n");
        return NULL;
    }
    
    ctx->magic = DMDEVFS_CONTEXT_MAGIC;
    ctx->config_path = Dmod_StrDup(config);
    ctx->drivers = dmlist_create(DMOD_MODULE_NAME);
    
    int res = configure_drivers(ctx, NULL, ctx->config_path);
    if (res != DMFSI_OK)
    {
        DMOD_LOG_ERROR("Failed to configure drivers\n");
        unconfigure_drivers(ctx);
        dmlist_destroy(ctx->drivers);
        Dmod_Free(ctx->config_path);
        Dmod_Free(ctx);
        return NULL;
    }
    
    return ctx;
}

/**
 * @brief Validate the file system context
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _context_is_valid, (dmfsi_context_t ctx) )
{
    return (ctx && ctx->magic == DMDEVFS_CONTEXT_MAGIC) ? 1 : 0;
}

/**
 * @brief Deinitialize the file system
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _deinit, (dmfsi_context_t ctx) )
{
    if (!dmfsi_dmdevfs_context_is_valid(ctx))
    {
        DMOD_LOG_ERROR("Invalid context in deinit\n");
        return DMFSI_ERR_INVALID;
    }

    unconfigure_drivers(ctx);
    dmlist_destroy(ctx->drivers);
    Dmod_Free(ctx->config_path);
    Dmod_Free(ctx);
    return DMFSI_OK;
}

/**
 * @brief Open a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _fopen, (dmfsi_context_t ctx, void** fp, const char* path, int mode, int attr) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in fopen\n");
        return DMFSI_ERR_INVALID;
    }
    
    if(fp == NULL || path == NULL)
    {
        DMOD_LOG_ERROR("NULL pointer in fopen\n");
        return DMFSI_ERR_INVALID;
    }
    
    // Find the driver node for this file
    driver_node_t* driver_node = find_driver_node(ctx, path);
    if(driver_node == NULL)
    {
        DMOD_LOG_ERROR("File not found: %s\n", path);
        return DMFSI_ERR_NOT_FOUND;
    }
    
    // Get the dmdrvi_open function
    dmod_dmdrvi_open_t dmdrvi_open = Dmod_GetDifFunction(driver_node->driver, dmod_dmdrvi_open_sig);
    if(dmdrvi_open == NULL)
    {
        DMOD_LOG_ERROR("Driver does not implement dmdrvi_open\n");
        return DMFSI_ERR_NOT_FOUND;
    }
    
    // Create file handle
    file_handle_t* handle = Dmod_Malloc(sizeof(file_handle_t));
    if(handle == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate memory for file handle\n");
        return DMFSI_ERR_GENERAL;
    }
    
    // Open the device through the driver
    // Note: dmdrvi_open only takes context and flags, returns device handle
    handle->driver_handle = dmdrvi_open(driver_node->driver_context, mode);
    if(handle->driver_handle == NULL)
    {
        DMOD_LOG_ERROR("Driver failed to open device: %s\n", path);
        Dmod_Free(handle);
        return DMFSI_ERR_GENERAL;
    }
    
    handle->driver = driver_node;
    handle->path = Dmod_StrDup(path);
    handle->mode = mode;
    handle->attr = attr;
    
    *fp = handle;
    return DMFSI_OK;
}

/**
 * @brief Close a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _fclose, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in fclose\n");
        return DMFSI_ERR_INVALID;
    }
    
    if(fp == NULL)
    {
        DMOD_LOG_ERROR("NULL file pointer in fclose\n");
        return DMFSI_ERR_INVALID;
    }
    
    file_handle_t* handle = (file_handle_t*)fp;
    
    // Get the dmdrvi_close function
    dmod_dmdrvi_close_t dmdrvi_close = Dmod_GetDifFunction(handle->driver->driver, dmod_dmdrvi_close_sig);
    if(dmdrvi_close != NULL)
    {
        dmdrvi_close(handle->driver->driver_context, handle->driver_handle);
    }
    
    // Free the path string that was duplicated in fopen
    if(handle->path)
    {
        Dmod_Free((void*)handle->path);
    }
    
    Dmod_Free(handle);
    return DMFSI_OK;
}

/**
 * @brief Read from a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _fread, (dmfsi_context_t ctx, void* fp, void* buffer, size_t size, size_t* read) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in fread\n");
        if(read) *read = 0;
        return DMFSI_ERR_INVALID;
    }
    
    if(fp == NULL || buffer == NULL)
    {
        if(read) *read = 0;
        return DMFSI_ERR_INVALID;
    }
    
    file_handle_t* handle = (file_handle_t*)fp;
    
    // Get the dmdrvi_read function
    dmod_dmdrvi_read_t dmdrvi_read = Dmod_GetDifFunction(handle->driver->driver, dmod_dmdrvi_read_sig);
    if(dmdrvi_read == NULL)
    {
        DMOD_LOG_ERROR("Driver does not implement dmdrvi_read\n");
        if(read) *read = 0;
        return DMFSI_ERR_NOT_FOUND;
    }
    
    // dmdrvi_read returns size_t (bytes read), not error code
    size_t bytes_read = dmdrvi_read(handle->driver->driver_context, handle->driver_handle, buffer, size);
    if(read) *read = bytes_read;
    
    return DMFSI_OK;
}

/**
 * @brief Write to a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _fwrite, (dmfsi_context_t ctx, void* fp, const void* buffer, size_t size, size_t* written) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in fwrite\n");
        if(written) *written = 0;
        return DMFSI_ERR_INVALID;
    }
    
    if(fp == NULL || buffer == NULL)
    {
        if(written) *written = 0;
        return DMFSI_ERR_INVALID;
    }
    
    file_handle_t* handle = (file_handle_t*)fp;
    
    // Get the dmdrvi_write function
    dmod_dmdrvi_write_t dmdrvi_write = Dmod_GetDifFunction(handle->driver->driver, dmod_dmdrvi_write_sig);
    if(dmdrvi_write == NULL)
    {
        DMOD_LOG_ERROR("Driver does not implement dmdrvi_write\n");
        if(written) *written = 0;
        return DMFSI_ERR_NOT_FOUND;
    }
    
    // dmdrvi_write returns size_t (bytes written), not error code
    size_t bytes_written = dmdrvi_write(handle->driver->driver_context, handle->driver_handle, buffer, size);
    if(written) *written = bytes_written;
    
    return DMFSI_OK;
}

/**
 * @brief Seek to a position in a file
 * @note Not supported for device drivers - devices are typically non-seekable
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _lseek, (dmfsi_context_t ctx, void* fp, long offset, int whence) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in lseek\n");
        return DMFSI_ERR_INVALID;
    }
    
    if(fp == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    
    // Device drivers typically don't support seek operations
    // Return error to indicate operation not supported
    DMOD_LOG_ERROR("lseek not supported for device drivers\n");
    return DMFSI_ERR_GENERAL;
}

/**
 * @brief Get current position in a file
 * @note Not supported for device drivers - devices are typically non-seekable
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, long, _tell, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in tell\n");
        return -1;
    }
    
    if(fp == NULL)
    {
        return -1;
    }
    
    // Device drivers typically don't support tell operations
    DMOD_LOG_ERROR("tell not supported for device drivers\n");
    return -1;
}

/**
 * @brief Check if at end of file
 * @note Device drivers typically operate in streaming mode - always return 0 (not at EOF)
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _eof, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in eof\n");
        return 1;
    }
    
    if(fp == NULL)
    {
        return 1;
    }
    
    // Device drivers typically don't have EOF concept
    // Return 0 (not at EOF) as devices can always potentially provide more data
    return 0;
}

/**
 * @brief Get file size
 * @note Device drivers represent devices, not files with fixed sizes. Use stat for size info.
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, long, _size, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in size\n");
        return -1;
    }
    
    if(fp == NULL)
    {
        return -1;
    }
    
    file_handle_t* handle = (file_handle_t*)fp;
    
    // Try to get size from stat if available
    dmdrvi_stat_t stat = {0};
    int result = driver_stat(handle->driver, handle->path, &stat);
    if(result == 0)
    {
        return (long)stat.size;
    }
    
    // Size not available for this device
    return -1;
}

/**
 * @brief Read a single character
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _getc, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in getc\n");
        return -1;
    }
    
    if(fp == NULL)
    {
        return -1;
    }
    
    unsigned char ch;
    size_t bytes_read = 0;
    
    int result = dmfsi_dmdevfs_fread(ctx, fp, &ch, 1, &bytes_read);
    if(result != DMFSI_OK || bytes_read != 1)
    {
        return -1;
    }
    
    return (int)ch;
}

/**
 * @brief Write a single character
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _putc, (dmfsi_context_t ctx, void* fp, char c) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in putc\n");
        return -1;
    }
    
    if(fp == NULL)
    {
        return -1;
    }
    
    unsigned char ch = (unsigned char)c;
    size_t bytes_written = 0;
    
    int result = dmfsi_dmdevfs_fwrite(ctx, fp, &ch, 1, &bytes_written);
    if(result != DMFSI_OK || bytes_written != 1)
    {
        return -1;
    }
    
    return (int)ch;
}

/**
 * @brief Flush file buffers
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _fflush, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in fflush\n");
        return DMFSI_ERR_INVALID;
    }
    
    if(fp == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    
    file_handle_t* handle = (file_handle_t*)fp;
    
    // Get the dmdrvi_flush function
    dmod_dmdrvi_flush_t dmdrvi_flush = Dmod_GetDifFunction(handle->driver->driver, dmod_dmdrvi_flush_sig);
    if(dmdrvi_flush == NULL)
    {
        // Flush not supported by driver, return OK
        return DMFSI_OK;
    }
    
    int result = dmdrvi_flush(handle->driver->driver_context, handle->driver_handle);
    if(result != 0)
    {
        return DMFSI_ERR_GENERAL;
    }
    
    return DMFSI_OK;
}

/**
 * @brief Sync file to storage
 * @note For device drivers, sync/flush are equivalent operations
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _sync, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in sync\n");
        return DMFSI_ERR_INVALID;
    }
    
    if(fp == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    
    file_handle_t* handle = (file_handle_t*)fp;
    
    // Get the dmdrvi_flush function (sync and flush are equivalent for devices)
    dmod_dmdrvi_flush_t dmdrvi_flush = Dmod_GetDifFunction(handle->driver->driver, dmod_dmdrvi_flush_sig);
    if(dmdrvi_flush == NULL)
    {
        // Sync not supported by driver, return OK
        return DMFSI_OK;
    }
    
    int result = dmdrvi_flush(handle->driver->driver_context, handle->driver_handle);
    if(result != 0)
    {
        return DMFSI_ERR_GENERAL;
    }
    
    return DMFSI_OK;
}

/**
 * @brief Open a directory
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _opendir, (dmfsi_context_t ctx, void** dp, const char* path) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in opendir\n");
        return DMFSI_ERR_INVALID;
    }
    
    if (!is_directory(ctx, path))
    {
        DMOD_LOG_ERROR("Directory not found: %s\n", path);
        return DMFSI_ERR_NOT_FOUND;
    }

    directory_node_t* dir_node = Dmod_Malloc(sizeof(directory_node_t));
    if (dir_node == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate memory for directory node\n");
        return DMFSI_ERR_GENERAL;
    }
    dir_node->driver = get_next_driver_node(ctx, NULL, path);
    dir_node->directory_path = Dmod_StrDup(path);
    
    *dp = dir_node;


    return DMFSI_OK;
}

/**
 * @brief Read directory entry
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _readdir, (dmfsi_context_t ctx, void* dp, dmfsi_dir_entry_t* entry) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in readdir\n");
        return DMFSI_ERR_INVALID;
    }
    
    directory_node_t* dir_node = (directory_node_t*)dp;
    if (dir_node->driver == NULL)
    {
        return DMFSI_ERR_NOT_FOUND; // No more entries
    }
    driver_node_t* driver = dir_node->driver;

    path_t parent_dir;
    if (read_driver_parent_directory(dir_node->driver, parent_dir, sizeof(parent_dir)) != 0)
    {
        DMOD_LOG_ERROR("Failed to read parent directory for driver\n");
        return DMFSI_ERR_GENERAL;
    }

    bool file_should_be_listed = strcmp(dir_node->directory_path, parent_dir) == 0;
    if(file_should_be_listed)
    {
        strncpy(entry->name, driver->path, sizeof(entry->name));
        
        dmdrvi_stat_t stat;
        int res = driver_stat(driver, driver->path, &stat);
        if (res != 0)
        {
            DMOD_LOG_ERROR("Failed to get file stats for: %s\n", driver->path);
            return DMFSI_ERR_GENERAL;
        }

        entry->size = stat.size;
        entry->attr = stat.mode;
    }
    else 
    {
        strncpy(entry->name, parent_dir, sizeof(entry->name));
        entry->size = 0;
        entry->attr = DMFSI_ATTR_DIRECTORY;
    }

    // Move to next driver for subsequent call
    dir_node->driver = get_next_driver_node(ctx, driver, dir_node->directory_path);
    return DMFSI_OK;
}

/**
 * @brief Close a directory
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _closedir, (dmfsi_context_t ctx, void* dp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in closedir\n");
        return DMFSI_ERR_INVALID;
    }
    
    directory_node_t* dir_node = (directory_node_t*)dp;
    Dmod_Free(dir_node->directory_path);
    Dmod_Free(dir_node);
    return DMFSI_OK;
}

/**
 * @brief Create a directory
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _mkdir, (dmfsi_context_t ctx, const char* path) )
{   
    return DMFSI_ERR_INVALID; // Not supported
}

/**
 * @brief Check if directory exists
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _direxists, (dmfsi_context_t ctx, const char* path) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in direxists\n");
        return 0;
    }
    
    return is_directory(ctx, path) ? 1 : 0;
}

/**
 * @brief Get file/directory statistics
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _stat, (dmfsi_context_t ctx, const char* path, dmfsi_stat_t* stat) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in stat\n");
        return DMFSI_ERR_INVALID;
    }
    
    driver_node_t* driver_node = find_driver_node(ctx, path);
    if (driver_node == NULL)
    {
        DMOD_LOG_ERROR("File not found in stat: %s\n", path);
        return DMFSI_ERR_NOT_FOUND;
    }

    dmdrvi_stat_t driver_stat_buf;
    int res = driver_stat(driver_node, path, &driver_stat_buf);
    if (res != 0)
    {
        DMOD_LOG_ERROR("Failed to get file stats for: %s\n", path);
        return DMFSI_ERR_GENERAL;
    }

    stat->size = driver_stat_buf.size;
    stat->attr = driver_stat_buf.mode;
    return DMFSI_OK;
}

/**
 * @brief Delete a file
 * @note Not supported for device drivers - devices cannot be deleted through filesystem operations
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _unlink, (dmfsi_context_t ctx, const char* path) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in unlink\n");
        return DMFSI_ERR_INVALID;
    }
    
    if(path == NULL)
    {
        DMOD_LOG_ERROR("NULL path in unlink\n");
        return DMFSI_ERR_INVALID;
    }
    
    // Device files cannot be deleted through filesystem operations
    DMOD_LOG_ERROR("unlink not supported for device drivers\n");
    return DMFSI_ERR_GENERAL;
}

/**
 * @brief Rename a file
 * @note Not supported for device drivers - devices cannot be renamed through filesystem operations
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _rename, (dmfsi_context_t ctx, const char* oldpath, const char* newpath) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in rename\n");
        return DMFSI_ERR_INVALID;
    }
    
    if(oldpath == NULL || newpath == NULL)
    {
        DMOD_LOG_ERROR("NULL path in rename\n");
        return DMFSI_ERR_INVALID;
    }
    
    // Device files cannot be renamed through filesystem operations
    DMOD_LOG_ERROR("rename not supported for device drivers\n");
    return DMFSI_ERR_GENERAL;
}


// ============================================================================
//                      Local functions
// ============================================================================

/**
 * @brief Configure drivers based on the configuration file
 */
static int configure_drivers(dmfsi_context_t ctx, const char* driver_name, const char* config_path)
{
    void* dir = Dmod_OpenDir(config_path);
    if (dir == NULL)
    {
        DMOD_LOG_ERROR("Failed to open config directory: %s\n", config_path);
        return DMFSI_ERR_NOT_FOUND;
    }

    const char* entry;
    while ((entry = Dmod_ReadDir(dir)) != NULL)
    {
        // Construct full path for the entry
        char full_path[MAX_PATH_LENGTH];
        size_t config_path_len = strlen(config_path);
        size_t entry_len = strlen(entry);
        
        // Check if we need a separator
        bool needs_separator = (config_path_len > 0 && config_path[config_path_len - 1] != '/');
        size_t required_len = config_path_len + (needs_separator ? 1 : 0) + entry_len + 1;
        
        if (required_len > MAX_PATH_LENGTH)
        {
            DMOD_LOG_ERROR("Path too long: %s/%s\n", config_path, entry);
            continue;
        }
        
        Dmod_SnPrintf(full_path, sizeof(full_path), "%s%s%s", 
                      config_path, 
                      needs_separator ? "/" : "", 
                      entry);
        
        if (is_file(full_path))
        {
            char module_name[DMOD_MAX_MODULE_NAME_LENGTH];
            dmini_context_t config_ctx = read_driver_for_config(full_path, module_name, sizeof(module_name), driver_name);
            if (config_ctx == NULL)
            {
                DMOD_LOG_ERROR("Failed to read driver for config: %s\n", full_path);
                continue;
            }

            driver_node_t* driver_node = configure_driver(module_name, config_ctx);
            dmini_destroy(config_ctx);
            if (driver_node == NULL)
            {
                DMOD_LOG_ERROR("Failed to configure driver: %s\n", module_name);
                continue;
            }
            if(dmlist_push_back(ctx->drivers, driver_node) != 0)
            {
                DMOD_LOG_ERROR("Failed to add driver to list: %s\n", module_name);
                Dmod_Free(driver_node);
                continue;
            }
        }
        else 
        {
            // read driver name from directory name
            char module_name[DMOD_MAX_MODULE_NAME_LENGTH];
            read_base_name(entry, module_name, sizeof(module_name));
            if(is_driver(module_name))
            {
                driver_name = module_name;
            }
            int res = configure_drivers(ctx, driver_name, full_path);
            if (res != DMFSI_OK)
            {
                DMOD_LOG_ERROR("Failed to configure drivers in directory: %s\n", full_path);
            }
        }
    }
    Dmod_CloseDir(dir);
    return DMFSI_OK;
}

/**
 * @brief Configure a single driver based on its name and configuration file
 */
static driver_node_t* configure_driver(const char* driver_name, dmini_context_t config_ctx)
{
    DMOD_LOG_VERBOSE("Configuring driver: %s\n", driver_name);
    bool was_loaded = false;
    bool was_enabled = false;
    Dmod_Context_t* driver = prepare_driver_module(driver_name, &was_loaded, &was_enabled);
    if (driver == NULL)
    {
        return NULL;
    }

    dmod_dmdrvi_create_t dmdrvi_create = Dmod_GetDifFunction(driver, dmod_dmdrvi_create_sig);
    if (dmdrvi_create == NULL)
    {
        DMOD_LOG_ERROR("Driver module does not implement dmdrvi_create: %s\n", driver_name);
        cleanup_driver_module(driver_name, was_loaded, was_enabled);
        return NULL;
    }

    driver_node_t* driver_node = Dmod_Malloc(sizeof(driver_node_t));
    if (driver_node == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate memory for driver node: %s\n", driver_name);
        cleanup_driver_module(driver_name, was_loaded, was_enabled);
        return NULL;
    }

    driver_node->was_loaded = was_loaded;
    driver_node->was_enabled = was_enabled;
    driver_node->driver = driver;
    driver_node->driver_context = dmdrvi_create(config_ctx, &driver_node->dev_num);
    if (driver_node->driver_context == NULL)
    {
        DMOD_LOG_ERROR("Failed to create driver context: %s\n", driver_name);
        cleanup_driver_module(driver_name, was_loaded, was_enabled);
        Dmod_Free(driver_node);
        return NULL;
    }
    if(read_driver_node_path( driver_node, driver_node->path, sizeof(driver_node->path) ) != 0)
    {
        DMOD_LOG_ERROR("Failed to read driver node path: %s\n", driver_name);
        dmod_dmdrvi_free_t dmdrvi_free = Dmod_GetDifFunction(driver, dmod_dmdrvi_free_sig);
        if (dmdrvi_free != NULL)
        {
            dmdrvi_free(driver_node->driver_context);
        }
        cleanup_driver_module(driver_name, was_loaded, was_enabled);
        Dmod_Free(driver_node);
        return NULL;
    }

    DMOD_LOG_INFO("Configured driver: %s (path: %s)\n", driver_name, driver_node->path);

    return driver_node;
}

/**
 * @brief Unconfigure and unload all drivers
 */
static int unconfigure_drivers(dmfsi_context_t ctx)
{
    if (ctx == NULL || ctx->drivers == NULL)
    {
        return DMFSI_ERR_INVALID;
    }

    size_t list_size = dmlist_size(ctx->drivers);
    for (size_t i = 0; i < list_size; i++)
    {
        driver_node_t* driver_node = (driver_node_t*)dmlist_get(ctx->drivers, i);
        if (driver_node != NULL)
        {
            dmod_dmdrvi_free_t dmdrvi_free = Dmod_GetDifFunction(driver_node->driver, dmod_dmdrvi_free_sig);
            if (dmdrvi_free != NULL)
            {
                dmdrvi_free(driver_node->driver_context);
                DMOD_LOG_INFO("Freed driver context for: %s\n", Dmod_GetName(driver_node->driver));
            }
            cleanup_driver_module(Dmod_GetName(driver_node->driver), driver_node->was_loaded, driver_node->was_enabled);
            Dmod_Free(driver_node);
        }
    }

    dmlist_clear(ctx->drivers);

    DMOD_LOG_INFO("Unconfigured all drivers\n");

    return DMFSI_OK;
}

/**
 * @brief Check if a path is a file
 */
static bool is_file(const char* path)
{
    // Check if path exists
    if (Dmod_Access(path, DMOD_F_OK) != 0)
    {
        return false;
    }
    
    // Try to open as directory - if it succeeds, it's a directory, not a file
    void* dir_handle = Dmod_OpenDir(path);
    if (dir_handle != NULL)
    {
        Dmod_CloseDir(dir_handle);
        return false;  // It's a directory
    }
    
    return true;  // It's a file
}

/**
 * @brief Check if a path is a directory
 */
static bool is_driver( const char* name)
{
    char module_name[DMOD_MAX_MODULE_NAME_LENGTH] = {0};
    return Dmod_FindMatch(name, module_name, sizeof(module_name));
}

/**
 * @brief Extract base name from a path
 */
static void read_base_name(const char* path, char* base_name, size_t name_size)
{
    const char* last_slash = strrchr(path, '/');
    const char* name_start = (last_slash != NULL) ? last_slash + 1 : path;
    strncpy(base_name, name_start, name_size);
    base_name[name_size - 1] = '\0';
}

/**
 * @brief Read driver name from configuration file
 */
static dmini_context_t read_driver_for_config(const char* config_path, char* driver_name, size_t name_size, const char* default_driver)
{
    dmini_context_t ctx = dmini_create();
    if (ctx == NULL)
    {
        DMOD_LOG_ERROR("Failed to create INI context\n");
        return NULL;
    }

    int res = dmini_parse_file(ctx, config_path);
    if (res != DMINI_OK)
    {
        DMOD_LOG_ERROR("Failed to parse INI file: %s\n", config_path);
        dmini_destroy(ctx);
        return NULL;  
    }

    const char* name = dmini_get_string(ctx, "main", "driver_name", default_driver);
    if(name != NULL)
    {
        strncpy(driver_name, name, name_size);
        driver_name[name_size - 1] = '\0';
        return ctx;
    }
    
    read_base_name(config_path, driver_name, name_size);

    // cut the `.ini` extension if present
    char* ext = strrchr(driver_name, '.');
    if (ext != NULL && strcmp(ext, ".ini") == 0)
    {
        *ext = '\0';
    }
    return ctx;
}

/**
 * @brief Prepare and load a driver module
 */
static Dmod_Context_t* prepare_driver_module(const char* driver_name, bool* was_loaded, bool* was_enabled)
{
    *was_loaded = Dmod_IsModuleLoaded(driver_name);
    *was_enabled = Dmod_IsModuleEnabled(driver_name);
    Dmod_Context_t* driver = Dmod_LoadModuleByName(driver_name);
    if (driver == NULL)
    {
        DMOD_LOG_ERROR("Failed to load driver module: %s\n", driver_name);
        return NULL;
    }
    if (!*was_enabled && !Dmod_EnableModule(driver_name, true, NULL))
    {
        DMOD_LOG_ERROR("Failed to enable driver module: %s\n", driver_name);
        if(!*was_loaded)
        {
            Dmod_UnloadModule(driver_name, false);
        }
        return NULL;
    }

    DMOD_LOG_INFO("Prepared driver module: %s (was_loaded: %d, was_enabled: %d)\n", driver_name, *was_loaded, *was_enabled);
    return driver;
}

/**
 * @brief Cleanup and unload a driver module
 */
static void cleanup_driver_module(const char* driver_name, bool was_loaded, bool was_enabled)
{
    if(!was_enabled)
    {
        Dmod_DisableModule(driver_name, false);
    }
    if(!was_loaded)
    {
        Dmod_UnloadModule(driver_name, false);
    }
}

/**
 * @brief Read the path associated with a driver directory
 */
static int read_driver_parent_directory( const driver_node_t* node, char* path_buffer, size_t buffer_size )
{
    if (node == NULL || path_buffer == NULL || buffer_size == 0)
    {
        return DMFSI_ERR_INVALID;
    }

    memset(path_buffer, 0, buffer_size);
    const char* driver_name = Dmod_GetName( node->driver );
    if(driver_name == NULL)
    {
        return DMFSI_ERR_NOT_FOUND;
    }
    bool major_given = (node->dev_num.flags & DMDRVI_NUM_MAJOR) != 0;
    bool minor_given = (node->dev_num.flags & DMDRVI_NUM_MINOR) != 0;
    if(major_given && minor_given)
    {
        Dmod_SnPrintf(path_buffer, buffer_size, "%s%u/", driver_name, node->dev_num.major);
    }
    else if(minor_given)
    {
        Dmod_SnPrintf(path_buffer, buffer_size, "%sx/", driver_name);
    }
    else 
    {
        strncpy(path_buffer, ROOT_DIRECTORY_NAME, buffer_size);
    }
    return DMFSI_OK;
}

/**
 * @brief Read the path associated with a driver node
 */
static int read_driver_node_path( const driver_node_t* node, char* path_buffer, size_t buffer_size )
{
    if (node == NULL || path_buffer == NULL || buffer_size == 0)
    {
        return DMFSI_ERR_INVALID;
    }
    memset(path_buffer, 0, buffer_size);    

    if(read_driver_parent_directory( node, path_buffer, buffer_size ) != DMFSI_OK)
    {
        return DMFSI_ERR_GENERAL;
    }
    bool major_given = (node->dev_num.flags & DMDRVI_NUM_MAJOR) != 0;
    bool minor_given = (node->dev_num.flags & DMDRVI_NUM_MINOR) != 0;
    size_t current_length = strlen(path_buffer);
    if(current_length >= buffer_size)
    {
        DMOD_LOG_ERROR("Buffer too small for driver path\n");
        return DMFSI_ERR_NO_SPACE;
    }
    path_buffer += current_length;
    buffer_size -= current_length;
    if(minor_given)
    {
        Dmod_SnPrintf(path_buffer, buffer_size, "%u", node->dev_num.minor);
    }
    else 
    {
        const char* driver_name = Dmod_GetName( node->driver );
        if(driver_name == NULL)
        {
            return DMFSI_ERR_NOT_FOUND;
        }
        if(major_given)
        {
            Dmod_SnPrintf(path_buffer, buffer_size, "%s%u", driver_name, node->dev_num.major);
        }
        else 
        {
            strncpy(path_buffer, driver_name, buffer_size);
        }
    }
    return DMFSI_OK;
}

/**
 * @brief Compare the path of a driver directory with a given path
 */
static int compare_driver_directory( const void* data, const void* user_data )
{
    const driver_node_t* node = (const driver_node_t*)data;
    const char* path = (const char*)user_data;
    if (node == NULL || path == NULL)
    {
        return 0;
    }

    char directory_path[MAX_PATH_LENGTH] = {0};
    if(read_driver_parent_directory(node, directory_path, sizeof(directory_path)) != 0)
    {
        return -1;
    }

    char* driver_path = directory_path;
    while(*path)
    {
        if(*path != *driver_path)
        {
            return 1;
        }
        path++;
        driver_path++;
    }
    return 0;
}

/**
 * @brief Compare the path of a driver node with a given path
 */
static int compare_driver_node_path( const void* data, const void* user_data )
{
    const driver_node_t* node = (const driver_node_t*)data;
    const char* path = (const char*)user_data;
    if (node == NULL || path == NULL)
    {
        return 0;
    }

    return strcmp(node->path, path);
}

/**
 * @brief Compare a driver node with a given driver node
 */
static int compare_driver(const void* data, const void* user_data )
{
    const driver_node_t* node = (const driver_node_t*)data;
    const driver_node_t* target = (const driver_node_t*)user_data;
    if (node == NULL || target == NULL)
    {
        return -1;
    }

    return (node == target) ? 0 : -1;
}

/**
 * @brief Check if a path is a directory
 */
static bool is_directory( dmfsi_context_t ctx, const char* path )
{
    return strcmp(path, ROOT_DIRECTORY_NAME) == 0 || dmlist_find(ctx->drivers, path, compare_driver_directory) != NULL;
}

/**
 * @brief Get the next driver node in a directory
 */
static driver_node_t* get_next_driver_node( dmfsi_context_t ctx, driver_node_t* current, const char* path )
{
    return dmlist_find_next(ctx->drivers, current, path, compare_driver_directory);
}

/**
 * @brief Find a driver node by its path
 */
static driver_node_t* find_driver_node( dmfsi_context_t ctx, const char* path )
{
    return dmlist_find(ctx->drivers, path, compare_driver_node_path);
}

/**
 * @brief Get file statistics from a driver
 */
static int driver_stat( driver_node_t* context, const char* path, dmdrvi_stat_t* stat )
{
    if (context == NULL || stat == NULL)
    {
        return DMFSI_ERR_INVALID;
    }

    dmod_dmdrvi_stat_t dmdrvi_stat = Dmod_GetDifFunction(context->driver, dmod_dmdrvi_stat_sig);
    if (dmdrvi_stat == NULL)
    {
        DMOD_LOG_ERROR("Driver module does not implement dmdrvi_stat\n");
        return DMFSI_ERR_NOT_FOUND;
    }

    return dmdrvi_stat(context->driver_context, path, stat);
}