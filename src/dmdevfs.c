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
#include <stdio.h>
#include <stdbool.h>

/** 
 * @brief Magic number for DMDEVFS context validation
 */
#define DMDEVFS_CONTEXT_MAGIC 0x444D4456  // 'DMDV'

typedef struct 
{
    dmdrvi_context_t driver_context;    // Driver-specific context
    Dmod_Context_t*  driver;            // Driver module context
    dmdrvi_dev_num_t dev_num;           // Device number assigned to the driver
    bool was_loaded;                    // Indicates if the driver was loaded by dmdevfs
    bool was_enabled;                   // Indicates if the driver was enabled by dmdevfs
    char driver_name[DMOD_MAX_MODULE_NAME_LENGTH];  // Driver name for device file naming
} driver_node_t;

/**
 * @brief Directory handle for directory operations
 */
typedef struct
{
    size_t current_index;    // Current position in the driver list
    bool is_open;            // Whether the directory is open
} dir_handle_t;

/**
 * @brief File system context structure
 */
struct dmfsi_context
{
    uint32_t    magic;
    const char* config_path;     // Path with the configuration files
    dmlist_context_t* drivers;      // List of loaded drivers
};


// ============================================================================
//                      Local prototypes
// ============================================================================
static int configure_drivers(dmfsi_context_t ctx, const char* driver_name, const char* config_path);
static driver_node_t* configure_driver(const char* driver_name, dmini_context_t config_ctx);
static int unconfigure_drivers(dmfsi_context_t ctx);
static bool is_file(const char* path);
static void read_base_name(const char* path, char* base_name, size_t name_size);
static dmini_context_t read_driver_for_config(const char* config_path, char* driver_name, size_t name_size, const char* default_driver);
static Dmod_Context_t* prepare_driver_module(const char* driver_name, bool* was_loaded, bool* was_enabled);
static void cleanup_driver_module(const char* driver_name, bool was_loaded, bool was_enabled);
static void build_device_filename(const driver_node_t* driver_node, char* filename, size_t size);
static bool is_root_path(const char* path);

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
        DMOD_LOG_ERROR("dmdevfs: Config path is NULL\n");
        return NULL;
    }

    if(strlen(config) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Config path is empty\n");
        return NULL;
    }

    dmfsi_context_t ctx = Dmod_Malloc(sizeof(struct dmfsi_context));
    if (ctx == NULL)
    {
        DMOD_LOG_ERROR("dmdevfs: Failed to allocate memory for context\n");
        return NULL;
    }
    
    ctx->magic = DMDEVFS_CONTEXT_MAGIC;
    ctx->config_path = Dmod_StrDup(config);
    ctx->drivers = dmlist_create(DMOD_MODULE_NAME);
    
    int res = configure_drivers(ctx, NULL, ctx->config_path);
    if (res != DMFSI_OK)
    {
        DMOD_LOG_ERROR("dmdevfs: Failed to configure drivers\n");
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
        DMOD_LOG_ERROR("dmdevfs: Invalid context in deinit\n");
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
        DMOD_LOG_ERROR("dmdevfs: Invalid context in fopen\n");
        return DMFSI_ERR_INVALID;
    }
    
    // TODO: Implement file opening through driver
    DMOD_LOG_ERROR("dmdevfs: fopen not yet implemented\n");
    return DMFSI_ERR_GENERAL;
}

/**
 * @brief Close a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _fclose, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in fclose\n");
        return DMFSI_ERR_INVALID;
    }
    
    // TODO: Implement file closing
    DMOD_LOG_ERROR("dmdevfs: fclose not yet implemented\n");
    return DMFSI_ERR_GENERAL;
}

/**
 * @brief Read from a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _fread, (dmfsi_context_t ctx, void* fp, void* buffer, size_t size, size_t* read) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in fread\n");
        return DMFSI_ERR_INVALID;
    }
    
    // TODO: Implement file reading
    if (read) *read = 0;
    return DMFSI_ERR_GENERAL;
}

/**
 * @brief Write to a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _fwrite, (dmfsi_context_t ctx, void* fp, const void* buffer, size_t size, size_t* written) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in fwrite\n");
        return DMFSI_ERR_INVALID;
    }
    
    // TODO: Implement file writing
    if (written) *written = 0;
    return DMFSI_ERR_GENERAL;
}

/**
 * @brief Seek to a position in a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _lseek, (dmfsi_context_t ctx, void* fp, long offset, int whence) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in lseek\n");
        return DMFSI_ERR_INVALID;
    }
    
    // TODO: Implement file seeking
    return DMFSI_ERR_GENERAL;
}

/**
 * @brief Get current position in a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, long, _tell, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in tell\n");
        return -1;
    }
    
    // TODO: Implement position retrieval
    return -1;
}

/**
 * @brief Check if at end of file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _eof, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in eof\n");
        return 1;
    }
    
    // TODO: Implement EOF checking
    return 1;
}

/**
 * @brief Get file size
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, long, _size, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in size\n");
        return -1;
    }
    
    // TODO: Implement size retrieval
    return -1;
}

/**
 * @brief Read a single character
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _getc, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in getc\n");
        return -1;
    }
    
    // TODO: Implement character reading
    return -1;
}

/**
 * @brief Write a single character
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _putc, (dmfsi_context_t ctx, void* fp, char c) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in putc\n");
        return -1;
    }
    
    // TODO: Implement character writing
    return DMFSI_ERR_GENERAL;
}

/**
 * @brief Flush file buffers
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _fflush, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in fflush\n");
        return DMFSI_ERR_INVALID;
    }
    
    // TODO: Implement buffer flushing
    return DMFSI_OK;
}

/**
 * @brief Sync file to storage
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _sync, (dmfsi_context_t ctx, void* fp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in sync\n");
        return DMFSI_ERR_INVALID;
    }
    
    // TODO: Implement sync
    return DMFSI_OK;
}

/**
 * @brief Open a directory
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _opendir, (dmfsi_context_t ctx, void** dp, const char* path) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in opendir\n");
        return DMFSI_ERR_INVALID;
    }
    
    // Only support opening root directory
    if (is_root_path(path))
    {
        dir_handle_t* dir = Dmod_Malloc(sizeof(dir_handle_t));
        if (dir == NULL)
        {
            DMOD_LOG_ERROR("dmdevfs: Failed to allocate directory handle\n");
            return DMFSI_ERR_GENERAL;
        }
        
        dir->current_index = 0;
        dir->is_open = true;
        *dp = dir;
        return DMFSI_OK;
    }
    
    return DMFSI_ERR_NOT_FOUND;
}

/**
 * @brief Read directory entry
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _readdir, (dmfsi_context_t ctx, void* dp, dmfsi_dir_entry_t* entry) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in readdir\n");
        return DMFSI_ERR_INVALID;
    }
    
    if (dp == NULL || entry == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    
    dir_handle_t* dir = (dir_handle_t*)dp;
    if (!dir->is_open)
    {
        return DMFSI_ERR_INVALID;
    }
    
    size_t driver_count = dmlist_size(ctx->drivers);
    if (dir->current_index >= driver_count)
    {
        return DMFSI_ERR_NOT_FOUND;  // No more entries
    }
    
    driver_node_t* driver_node = (driver_node_t*)dmlist_get(ctx->drivers, dir->current_index);
    if (driver_node == NULL)
    {
        dir->current_index++;
        return DMFSI_ERR_GENERAL;
    }
    
    // Build device filename based on dev_num flags
    build_device_filename(driver_node, entry->name, sizeof(entry->name));
    entry->is_directory = false;
    entry->size = 0;
    
    dir->current_index++;
    return DMFSI_OK;
}

/**
 * @brief Close a directory
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _closedir, (dmfsi_context_t ctx, void* dp) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in closedir\n");
        return DMFSI_ERR_INVALID;
    }
    
    if (dp != NULL)
    {
        dir_handle_t* dir = (dir_handle_t*)dp;
        dir->is_open = false;
        Dmod_Free(dp);
        return DMFSI_OK;
    }
    
    return DMFSI_ERR_GENERAL;
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
        DMOD_LOG_ERROR("dmdevfs: Invalid context in direxists\n");
        return 0;
    }
    
    // Only root directory exists
    return is_root_path(path) ? 1 : 0;
}

/**
 * @brief Get file/directory statistics
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _stat, (dmfsi_context_t ctx, const char* path, dmfsi_stat_t* stat) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in stat\n");
        return DMFSI_ERR_INVALID;
    }
    
    // TODO: Implement stat
    return DMFSI_ERR_GENERAL;
}

/**
 * @brief Delete a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _unlink, (dmfsi_context_t ctx, const char* path) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in unlink\n");
        return DMFSI_ERR_INVALID;
    }
    
    // TODO: Implement file deletion
    return DMFSI_ERR_GENERAL;
}

/**
 * @brief Rename a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _rename, (dmfsi_context_t ctx, const char* oldpath, const char* newpath) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("dmdevfs: Invalid context in rename\n");
        return DMFSI_ERR_INVALID;
    }
    
    // TODO: Implement file renaming
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
        DMOD_LOG_ERROR("dmdevfs: Failed to open config directory: %s\n", ctx->config_path);
        return DMFSI_ERR_NOT_FOUND;
    }

    const char* entry;
    while ((entry = Dmod_ReadDir(dir)) != NULL)
    {
        if (is_file(entry))
        {
            char module_name[DMOD_MAX_MODULE_NAME_LENGTH];
            dmini_context_t config_ctx = read_driver_for_config(entry, module_name, sizeof(module_name), driver_name);
            if (config_ctx == NULL)
            {
                DMOD_LOG_ERROR("dmdevfs: Failed to read driver for config: %s\n", entry);
                continue;
            }

            driver_node_t* driver_node = configure_driver(module_name, config_ctx);
            dmini_destroy(config_ctx);
            if (driver_node == NULL)
            {
                DMOD_LOG_ERROR("dmdevfs: Failed to configure driver: %s\n", module_name);
                continue;
            }
            if(dmlist_push_back(ctx->drivers, driver_node) != 0)
            {
                DMOD_LOG_ERROR("dmdevfs: Failed to add driver to list: %s\n", module_name);
                Dmod_Free(driver_node);
                continue;
            }
        }
        else 
        {
            // read driver name from directory name
            char module_name[DMOD_MAX_MODULE_NAME_LENGTH];
            read_base_name(entry, module_name, sizeof(module_name));
            int res = configure_drivers(ctx, driver_name, entry);
            if (res != DMFSI_OK)
            {
                DMOD_LOG_ERROR("dmdevfs: Failed to configure drivers in directory: %s\n", entry);
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
    strncpy(driver_node->driver_name, driver_name, DMOD_MAX_MODULE_NAME_LENGTH - 1);
    driver_node->driver_name[DMOD_MAX_MODULE_NAME_LENGTH - 1] = '\0';
    
    driver_node->driver_context = dmdrvi_create(config_ctx, &driver_node->dev_num);
    if (driver_node->driver_context == NULL)
    {
        DMOD_LOG_ERROR("Failed to create driver context: %s\n", driver_name);
        cleanup_driver_module(driver_name, was_loaded, was_enabled);
        Dmod_Free(driver_node);
        return NULL;
    }

    // Log device file information based on dev_num flags
    if (driver_node->dev_num.flags == DMDRVI_NUM_NONE)
    {
        Dmod_Printf("dmdevfs: Device created: %s\n", driver_name);
    }
    else if (driver_node->dev_num.flags & DMDRVI_NUM_MINOR)
    {
        Dmod_Printf("dmdevfs: Device created: %s%d/%d (major/minor)\n", 
                    driver_name, driver_node->dev_num.major, driver_node->dev_num.minor);
    }
    else if (driver_node->dev_num.flags & DMDRVI_NUM_MAJOR)
    {
        Dmod_Printf("dmdevfs: Device created: %s%d (major)\n", 
                    driver_name, driver_node->dev_num.major);
    }

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
            }
            cleanup_driver_module(Dmod_GetName(driver_node->driver), driver_node->was_loaded, driver_node->was_enabled);
            Dmod_Free(driver_node);
        }
    }

    dmlist_clear(ctx->drivers);

    return DMFSI_OK;
}

/**
 * @brief Check if a path is a file
 */
static bool is_file(const char* path)
{
    return Dmod_Access(path, DMOD_F_OK) == 0;
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
        DMOD_LOG_ERROR("dmdevfs: Failed to create INI context\n");
        return NULL;
    }

    int res = dmini_parse_file(ctx, config_path);
    if (res != DMINI_OK)
    {
        DMOD_LOG_ERROR("dmdevfs: Failed to parse INI file: %s\n", config_path);
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
 * @brief Build device filename based on dev_num flags
 * 
 * Note: For DMDRVI_NUM_MINOR, this returns a path with forward slash (e.g., "dmspi0/0").
 * This matches the dmdrvi interface specification. Full directory hierarchy support
 * would require implementing nested directory operations, which is left for future
 * enhancement when file I/O operations are implemented.
 * 
 * @param driver_node Pointer to driver node (must not be NULL)
 * @param filename Output buffer for filename (must not be NULL)
 * @param size Size of output buffer (must be > 0)
 */
static void build_device_filename(const driver_node_t* driver_node, char* filename, size_t size)
{
    // Validate input parameters
    if (driver_node == NULL || filename == NULL || size == 0)
    {
        return;
    }
    
    if (driver_node->dev_num.flags == DMDRVI_NUM_NONE)
    {
        // Device file: /dev/dmclk -> dmclk (no /dev prefix needed)
        snprintf(filename, size, "%s", driver_node->driver_name);
    }
    else if (driver_node->dev_num.flags & DMDRVI_NUM_MINOR)
    {
        // Device file: /dev/dmspi0/0 -> dmspi0/0
        // This represents a directory structure with major and minor numbers
        // TODO: Implement proper nested directory support in opendir/readdir
        snprintf(filename, size, "%s%d/%d", 
                 driver_node->driver_name, 
                 driver_node->dev_num.major, 
                 driver_node->dev_num.minor);
    }
    else if (driver_node->dev_num.flags & DMDRVI_NUM_MAJOR)
    {
        // Device file: /dev/dmuart0 -> dmuart0
        snprintf(filename, size, "%s%d", 
                 driver_node->driver_name, 
                 driver_node->dev_num.major);
    }
    else
    {
        // Fallback to driver name only
        snprintf(filename, size, "%s", driver_node->driver_name);
    }
}

/**
 * @brief Check if path represents the root directory
 * 
 * @param path Path to check (can be NULL)
 * @return true if path is NULL, empty, or "/"
 */
static bool is_root_path(const char* path)
{
    if (path == NULL)
    {
        return true;
    }
    
    if (path[0] == '\0' || strcmp(path, "/") == 0)
    {
        return true;
    }
    
    return false;
}
}