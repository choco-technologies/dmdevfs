#include "dmdevfs_scan.h"
#include "dmini.h"
#include "dmdevfs_driver.h"
#include "dmdevfs_node.h"
#include "dmdevfs_context.h"
#include <string.h>

/**
 * @brief returns an extension of the file
 */
static const char* get_extension(const char* path)
{
    size_t len = strlen(path);
    if(len == 0)
    {
        return NULL;
    }

    const char* ext = &path[len-1];
    while(ext >= path)
    {
        if(*ext == '.')
        {
            return ext + 1;
        }
        ext--;
    }
    
    return NULL;
}

/**
 * @brief checks if the given file has the expected extension
 */
static const bool is_extension( const char* path, const char* expected )
{
    const char* ext = get_extension(path);
    if(ext == NULL)
    {
        return false;
    }
    return strcmp(ext, expected) == 0;
}

/**
 * @brief checks if the module with the given name exists (without loading it)
 */
static bool module_exists(const char* module_name)
{
    return Dmod_ModuleExists(module_name);
}

/**
 * @brief tries to guess a driver name
 */
static const char* guess_driver_name( dmfsi_context_t context, const char* cfg_path, dmini_context_t ini_context, const char* section_name, const char* dir_name )
{
    const char* driver_name = dmini_get_string(ini_context, section_name, "driver_name", NULL);
    if(driver_name != NULL && module_exists(driver_name))
    {
        return driver_name;
    }

    if( section_name != NULL && module_exists(section_name))
    {
        return section_name;
    }

    if(module_exists(dir_name))
    {
        return dir_name;
    }

    DMOD_LOG_ERROR("Cannot guess driver name for %s/%s dir_name: '%s'\n", cfg_path, section_name != NULL ? section_name : "<global>", dir_name);
    return NULL;
}

/**
 * @brief reads the device order for the given node
 */
static device_order_t get_device_order( dmini_context_t ini_context, const char* section_name )
{
    if(dmini_has_key(ini_context, section_name, "driver_order"))
    {
        return (device_order_t)(ini_context, section_name, "driver_order", 0);
    } 
    return (device_order_t)dmini_get_int(ini_context, section_name, "device_order", 0);
}

/**
 * @brief scans the given configuration directory
 */
bool scan( dmfsi_context_t context, const char* path, const char* dir_name )
{
    void* dir = Dmod_OpenDir(path);
    if(dir == NULL)
    {
        DMOD_LOG_ERROR("No such directory: '%s'\n", path);
        return false;
    }

    for(const Dmod_DirEntry_t* dir_entry = Dmod_ReadDirEx(dir); dir_entry != NULL; dir_entry = Dmod_ReadDirEx(dir))
    {
        char* full_path = Dmod_StrMerge(3, path, "/", dir_entry->name);
        if(dir_entry->type == Dmod_DirEntryType_Dir )
        {
            scan(context, full_path, dir_entry->name);
        }
        else if(dir_entry->type == Dmod_DirEntryType_File)
        {
            scan_file(context, full_path, dir_name);
        }
        Dmod_Free(full_path);
    }

    Dmod_CloseDir(dir);

    return true;
}

/**
 * @brief scans the given configuration file
 */
bool scan_file( dmfsi_context_t context, const char* path, const char* dir_name )
{
    if(!is_extension(path, "ini"))
    {
        DMOD_LOG_INFO("Skipping unrecognized file: '%s'\n", path);
        return false;    
    }

    dmini_context_t ini_context = dmini_create();

    if(dmini_parse_file(ini_context, path) != 0)
    {
        DMOD_LOG_ERROR("Cannot parse configuration file at '%s'\n", path);
        dmini_destroy(ini_context);
        return false;
    }

    bool success = false;
    int section_count = dmini_section_count(ini_context);
    if(section_count == 0)
    {
        success = scan_section(context, ini_context, NULL, path, dir_name);
    }
    else 
    {
        for(int section_index = 0; section_index < section_count; section_index++)
        {
            const char* section_name = dmini_section_name(ini_context, section_index);
            success = scan_section(context, ini_context, section_name, path, dir_name) || success;
        }
    }

    dmini_destroy(ini_context);
    return true;
}

/**
 * @brief scans the given section
 */
bool scan_section( dmfsi_context_t context, dmini_context_t ini_context, const char* section_name, const char* cfg_path, const char* dir_name )
{
    const char* driver_name = guess_driver_name(context, cfg_path, ini_context, section_name, dir_name);
    if(driver_name == NULL)
    {
        return false;
    }
    driver_t* driver = context_find_driver(context, driver_name);
    if(driver == NULL)
    {
        driver = driver_new(driver_name);
        if(driver == NULL)
        {
            DMOD_LOG_ERROR("Cannot create driver '%s' for %s/%s\n", driver_name, cfg_path, section_name != NULL ? section_name : "<global>");
            return false;
        }

        if(!context_add_driver(context, driver))
        {
            DMOD_LOG_ERROR("Cannot add driver %s for %s/%s\n", driver_name, cfg_path, section_name != NULL ? section_name : "<global>");
            return false;
        }
    }

    device_node_t* node = node_new(cfg_path, section_name, driver, get_device_order(ini_context, section_name));
    if(node == NULL)
    {
        DMOD_LOG_ERROR("Cannot create node %s for %s/%s\n", driver_name, cfg_path, section_name != NULL ? section_name : "<global>");
        return false;
    }

    if(!context_add_node(context, node))
    {
        DMOD_LOG_ERROR("Cannot add node %s for %s/%s\n", driver_name, cfg_path, section_name != NULL ? section_name : "<global>");
        node_delete(node);
        return false;
    }

    return true;
}