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
#include "dmosi.h"
#include <string.h>

/**
 * @brief Magic number for DMDEVFS context validation
 */
#define DMDEVFS_CONTEXT_MAGIC 0x444D4456  // 'DMDV'
#define ROOT_DIRECTORY_NAME "/"
#define MAX_PATH_LENGTH     (DMOD_MAX_MODULE_NAME_LENGTH + 20)
#define INI_MAIN_SECTION "main"

/**
 * @brief Hot-plug worker thread configuration
 *
 * MAL notifications (dmdrvi_device_available()/_unavailable()) only enqueue an
 * event and return - the actual work (searching/mutating the driver list) is
 * done on this dedicated thread, so a driver announcing a hot-plug event never
 * blocks on dmdevfs-internal bookkeeping.
 */
#define DMDEVFS_HOTPLUG_QUEUE_LENGTH        8
#define DMDEVFS_HOTPLUG_THREAD_PRIORITY     1
#define DMDEVFS_HOTPLUG_THREAD_STACK_SIZE   (512 + DMOSI_THREAD_STACK_OVERHEAD)
#define DMDEVFS_HOTPLUG_THREAD_NAME         "dmdevfs_hotplug"

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
    bool is_dynamic;                    // True for devices announced via dmdrvi_device_available() (hot-plug);
                                         // such nodes share driver_context/driver with their owning node and must
                                         // never be passed to dmdrvi_free()/cleanup_driver_module() themselves.
    path_t path;                        // Path associated with the driver
} driver_node_t;

typedef struct
{
    driver_node_t* driver;   // Last driver
    char* directory_path;   // Directory path
} directory_node_t;

/**
 * @brief Lookup key used to find a dynamic (hot-plugged) driver_node_t by
 *        the (driver context, device number) pair reported through the
 *        dmdrvi hot-plug MAL callbacks.
 */
typedef struct
{
    dmdrvi_context_t context;
    const dmdrvi_dev_num_t* dev_num;
} dynamic_device_lookup_t;

/**
 * @brief Hot-plug event queued from dmdrvi_device_available()/_unavailable()
 *        for the hot-plug worker thread to process.
 *
 * context == NULL is reserved as the shutdown sentinel (see dmod_deinit()).
 */
typedef struct
{
    dmdrvi_context_t context;
    dmdrvi_dev_num_t dev_num;
    bool available;   // true: device became available, false: no longer available
} hotplug_event_t;

/**
 * @brief A driver configuration discovered while walking the config tree, waiting
 *        to be applied. Configuration is deferred so that `driver_order` and
 *        inter-module dependencies (as reported by dmod) can be resolved across
 *        the whole config tree before any driver is actually created.
 */
typedef struct
{
    char module_name[DMOD_MAX_MODULE_NAME_LENGTH];   // Driver module to configure
    char section_name[DMOD_MAX_MODULE_NAME_LENGTH];  // Section within config_ctx (only if has_section)
    bool has_section;                                // Whether section_name is a real INI section
    dmini_context_t config_ctx;                       // INI context (owned by the config file, not by this entry)
    int driver_order;                                 // Explicit ordering group (default 0)
    bool configured;                                  // Already turned into a driver_node_t
    bool configuring;                                 // Cycle guard while resolving dependencies
} pending_driver_t;

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
    uint32_t offset;            // Current byte offset within the device
} file_handle_t;

/**
 * @brief File system context structure
 */
struct dmfsi_context
{
    uint32_t    magic;
    char* config_path;          // Path with the configuration files
    dmlist_context_t* drivers;  // List of loaded drivers
    bool ready;                 // True once dmfsi_dmdevfs_mounted() has been called by the mounter
    path_t mount_path;          // This mount's own absolute path, valid once `ready` is true
};

/**
 * @brief Registry of all live dmdevfs mounts (struct dmfsi_context*)
 *
 * dmdrvi_device_available()/dmdrvi_device_unavailable() are MAL callbacks
 * invoked by a driver with only its dmdrvi_context_t - not the dmfsi_context_t
 * of the dmdevfs mount that created it. This registry lets those callbacks find
 * the right mount (and driver_node_t) to update.
 */
static dmlist_context_t* g_dmdevfs_mounts = NULL;

/**
 * @brief Guards g_dmdevfs_mounts and every mount's ctx->drivers list against
 *        concurrent access between the hot-plug worker thread and callers of
 *        the dmfsi DIF functions (potentially arriving through dmvfs, which
 *        holds its own mutex around the dispatch).
 *
 * Kept to the smallest possible critical sections and NEVER held across a
 * DMOD_LOG_* call or a call into driver code: logging can end up writing to a
 * file through dmvfs, which would need dmvfs's mutex while we hold ours - and
 * a dmvfs-side caller blocked on this very mutex (already holding dmvfs's)
 * would deadlock against it.
 */
static dmosi_mutex_t g_devfs_mutex = NULL;

/**
 * @brief Queue and worker thread that process hot-plug events out of MAL
 *        callback context (see dmdrvi_device_available()/_unavailable())
 */
static dmosi_queue_t   g_hotplug_queue = NULL;
static dmosi_process_t g_hotplug_process = NULL;
static dmosi_thread_t  g_hotplug_thread = NULL;


// ============================================================================
//                      Local prototypes
// ============================================================================
static int configure_drivers(dmfsi_context_t ctx, const char* driver_name, const char* config_path);
static int collect_driver_configs(dmlist_context_t* pending, dmlist_context_t* config_files, const char* driver_name, const char* config_path);
static int collect_section_driver_configs(dmlist_context_t* pending, dmini_context_t config_ctx);
static bool add_pending_driver(dmlist_context_t* pending, const char* module_name, dmini_context_t config_ctx, const char* section_name, int driver_order);
static int configure_pending_drivers(dmfsi_context_t ctx, dmlist_context_t* pending);
static bool configure_pending_entry(dmfsi_context_t ctx, dmlist_context_t* pending, pending_driver_t* entry);
static bool configure_required_dependencies(dmfsi_context_t ctx, dmlist_context_t* pending, pending_driver_t* entry);
static driver_node_t* configure_driver(const char* driver_name, dmini_context_t config_ctx, const char* section_name);
static int unconfigure_drivers(dmfsi_context_t ctx);
static bool is_file(const char* path);
static bool is_driver( const char* name);
static void read_base_name(const char* path, char* base_name, size_t name_size);
static void read_dir_name_from_path(const char* path, char* dir_name, size_t name_size);
static void read_next_subdir_name(const char* base_path, const char* full_path, char* dir_name, size_t name_size);
static dmini_context_t read_driver_for_config(const char* config_path, char* driver_name, size_t name_size, const char* default_driver);
static Dmod_Context_t* prepare_driver_module(const char* driver_name, bool* was_loaded, bool* was_enabled);
static void cleanup_driver_module(const char* driver_name, bool was_loaded, bool was_enabled);
static int read_driver_parent_directory( const driver_node_t* node, char* path_buffer, size_t buffer_size );
static int read_driver_node_path( const driver_node_t* node, char* path_buffer, size_t buffer_size );
static int compare_paths_ignore_trailing_slash( const char* path1, const char* path2 );
static int compare_driver_directory( const void* data, const void* user_data );
static int compare_driver_node_path( const void* data, const void* user_data );
static int compare_driver(const void* data, const void* user_data );
static bool is_directory( dmfsi_context_t ctx, const char* path );
static driver_node_t* get_next_driver_node( dmfsi_context_t ctx, driver_node_t* current, const char* dir_path );

static driver_node_t* find_driver_node( dmfsi_context_t ctx, const char* path );
static int driver_stat( driver_node_t* context, const char* path, dmdrvi_stat_t* stat );
static bool dev_num_equal( const dmdrvi_dev_num_t* a, const dmdrvi_dev_num_t* b );
static int compare_driver_context( const void* data, const void* user_data );
static int compare_dynamic_device( const void* data, const void* user_data );
static int compare_mount_context( const void* data, const void* user_data );
static int compare_mount_owns_context( const void* data, const void* user_data );
static int compare_mount_owns_dynamic_device( const void* data, const void* user_data );
static int build_absolute_path( const char* mount_path, const char* node_path, char* out, size_t out_size );
static void notify_driver_path_ready( const char* mount_path, driver_node_t* node );
static bool notify_all_drivers_path_ready( void* data, void* user_data );
static void hotplug_worker_thread( void* arg );
static void process_device_available( dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num );
static void process_device_unavailable( dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num );

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
    g_dmdevfs_mounts = dmlist_create(DMOD_MODULE_NAME);
    g_devfs_mutex = dmosi_mutex_create(false);
    g_hotplug_queue = dmosi_queue_create(sizeof(hotplug_event_t), DMDEVFS_HOTPLUG_QUEUE_LENGTH);
    if (g_dmdevfs_mounts == NULL || g_devfs_mutex == NULL || g_hotplug_queue == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate hot-plug support resources\n");
        return -1;
    }

    // Own process (rather than NULL/"current process") so the worker thread is
    // grouped under an identity that actually represents it - not whatever
    // process happened to be current on the thread that loaded this module.
    g_hotplug_process = dmosi_process_create(DMDEVFS_HOTPLUG_THREAD_NAME, DMOD_MODULE_NAME, NULL);
    if (g_hotplug_process == NULL)
    {
        DMOD_LOG_ERROR("Failed to create hot-plug worker process\n");
        return -1;
    }

    g_hotplug_thread = dmosi_thread_create(hotplug_worker_thread, NULL,
        DMDEVFS_HOTPLUG_THREAD_PRIORITY, DMDEVFS_HOTPLUG_THREAD_STACK_SIZE,
        DMDEVFS_HOTPLUG_THREAD_NAME, g_hotplug_process);
    if (g_hotplug_thread == NULL)
    {
        DMOD_LOG_ERROR("Failed to start hot-plug worker thread\n");
        return -1;
    }

    return 0;
}

/**
 * @brief Module deinitialization
 */
int dmod_deinit(void)
{
    if (g_hotplug_thread != NULL)
    {
        // Wake the worker with a shutdown sentinel (context == NULL) and wait
        // for it to exit before tearing down the resources it uses.
        hotplug_event_t stop_event = { .context = NULL };
        dmosi_queue_send(g_hotplug_queue, &stop_event, -1);
        dmosi_thread_join(g_hotplug_thread);
        dmosi_thread_destroy(g_hotplug_thread);
        g_hotplug_thread = NULL;
    }

    if (g_hotplug_process != NULL)
    {
        dmosi_process_destroy(g_hotplug_process);
        g_hotplug_process = NULL;
    }

    dmosi_queue_destroy(g_hotplug_queue);
    g_hotplug_queue = NULL;
    dmosi_mutex_destroy(g_devfs_mutex);
    g_devfs_mutex = NULL;
    dmlist_destroy(g_dmdevfs_mounts);
    g_dmdevfs_mounts = NULL;
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
    ctx->ready = false;
    ctx->mount_path[0] = '\0';

    // Registered before configuring any driver (rather than after, as
    // before) so that dmdrvi_device_available()/_unavailable() can find this
    // mount even while a driver's own dmdrvi_create() is still running as
    // part of configure_drivers() below.
    dmosi_mutex_lock(g_devfs_mutex);
    bool registered = dmlist_push_back(g_dmdevfs_mounts, ctx);
    dmosi_mutex_unlock(g_devfs_mutex);
    if (!registered)
    {
        DMOD_LOG_ERROR("Failed to register mount for hot-plug notifications\n");
    }

    int res = configure_drivers(ctx, NULL, ctx->config_path);
    if (res != DMFSI_OK)
    {
        DMOD_LOG_ERROR("Failed to configure drivers\n");
        unconfigure_drivers(ctx);

        if (registered)
        {
            dmosi_mutex_lock(g_devfs_mutex);
            dmlist_remove(g_dmdevfs_mounts, ctx, compare_mount_context);
            dmosi_mutex_unlock(g_devfs_mutex);
        }

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

    // Deregister before tearing down ctx->drivers: once removed, the hot-plug
    // worker thread can no longer find (or touch) this mount, so the rest of
    // teardown below can safely run without holding g_devfs_mutex.
    dmosi_mutex_lock(g_devfs_mutex);
    dmlist_remove(g_dmdevfs_mounts, ctx, compare_mount_context);
    dmosi_mutex_unlock(g_devfs_mutex);

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
    
    // Open the device through the driver, identifying which device within the
    // context this is (the context's own device, or a hot-plugged sub-device)
    handle->driver_handle = dmdrvi_open(driver_node->driver_context, mode, &driver_node->dev_num);
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
    handle->offset = 0;
    
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
    size_t bytes_read = dmdrvi_read(handle->driver->driver_context, handle->driver_handle, buffer, size, handle->offset);
    if(read) *read = bytes_read;
    handle->offset += (uint32_t)bytes_read;
    
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
    size_t bytes_written = dmdrvi_write(handle->driver->driver_context, handle->driver_handle, buffer, size, handle->offset);
    if(written) *written = bytes_written;
    handle->offset += (uint32_t)bytes_written;
    
    return DMFSI_OK;
}

/**
 * @brief Seek to a position in a file
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, long, _lseek, (dmfsi_context_t ctx, void* fp, long offset, int whence) )
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
    
    file_handle_t* handle = (file_handle_t*)fp;
    long new_offset;

    if(whence == DMFSI_SEEK_SET)
    {
        new_offset = offset;
    }
    else if(whence == DMFSI_SEEK_CUR)
    {
        new_offset = (long)handle->offset + offset;
    }
    else if(whence == DMFSI_SEEK_END)
    {
        dmdrvi_stat_t stat = {0};
        int result = driver_stat(handle->driver, handle->path, &stat);
        if(result != 0)
        {
            DMOD_LOG_ERROR("lseek SEEK_END: failed to get file size\n");
            return DMFSI_ERR_GENERAL;
        }
        new_offset = (long)stat.size + offset;
    }
    else
    {
        DMOD_LOG_ERROR("lseek: invalid whence value %d\n", whence);
        return DMFSI_ERR_INVALID;
    }

    if(new_offset < 0)
    {
        DMOD_LOG_ERROR("lseek: resulting offset is negative\n");
        return DMFSI_ERR_INVALID;
    }

    handle->offset = (uint32_t)new_offset;
    return new_offset;
}

/**
 * @brief Perform I/O control operation
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, int, _ioctl, (dmfsi_context_t ctx, void* fp, int request, void* arg) )
{
    if(dmfsi_dmdevfs_context_is_valid(ctx) == 0)
    {
        DMOD_LOG_ERROR("Invalid context in ioctl\n");
        return DMFSI_ERR_INVALID;
    }

    if(fp == NULL)
    {
        return DMFSI_ERR_INVALID;
    }

    file_handle_t* handle = (file_handle_t*)fp;

    // Get the dmdrvi_ioctl function
    dmod_dmdrvi_ioctl_t dmdrvi_ioctl = Dmod_GetDifFunction(handle->driver->driver, dmod_dmdrvi_ioctl_sig);
    if(dmdrvi_ioctl == NULL)
    {
        DMOD_LOG_ERROR("Driver does not implement dmdrvi_ioctl\n");
        return DMFSI_ERR_NOT_FOUND;
    }

    int result = dmdrvi_ioctl(handle->driver->driver_context, handle->driver_handle, request, arg);
    if(result != 0)
    {
        return DMFSI_ERR_GENERAL;
    }

    return DMFSI_OK;
}

/**
 * @brief Get current position in a file
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
    
    file_handle_t* handle = (file_handle_t*)fp;
    return (long)handle->offset;
}

/**
 * @brief Check if at end of file
 * @note EOF is determined by comparing the current offset against the file size
 *       reported by the driver. If file size is unavailable, 0 (not at EOF) is
 *       returned as EOF cannot be determined.
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
    
    file_handle_t* handle = (file_handle_t*)fp;
    
    // Compare current offset against file size reported by driver
    dmdrvi_stat_t stat = {0};
    int result = driver_stat(handle->driver, handle->path, &stat);
    if(result == 0)
    {
        return (handle->offset >= stat.size) ? 1 : 0;
    }
    
    // Size not available - cannot determine EOF
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
        // Check if the path is a file (device node)
        driver_node_t* driver_node = find_driver_node(ctx, path);
        if (driver_node != NULL)
        {
            // Path exists but is a file, not a directory
            return DMFSI_ERR_NOT_FOUND;
        }
        // Path doesn't exist at all
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

    bool file_should_be_listed = compare_paths_ignore_trailing_slash(dir_node->directory_path, parent_dir) == 0;
    if(file_should_be_listed)
    {
        // Extract basename from the full path for the directory entry
        read_base_name(driver->path, entry->name, sizeof(entry->name));
        
        dmdrvi_stat_t stat = {0};
        int res = driver_stat(driver, driver->path, &stat);
        if (res != 0)
        {
            DMOD_LOG_ERROR("Failed to get file stats for: %s\n", driver->path);
            return DMFSI_ERR_GENERAL;
        }

        entry->size = stat.size;
        entry->attr = stat.mode & ~DMFSI_ATTR_DIRECTORY;
    }
    else 
    {
        // Extract the immediate subdirectory name relative to the listing directory.
        // E.g. listing "/" with a driver whose parent is "dmgpio8/" yields "dmgpio8".
        read_next_subdir_name(dir_node->directory_path, parent_dir, entry->name, sizeof(entry->name));
        entry->size = 0;
        entry->attr = DMFSI_ATTR_DIRECTORY;
    }

    // Move to next driver for subsequent call.
    // When this entry is a directory, skip subsequent drivers that would emit
    // the same immediate subdirectory name to avoid duplicate directory entries.
    driver_node_t* next_driver = get_next_driver_node(ctx, driver, dir_node->directory_path);
    if (!file_should_be_listed)
    {
        while (next_driver != NULL)
        {
            path_t next_parent_dir;
            if (read_driver_parent_directory(next_driver, next_parent_dir, sizeof(next_parent_dir)) != 0)
            {
                DMOD_LOG_ERROR("Failed to read parent directory for next driver\n");
                return DMFSI_ERR_GENERAL;
            }

            bool next_is_file_in_listed_dir = compare_paths_ignore_trailing_slash(dir_node->directory_path, next_parent_dir) == 0;
            if (next_is_file_in_listed_dir)
            {
                break;
            }

            path_t next_subdir_name;
            read_next_subdir_name(dir_node->directory_path, next_parent_dir, next_subdir_name, sizeof(next_subdir_name));
            if (strcmp(next_subdir_name, entry->name) != 0)
            {
                break;
            }

            next_driver = get_next_driver_node(ctx, next_driver, dir_node->directory_path);
        }
    }
    dir_node->driver = next_driver;
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
 *
 * Configuration happens in two phases so that ordering can be resolved across
 * the whole config tree instead of just in directory-read order:
 *  1. Walk the config tree and collect every driver configuration into a
 *     pending queue, ordered by `driver_order` (see collect_driver_configs).
 *  2. Actually create the drivers, pulling forward (out of order if needed)
 *     any pending driver that is required (per dmod) by the one being
 *     configured, so a driver is never dmdrvi_create'd before the modules it
 *     depends on have already gone through their own configuration.
 */
static int configure_drivers(dmfsi_context_t ctx, const char* driver_name, const char* config_path)
{
    dmlist_context_t* pending = dmlist_create(DMOD_MODULE_NAME);
    dmlist_context_t* config_files = dmlist_create(DMOD_MODULE_NAME);
    if (pending == NULL || config_files == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate driver configuration queues\n");
        dmlist_destroy(pending);
        dmlist_destroy(config_files);
        return DMFSI_ERR_GENERAL;
    }

    int res = collect_driver_configs(pending, config_files, driver_name, config_path);
    if (res == DMFSI_OK)
    {
        configure_pending_drivers(ctx, pending);
    }

    size_t pending_count = dmlist_size(pending);
    for (size_t i = 0; i < pending_count; i++)
    {
        Dmod_Free(dmlist_get(pending, i));
    }
    dmlist_destroy(pending);

    size_t config_files_count = dmlist_size(config_files);
    for (size_t i = 0; i < config_files_count; i++)
    {
        dmini_destroy((dmini_context_t)dmlist_get(config_files, i));
    }
    dmlist_destroy(config_files);

    return res;
}

/**
 * @brief Walk the config tree, queuing every driver configuration it finds
 *
 * This mirrors the directory traversal that used to configure drivers
 * immediately, except it only records what needs to be configured. The INI
 * contexts are kept alive in `config_files` until the whole tree has been
 * queued and configured, since a single file's context may still be needed
 * later on (e.g. as a dependency of a driver queued elsewhere).
 */
static int collect_driver_configs(dmlist_context_t* pending, dmlist_context_t* config_files, const char* driver_name, const char* config_path)
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

            if (!dmlist_push_back(config_files, config_ctx))
            {
                DMOD_LOG_ERROR("Failed to track config context for: %s\n", full_path);
                dmini_destroy(config_ctx);
                continue;
            }

            // Section-specific driver_name entries take priority over the file/directory
            // derived driver name. Only queue the main driver when no section-level
            // drivers are present in the file.
            int section_drivers_added = collect_section_driver_configs(pending, config_ctx);
            if (section_drivers_added == 0)
            {
                int driver_order = dmini_get_int(config_ctx, INI_MAIN_SECTION, "driver_order", 0);
                if (!add_pending_driver(pending, module_name, config_ctx, NULL, driver_order))
                {
                    DMOD_LOG_ERROR("Failed to queue driver configuration: %s\n", module_name);
                }
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
            int res = collect_driver_configs(pending, config_files, driver_name, full_path);
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
static driver_node_t* configure_driver(const char* driver_name, dmini_context_t config_ctx, const char* section_name)
{
    DMOD_LOG_STEP_BEGIN("Configuring driver: %s\n", driver_name);
    bool was_loaded = false;
    bool was_enabled = false;
    DMOD_LOG_STEP_PROGRESS(25, "Loading driver module: %s\n", driver_name);
    Dmod_Context_t* driver = prepare_driver_module(driver_name, &was_loaded, &was_enabled);
    if (driver == NULL)
    {
        DMOD_LOG_STEP(1, "Failed to configure driver: %s\n", driver_name);
        return NULL;
    }

    DMOD_LOG_STEP_PROGRESS(50, "Resolving driver interface: %s\n", driver_name);
    dmod_dmdrvi_create_t dmdrvi_create = Dmod_GetDifFunction(driver, dmod_dmdrvi_create_sig);
    if (dmdrvi_create == NULL)
    {
        DMOD_LOG_ERROR("Driver module does not implement dmdrvi_create: %s\n", driver_name);
        cleanup_driver_module(driver_name, was_loaded, was_enabled);
        DMOD_LOG_STEP(1, "Failed to configure driver: %s\n", driver_name);
        return NULL;
    }

    driver_node_t* driver_node = Dmod_Malloc(sizeof(driver_node_t));
    if (driver_node == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate memory for driver node: %s\n", driver_name);
        cleanup_driver_module(driver_name, was_loaded, was_enabled);
        DMOD_LOG_STEP(1, "Failed to configure driver: %s\n", driver_name);
        return NULL;
    }

    DMOD_LOG_STEP_PROGRESS(75, "Creating driver context: %s\n", driver_name);
    driver_node->was_loaded = was_loaded;
    driver_node->was_enabled = was_enabled;
    driver_node->is_dynamic = false;
    driver_node->driver = driver;
    driver_node->driver_context = dmdrvi_create(config_ctx, &driver_node->dev_num);
    if (driver_node->driver_context == NULL)
    {
        DMOD_LOG_ERROR("Failed to create driver context: %s\n", driver_name);
        cleanup_driver_module(driver_name, was_loaded, was_enabled);
        Dmod_Free(driver_node);
        DMOD_LOG_STEP(1, "Failed to configure driver: %s\n", driver_name);
        return NULL;
    }

    if ((driver_node->dev_num.flags & DMDRVI_NUM_ALT_NAME) != 0 && driver_node->dev_num.alt_name[0] == '\0')
    {
        if (section_name != NULL && section_name[0] != '\0' && strcmp(section_name, driver_name) != 0)
        {
            Dmod_SnPrintf(driver_node->dev_num.alt_name, sizeof(driver_node->dev_num.alt_name), "%s", section_name);
        }
        else
        {
            driver_node->dev_num.flags &= ~DMDRVI_NUM_ALT_NAME;
        }
    }

    DMOD_LOG_STEP_PROGRESS(90, "Reading driver node path: %s\n", driver_name);
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
        DMOD_LOG_STEP(1, "Failed to configure driver: %s\n", driver_name);
        return NULL;
    }

    DMOD_LOG_STEP(0, "Configured driver: %s (path: %s)\n", driver_name, driver_node->path);

    return driver_node;
}

/**
 * @brief Queue drivers for non-main sections that contain a driver_name key
 *
 * Iterates over all sections in the dmini context using dmini_section_count
 * and dmini_section_name. For each non-main section that contains a driver_name
 * key, a pending entry is queued that remembers the section name so the INI
 * context can be restricted to it (via dmini_set_active_section) at the point
 * it is actually configured.
 *
 * Returns the number of section-specific drivers that were successfully queued.
 * A non-zero return value signals to the caller that the file is a multi-driver
 * config and no fallback main driver should be queued.
 */
static int collect_section_driver_configs(dmlist_context_t* pending, dmini_context_t config_ctx)
{
    int num_added = 0;
    int section_count = dmini_section_count(config_ctx);

    for (int i = 0; i < section_count; i++)
    {
        const char* section_name = dmini_section_name(config_ctx, i);

        // Skip the global (unnamed) section and the [main] section
        if (section_name == NULL || strcmp(section_name, INI_MAIN_SECTION) == 0) continue;

        // Only process sections that declare a driver_name
        if (!dmini_has_key(config_ctx, section_name, "driver_name")) continue;

        const char* drv_name = dmini_get_string(config_ctx, section_name, "driver_name", NULL);
        if (drv_name == NULL) continue;

        int driver_order = dmini_get_int(config_ctx, section_name, "driver_order", 0);

        if (add_pending_driver(pending, drv_name, config_ctx, section_name, driver_order))
        {
            num_added++;
        }
        else
        {
            DMOD_LOG_ERROR("Failed to queue driver configuration for section [%s]: %s\n",
                           section_name, drv_name);
        }
    }

    return num_added;
}

/**
 * @brief Queue a driver configuration, inserted in `driver_order` position
 *
 * Entries are kept sorted by ascending driver_order (default 0), using a
 * stable insertion so entries that share the same driver_order stay in the
 * order they were discovered while walking the config tree.
 */
static bool add_pending_driver(dmlist_context_t* pending, const char* module_name, dmini_context_t config_ctx, const char* section_name, int driver_order)
{
    pending_driver_t* new_entry = Dmod_Malloc(sizeof(pending_driver_t));
    if (new_entry == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate pending driver entry: %s\n", module_name);
        return false;
    }

    memset(new_entry, 0, sizeof(*new_entry));
    strncpy(new_entry->module_name, module_name, sizeof(new_entry->module_name));
    new_entry->module_name[sizeof(new_entry->module_name) - 1] = '\0';
    new_entry->config_ctx = config_ctx;
    new_entry->driver_order = driver_order;
    if (section_name != NULL)
    {
        new_entry->has_section = true;
        strncpy(new_entry->section_name, section_name, sizeof(new_entry->section_name));
        new_entry->section_name[sizeof(new_entry->section_name) - 1] = '\0';
    }

    size_t count = dmlist_size(pending);
    size_t insert_pos = count;
    for (size_t i = 0; i < count; i++)
    {
        pending_driver_t* existing = (pending_driver_t*)dmlist_get(pending, i);
        if (existing->driver_order > driver_order)
        {
            insert_pos = i;
            break;
        }
    }

    if (!dmlist_insert(pending, insert_pos, new_entry))
    {
        Dmod_Free(new_entry);
        return false;
    }

    return true;
}

/**
 * @brief Configure every queued driver, respecting driver_order and dependencies
 */
static int configure_pending_drivers(dmfsi_context_t ctx, dmlist_context_t* pending)
{
    size_t count = dmlist_size(pending);
    for (size_t i = 0; i < count; i++)
    {
        pending_driver_t* entry = (pending_driver_t*)dmlist_get(pending, i);
        if (!entry->configured)
        {
            configure_pending_entry(ctx, pending, entry);
        }
    }
    return DMFSI_OK;
}

/**
 * @brief Configure a single queued driver, first configuring any of its
 *        still-pending required modules
 *
 * dmod already guarantees required modules are loaded and enabled before a
 * driver is loaded, but that is not enough: a required module may not have
 * gone through its own dmdrvi_create yet, so it can still be functionally
 * unconfigured (e.g. a clock not yet set up) when a dependent driver tries to
 * use it during its own configuration. Resolving dependencies here, ahead of
 * driver_order, ensures a driver's prerequisites are always configured first.
 */
static bool configure_pending_entry(dmfsi_context_t ctx, dmlist_context_t* pending, pending_driver_t* entry)
{
    if (entry->configured)
    {
        return true;
    }
    if (entry->configuring)
    {
        DMOD_LOG_ERROR("Circular driver dependency detected while configuring: %s\n", entry->module_name);
        return false;
    }

    entry->configuring = true;
    bool deps_ok = configure_required_dependencies(ctx, pending, entry);
    entry->configuring = false;

    if (!deps_ok)
    {
        DMOD_LOG_ERROR("Failed to configure driver: %s\n", entry->module_name);
        return false;
    }

    // Restrict the INI context to this entry's section (if any) so the driver
    // only sees its own keys, mirroring what the immediate-configuration code
    // used to do around each dmdrvi_create call. Without this, section == NULL
    // lookups inside the driver resolve to the empty global section instead of
    // the intended one, silently handing the driver only default values.
    driver_node_t* driver_node;
    if (entry->has_section)
    {
        dmini_set_active_section(entry->config_ctx, entry->section_name, 0);
        driver_node = configure_driver(entry->module_name, entry->config_ctx, entry->section_name);
        dmini_clear_active_section(entry->config_ctx, 0);
    }
    else
    {
        driver_node = configure_driver(entry->module_name, entry->config_ctx, NULL);
    }

    if (driver_node == NULL)
    {
        DMOD_LOG_ERROR("Failed to configure driver: %s\n", entry->module_name);
        return false;
    }

    if (!dmlist_push_back(ctx->drivers, driver_node))
    {
        DMOD_LOG_ERROR("Failed to add driver to list: %s\n", entry->module_name);
        Dmod_Free(driver_node);
        return false;
    }

    // Only true if this driver is configured into an already-mounted
    // instance rather than during the initial _init() (e.g. a future
    // runtime-added configuration) - dmfsi_dmdevfs_mounted() handles the
    // initial batch once the mount itself becomes ready.
    dmosi_mutex_lock(g_devfs_mutex);
    bool ready = ctx->ready;
    path_t mount_path;
    if (ready)
    {
        strncpy(mount_path, ctx->mount_path, sizeof(mount_path));
        mount_path[sizeof(mount_path) - 1] = '\0';
    }
    dmosi_mutex_unlock(g_devfs_mutex);
    if (ready)
    {
        notify_driver_path_ready(mount_path, driver_node);
    }

    entry->configured = true;
    return true;
}

/**
 * @brief Make sure every module required (per dmod) by `entry` is already configured
 *
 * Dependencies are read via Dmod_IsModuleRequired() against dmod's live module
 * registry rather than via Dmod_FindModuleFile()/Dmod_ReadRequiredModules() on
 * a module file. The file-based APIs only work when each module exists as a
 * standalone .dmf/.dmfc on a search path; on targets where all modules are
 * bundled into a single embedded package (e.g. modules.dmp baked into ROM),
 * no individual file can ever be found, silently turning dependency detection
 * into a no-op. Dmod_IsModuleRequired() instead inspects the module's context
 * once it is loaded (from a file or from a package alike), so it works in
 * both deployment styles.
 */
static bool configure_required_dependencies(dmfsi_context_t ctx, dmlist_context_t* pending, pending_driver_t* entry)
{
    // Load (without enabling/configuring) so dmod records entry's required
    // modules; prepare_driver_module() will see it as already loaded shortly
    // after and skip straight to enabling it.
    if (Dmod_LoadModuleByName(entry->module_name) == NULL)
    {
        // Let configure_driver() surface and log the real load failure.
        return true;
    }

    size_t count = dmlist_size(pending);
    for (size_t i = 0; i < count; i++)
    {
        pending_driver_t* candidate = (pending_driver_t*)dmlist_get(pending, i);
        if (candidate == entry || candidate->configured)
        {
            continue;
        }

        if (!Dmod_IsModuleRequired(entry->module_name, candidate->module_name))
        {
            continue;
        }

        if (!configure_pending_entry(ctx, pending, candidate))
        {
            DMOD_LOG_ERROR("Failed to configure required driver '%s' needed by '%s'\n",
                           candidate->module_name, entry->module_name);
            return false;
        }
    }

    return true;
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
            // Dynamic (hot-plugged) nodes share driver_context/driver with the node that
            // owns them (created via dmdrvi_create) - only the owner may free the context
            // or release the module, otherwise it would be freed/unloaded more than once.
            if (!driver_node->is_dynamic)
            {
                dmod_dmdrvi_free_t dmdrvi_free = Dmod_GetDifFunction(driver_node->driver, dmod_dmdrvi_free_sig);
                if (dmdrvi_free != NULL)
                {
                    dmdrvi_free(driver_node->driver_context);
                    DMOD_LOG_INFO("Freed driver context for: %s\n", Dmod_GetName(driver_node->driver));
                }
                cleanup_driver_module(Dmod_GetName(driver_node->driver), driver_node->was_loaded, driver_node->was_enabled);
            }
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
 * @brief Extract directory name from a path, handling trailing slashes
 * @param path Path to extract directory name from (may have trailing slash)
 * @param dir_name Output buffer for directory name
 * @param name_size Size of output buffer
 * 
 * Examples:
 * - "dev/" -> "dev"
 * - "/dev/" -> "dev"
 * - "dmspiflash0/" -> "dmspiflash0"
 * - "/" -> "" (root has no name)
 */
static void read_dir_name_from_path(const char* path, char* dir_name, size_t name_size)
{
    if (path == NULL || dir_name == NULL || name_size == 0)
    {
        if (dir_name && name_size > 0)
        {
            dir_name[0] = '\0';
        }
        return;
    }

    // Find the length without trailing slashes
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
    {
        len--;
    }

    // Special case: if path is just "/", return empty
    if (len == 1 && path[0] == '/')
    {
        dir_name[0] = '\0';
        return;
    }

    // Find the last slash before the directory name
    const char* last_slash = NULL;
    for (size_t i = 0; i < len; i++)
    {
        if (path[i] == '/')
        {
            last_slash = &path[i];
        }
    }

    // Extract the directory name
    const char* name_start = (last_slash != NULL) ? last_slash + 1 : path;
    size_t name_len = len - (name_start - path);
    
    // Use snprintf for safe copying with guaranteed null-termination
    int written = Dmod_SnPrintf(dir_name, name_size, "%.*s", (int)name_len, name_start);
    if (written < 0 || (size_t)written >= name_size)
    {
        // Truncated, but snprintf ensures null-termination
        dir_name[name_size - 1] = '\0';
    }
}

/**
 * @brief Extract the first path component of full_path that comes after base_path
 * @param base_path The directory currently being listed (e.g., "/" or "/foo")
 * @param full_path The driver's parent directory path (e.g., "/foo/bar/")
 * @param dir_name  Output buffer for the immediate subdirectory name
 * @param name_size Size of the output buffer
 *
 * Examples:
 * - base="/",       full="/dmgpio8/"   -> "dmgpio8"
 * - base="/",       full="/a/b/c/"     -> "a"
 * - base="/a",      full="/a/b/c/"     -> "b"
 * - base="/a/b",    full="/a/b/c/"     -> "c"
 */
static void read_next_subdir_name(const char* base_path, const char* full_path, char* dir_name, size_t name_size)
{
    if (base_path == NULL || full_path == NULL || dir_name == NULL || name_size == 0)
    {
        if (dir_name && name_size > 0)
        {
            dir_name[0] = '\0';
        }
        return;
    }

    // Compute effective length of base_path without trailing slashes
    size_t base_len = strlen(base_path);
    while (base_len > 1 && base_path[base_len - 1] == '/')
    {
        base_len--;
    }

    const char* start;
    if (base_len == 1 && base_path[0] == '/')
    {
        // Base is the root directory; skip the leading '/' in full_path if present
        start = (full_path[0] == '/') ? full_path + 1 : full_path;
    }
    else
    {
        // Skip past the base_path prefix and the separator '/'
        if (strncmp(full_path, base_path, base_len) == 0 && full_path[base_len] == '/')
        {
            start = full_path + base_len + 1;
        }
        else
        {
            // Fallback: return the first component of full_path
            start = full_path;
        }
    }

    // Copy up to the next '/' (or end of string)
    const char* end = start;
    while (*end != '\0' && *end != '/')
    {
        end++;
    }

    size_t len = (size_t)(end - start);
    if (len >= name_size)
    {
        len = name_size - 1;
    }
    strncpy(dir_name, start, len);
    dir_name[len] = '\0';
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
        Dmod_SnPrintf(path_buffer, buffer_size, "/%s%u/", driver_name, node->dev_num.major);
    }
    else if(minor_given)
    {
        Dmod_SnPrintf(path_buffer, buffer_size, "/%sx/", driver_name);
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
    bool name_given = (node->dev_num.flags & DMDRVI_NUM_ALT_NAME) != 0 && node->dev_num.alt_name[0] != '\0';
    size_t current_length = strlen(path_buffer);
    if(current_length >= buffer_size)
    {
        DMOD_LOG_ERROR("Buffer too small for driver path\n");
        return DMFSI_ERR_NO_SPACE;
    }
    path_buffer += current_length;
    buffer_size -= current_length;
    if(name_given)
    {
        Dmod_SnPrintf(path_buffer, buffer_size, "%s", node->dev_num.alt_name);
    }
    else if(minor_given)
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
 * @brief Compare two paths, ignoring trailing slashes
 * @param path1 First path to compare
 * @param path2 Second path to compare  
 * @return 0 if equal, non-zero if different
 * 
 * Note: The root path "/" is treated specially and retains its slash.
 * For example, "/" and "//" are considered equal, but "dir" and "dir/" are also equal.
 */
static int compare_paths_ignore_trailing_slash( const char* path1, const char* path2 )
{
    // Handle NULL pointers - both NULL is equal, one NULL is different
    if (path1 == NULL && path2 == NULL)
    {
        return 0;
    }
    if (path1 == NULL || path2 == NULL)
    {
        return 1;
    }

    // Get lengths
    size_t len1 = strlen(path1);
    size_t len2 = strlen(path2);
    
    // Remove trailing slashes from both paths for comparison
    // Keep at least "/" if that's the entire path (len > 1 ensures we keep root "/")
    while (len1 > 1 && path1[len1 - 1] == '/')
    {
        len1--;
    }
    while (len2 > 1 && path2[len2 - 1] == '/')
    {
        len2--;
    }
    
    // Lengths must match
    if (len1 != len2)
    {
        return 1;
    }
    
    // Content must match
    return strncmp(path1, path2, len1);
}

/**
 * @brief Compare the path of a driver directory with a given path
 * 
 * This function checks whether a driver node is reachable from the given path,
 * either directly (its parent directory exactly matches path) or indirectly
 * (its parent directory is a subdirectory of path).
 * 
 * @param data Pointer to driver_node_t
 * @param user_data Pointer to directory path string
 * @return 0 if the node is reachable from the given path, non-zero otherwise
 * 
 * Example: When listing directory "/", this function returns 0 for any driver
 * node, including those with parent "dmspiflash0/" or deeper paths.
 * When listing "dmspiflash0", it returns 0 for nodes whose parent starts with
 * "dmspiflash0/", enabling both direct files and nested subdirectories.
 * 
 * Note: Trailing slashes are ignored in comparison.
 */
static int compare_driver_directory( const void* data, const void* user_data )
{
    const driver_node_t* node = (const driver_node_t*)data;
    const char* path = (const char*)user_data;
    if (node == NULL || path == NULL)
    {
        return 0;
    }

    char parent_dir[MAX_PATH_LENGTH] = {0};
    if(read_driver_parent_directory(node, parent_dir, sizeof(parent_dir)) != 0)
    {
        return -1;
    }

    // Check for exact match (driver is directly in this directory)
    if (compare_paths_ignore_trailing_slash(path, parent_dir) == 0)
    {
        return 0;
    }

    // Check if the driver is in a subdirectory of path.
    // Determine the effective length of path without trailing slashes.
    size_t path_len = strlen(path);
    while (path_len > 1 && path[path_len - 1] == '/')
    {
        path_len--;
    }

    // Root directory "/" is an ancestor of every path
    if (path_len == 1 && path[0] == '/')
    {
        return 0;
    }

    // parent_dir must start with path followed by '/' to be a subdirectory
    if (strncmp(parent_dir, path, path_len) == 0 && parent_dir[path_len] == '/')
    {
        return 0;
    }

    return 1;
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
 *
 * Locked: ctx->drivers may be concurrently mutated by the hot-plug worker
 * thread (see dmdrvi_device_available()/_unavailable()).
 */
static bool is_directory( dmfsi_context_t ctx, const char* path )
{
    if (strcmp(path, ROOT_DIRECTORY_NAME) == 0)
    {
        return true;
    }
    dmosi_mutex_lock(g_devfs_mutex);
    bool found = dmlist_find(ctx->drivers, path, compare_driver_directory) != NULL;
    dmosi_mutex_unlock(g_devfs_mutex);
    return found;
}

/**
 * @brief Get the next driver node in a directory
 *
 * Locked: see is_directory().
 */
static driver_node_t* get_next_driver_node( dmfsi_context_t ctx, driver_node_t* current, const char* path )
{
    dmosi_mutex_lock(g_devfs_mutex);
    driver_node_t* next = (driver_node_t*)dmlist_find_next(ctx->drivers, current, path, compare_driver_directory);
    dmosi_mutex_unlock(g_devfs_mutex);
    return next;
}

/**
 * @brief Find a driver node by its path
 *
 * Locked: see is_directory().
 */
static driver_node_t* find_driver_node( dmfsi_context_t ctx, const char* path )
{
    dmosi_mutex_lock(g_devfs_mutex);
    driver_node_t* node = (driver_node_t*)dmlist_find(ctx->drivers, path, compare_driver_node_path);
    dmosi_mutex_unlock(g_devfs_mutex);
    return node;
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

/**
 * @brief Compare two device numbers for equality
 *
 * Only the fields flagged as valid in `flags` are compared, matching how
 * drivers report which of major/minor/alt_name they actually use.
 */
static bool dev_num_equal( const dmdrvi_dev_num_t* a, const dmdrvi_dev_num_t* b )
{
    if (a->flags != b->flags)
    {
        return false;
    }
    if ((a->flags & DMDRVI_NUM_MAJOR) != 0 && a->major != b->major)
    {
        return false;
    }
    if ((a->flags & DMDRVI_NUM_MINOR) != 0 && a->minor != b->minor)
    {
        return false;
    }
    if ((a->flags & DMDRVI_NUM_ALT_NAME) != 0 && strcmp(a->alt_name, b->alt_name) != 0)
    {
        return false;
    }
    return true;
}

/**
 * @brief Compare a driver node's driver_context against a target dmdrvi_context_t
 *
 * Matches the node that originally called dmdrvi_create() as well as any
 * dynamic (hot-plugged) nodes sharing that same context.
 */
static int compare_driver_context( const void* data, const void* user_data )
{
    const driver_node_t* node = (const driver_node_t*)data;
    if (node == NULL || user_data == NULL)
    {
        return -1;
    }
    return (node->driver_context == (dmdrvi_context_t)user_data) ? 0 : -1;
}

/**
 * @brief Compare a driver node against a (context, dev_num) lookup, matching
 *        only dynamic (hot-plugged) nodes
 */
static int compare_dynamic_device( const void* data, const void* user_data )
{
    const driver_node_t* node = (const driver_node_t*)data;
    const dynamic_device_lookup_t* lookup = (const dynamic_device_lookup_t*)user_data;
    if (node == NULL || lookup == NULL || !node->is_dynamic)
    {
        return -1;
    }
    if (node->driver_context != lookup->context)
    {
        return -1;
    }
    return dev_num_equal(&node->dev_num, lookup->dev_num) ? 0 : -1;
}

/**
 * @brief Compare a mount (dmfsi_context_t) against a target pointer
 */
static int compare_mount_context( const void* data, const void* user_data )
{
    return (data == user_data) ? 0 : -1;
}

/**
 * @brief Match the mount (dmfsi_context_t) whose driver list contains a node
 *        for the given dmdrvi_context_t
 */
static int compare_mount_owns_context( const void* data, const void* user_data )
{
    const struct dmfsi_context* mount = (const struct dmfsi_context*)data;
    if (mount == NULL || user_data == NULL)
    {
        return -1;
    }
    return (dmlist_find(mount->drivers, user_data, compare_driver_context) != NULL) ? 0 : -1;
}

/**
 * @brief Match the mount (dmfsi_context_t) whose driver list contains a
 *        dynamic device for the given (context, dev_num) lookup
 */
static int compare_mount_owns_dynamic_device( const void* data, const void* user_data )
{
    const struct dmfsi_context* mount = (const struct dmfsi_context*)data;
    if (mount == NULL || user_data == NULL)
    {
        return -1;
    }
    return (dmlist_find(mount->drivers, user_data, compare_dynamic_device) != NULL) ? 0 : -1;
}

/**
 * @brief MAL implementation of dmdrvi_device_available()
 *
 * Called by a driver to announce that a new device (e.g. a hot-plugged
 * sub-device or a dynamically discovered channel) became available within
 * a driver context it previously created via dmdrvi_create(). Only queues the
 * event for the hot-plug worker thread - see process_device_available() for
 * the actual work - so the calling driver never blocks on it.
 */
void dmdrvi_device_available( dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num )
{
    if (context == NULL || dev_num == NULL)
    {
        DMOD_LOG_ERROR("dmdrvi_device_available: invalid arguments\n");
        return;
    }

    hotplug_event_t event = { .context = context, .dev_num = *dev_num, .available = true };
    if (dmosi_queue_send(g_hotplug_queue, &event, 0) != 0)
    {
        DMOD_LOG_ERROR("dmdrvi_device_available: hot-plug queue full, event dropped\n");
    }
}

/**
 * @brief MAL implementation of dmdrvi_device_unavailable()
 *
 * Counterpart of dmdrvi_device_available(): queues removal of the device file
 * exposed for a previously announced device. See process_device_unavailable().
 */
void dmdrvi_device_unavailable( dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num )
{
    if (context == NULL || dev_num == NULL)
    {
        DMOD_LOG_ERROR("dmdrvi_device_unavailable: invalid arguments\n");
        return;
    }

    hotplug_event_t event = { .context = context, .dev_num = *dev_num, .available = false };
    if (dmosi_queue_send(g_hotplug_queue, &event, 0) != 0)
    {
        DMOD_LOG_ERROR("dmdrvi_device_unavailable: hot-plug queue full, event dropped\n");
    }
}

/**
 * @brief Combine this mount's own absolute path with a path relative to it
 *
 * Used by notify_driver_path_ready() to build the path pushed to a driver.
 *
 * @return 0 on success, negative value on failure (buffer too small)
 */
static int build_absolute_path( const char* mount_path, const char* node_path, char* out, size_t out_size )
{
    bool mount_is_root = (strcmp(mount_path, ROOT_DIRECTORY_NAME) == 0);
    int written = mount_is_root
        ? Dmod_SnPrintf(out, out_size, "%s", node_path)
        : Dmod_SnPrintf(out, out_size, "%s%s", mount_path, node_path);

    if (written < 0 || (size_t)written >= out_size)
    {
        DMOD_LOG_ERROR("build_absolute_path: path buffer too small\n");
        return -1;
    }

    return 0;
}

/**
 * @brief Push a device's now-known absolute path to its driver, if implemented
 *
 * Called once dmdevfs itself is ready (mount->ready) and `node` is
 * registered - either while flushing already-registered devices from
 * dmfsi_dmdevfs_mounted(), or immediately upon registering a device that
 * arrives after dmdevfs is already mounted (hot-plug, or any future
 * runtime-added driver configuration).
 *
 * Must be called without g_devfs_mutex held: it calls into the driver's own
 * dif implementation.
 */
static void notify_driver_path_ready( const char* mount_path, driver_node_t* node )
{
    dmod_dmdrvi_path_ready_t path_ready = Dmod_GetDifFunction(node->driver, dmod_dmdrvi_path_ready_sig);
    if (path_ready == NULL)
    {
        return;
    }

    path_t abs_path;
    if (build_absolute_path(mount_path, node->path, abs_path, sizeof(abs_path)) != 0)
    {
        DMOD_LOG_ERROR("notify_driver_path_ready: failed to build absolute path for %s\n", node->path);
        return;
    }

    path_ready(node->driver_context, &node->dev_num, abs_path);
}

/**
 * @brief dmlist_foreach() callback pushing the path to every already-registered driver
 */
static bool notify_all_drivers_path_ready( void* data, void* user_data )
{
    notify_driver_path_ready((const char*)user_data, (driver_node_t*)data);
    return true;
}

/**
 * @brief DIF implementation of dmfsi_mounted()
 *
 * Called by the mounter (e.g. dmvfs) once this mount is fully registered,
 * handing this mount's own absolute path directly. Caches it, marks the
 * mount ready, and flushes the path to every driver already configured
 * during _init() (which ran before this mount could be resolved). Any driver
 * registered afterwards (hot-plug, or a future runtime-added configuration)
 * is instead notified immediately at registration time - see
 * configure_pending_entry() and process_device_available().
 */
dmod_dmfsi_dif_api_declaration( 1.0, dmdevfs, void, _mounted, (dmfsi_context_t ctx, const char* mount_path) )
{
    if (dmfsi_dmdevfs_context_is_valid(ctx) == 0 || mount_path == NULL)
    {
        DMOD_LOG_ERROR("Invalid arguments in mounted\n");
        return;
    }

    dmosi_mutex_lock(g_devfs_mutex);
    strncpy(ctx->mount_path, mount_path, sizeof(ctx->mount_path));
    ctx->mount_path[sizeof(ctx->mount_path) - 1] = '\0';
    ctx->ready = true;
    dmosi_mutex_unlock(g_devfs_mutex);

    // Unlocked: notify_driver_path_ready() calls into driver code, so it
    // must not run while g_devfs_mutex is held. The list itself is not
    // mutated here, only iterated - concurrent hot-plug insertions are
    // handled by process_device_available() directly.
    dmlist_foreach(ctx->drivers, notify_all_drivers_path_ready, ctx->mount_path);
}

/**
 * @brief Hot-plug worker thread entry point
 *
 * Blocks on the queue and dispatches each event to process_device_available()
 * or process_device_unavailable(). Exits when it receives the shutdown
 * sentinel (an event with context == NULL) sent by dmod_deinit().
 */
static void hotplug_worker_thread( void* arg )
{
    (void)arg;

    for (;;)
    {
        hotplug_event_t event;
        if (dmosi_queue_receive(g_hotplug_queue, &event, -1) != 0)
        {
            continue;
        }
        if (event.context == NULL)
        {
            break;
        }

        if (event.available)
        {
            process_device_available(event.context, &event.dev_num);
        }
        else
        {
            process_device_unavailable(event.context, &event.dev_num);
        }
    }
}

/**
 * @brief Do the actual work for a queued dmdrvi_device_available() event
 *
 * Runs on the hot-plug worker thread, in two separately-locked steps so that
 * read_driver_node_path() - which can itself call DMOD_LOG_ERROR() on failure -
 * never runs while g_devfs_mutex is held:
 *
 *  1. Locked: look up the owning mount, reject duplicates, and copy out the
 *     (stable, pointer-only) fields needed to build the new node.
 *  2. Unlocked: allocate the node and compute its path (may log).
 *  3. Locked again: re-validate the mount is still registered - if _deinit()
 *     raced us and won, mount may now be a dangling pointer, so this check
 *     must only ever compare it (never dereference it) until proven live -
 *     and only then insert the node.
 */
static void process_device_available( dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num )
{
    dmosi_mutex_lock(g_devfs_mutex);

    dmfsi_context_t mount = (dmfsi_context_t)dmlist_find(g_dmdevfs_mounts, (void*)context, compare_mount_owns_context);
    if (mount == NULL)
    {
        dmosi_mutex_unlock(g_devfs_mutex);
        DMOD_LOG_ERROR("dmdrvi_device_available: driver context not registered with any dmdevfs mount\n");
        return;
    }

    dynamic_device_lookup_t lookup = { .context = context, .dev_num = dev_num };
    if (dmlist_find(mount->drivers, &lookup, compare_dynamic_device) != NULL)
    {
        dmosi_mutex_unlock(g_devfs_mutex);
        DMOD_LOG_ERROR("dmdrvi_device_available: device already registered\n");
        return;
    }

    driver_node_t* owner = (driver_node_t*)dmlist_find(mount->drivers, (void*)context, compare_driver_context);
    dmdrvi_context_t owner_context = owner->driver_context;
    Dmod_Context_t* owner_driver = owner->driver;

    dmosi_mutex_unlock(g_devfs_mutex);

    driver_node_t* new_node = Dmod_Malloc(sizeof(driver_node_t));
    if (new_node == NULL)
    {
        DMOD_LOG_ERROR("dmdrvi_device_available: failed to allocate device node\n");
        return;
    }

    new_node->driver_context = owner_context;
    new_node->driver = owner_driver;
    new_node->dev_num = *dev_num;
    new_node->was_loaded = false;
    new_node->was_enabled = false;
    new_node->is_dynamic = true;

    if (read_driver_node_path(new_node, new_node->path, sizeof(new_node->path)) != 0)
    {
        DMOD_LOG_ERROR("dmdrvi_device_available: failed to compute device path\n");
        Dmod_Free(new_node);
        return;
    }

    dmosi_mutex_lock(g_devfs_mutex);
    bool mount_still_valid = dmlist_find(g_dmdevfs_mounts, mount, compare_mount_context) != NULL;
    bool inserted = mount_still_valid && dmlist_push_back(mount->drivers, new_node);
    // Captured under lock, alongside mount_still_valid: mount must not be
    // dereferenced again once we can no longer prove it is still live.
    bool mount_ready = mount_still_valid && mount->ready;
    path_t mount_path;
    if (mount_ready)
    {
        strncpy(mount_path, mount->mount_path, sizeof(mount_path));
        mount_path[sizeof(mount_path) - 1] = '\0';
    }
    dmosi_mutex_unlock(g_devfs_mutex);

    if (!mount_still_valid)
    {
        DMOD_LOG_ERROR("dmdrvi_device_available: mount was unmounted while processing event\n");
        Dmod_Free(new_node);
        return;
    }
    if (!inserted)
    {
        DMOD_LOG_ERROR("dmdrvi_device_available: failed to register device node\n");
        Dmod_Free(new_node);
        return;
    }

    // Only true for a device announced after dmdevfs itself is already
    // mounted (the normal hot-plug case) - a device announced while dmdevfs
    // is still being mounted is instead flushed by dmfsi_dmdevfs_mounted().
    if (mount_ready)
    {
        notify_driver_path_ready(mount_path, new_node);
    }

    DMOD_LOG_INFO("Hot-plugged device now available: %s\n", new_node->path);
}

/**
 * @brief Do the actual work for a queued dmdrvi_device_unavailable() event
 *
 * Runs on the hot-plug worker thread. See process_device_available() for the
 * locking rationale.
 */
static void process_device_unavailable( dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num )
{
    dmosi_mutex_lock(g_devfs_mutex);

    dynamic_device_lookup_t lookup = { .context = context, .dev_num = dev_num };
    dmfsi_context_t mount = (dmfsi_context_t)dmlist_find(g_dmdevfs_mounts, &lookup, compare_mount_owns_dynamic_device);
    if (mount == NULL)
    {
        dmosi_mutex_unlock(g_devfs_mutex);
        DMOD_LOG_ERROR("dmdrvi_device_unavailable: device not registered with any dmdevfs mount\n");
        return;
    }

    driver_node_t* node = (driver_node_t*)dmlist_find(mount->drivers, &lookup, compare_dynamic_device);
    path_t removed_path;
    strncpy(removed_path, node->path, sizeof(removed_path));
    removed_path[sizeof(removed_path) - 1] = '\0';
    dmlist_remove(mount->drivers, node, compare_driver);

    dmosi_mutex_unlock(g_devfs_mutex);

    Dmod_Free(node);
    DMOD_LOG_INFO("Hot-plugged device no longer available: %s\n", removed_path);
}
