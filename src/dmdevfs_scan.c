#include "dmdevfs_scan.h"
#include "dmini.h"
#include "dmdevfs_node.h"
#include <string.h>

/**
 * @brief returns an extension of the file
 */
const char* get_extension(const char* path)
{
    size_t len = strlen(path);
    if(len == 0)
    {
        return NULL;
    }

    const char* ext = &path[len-1];
    while(ext >= path)
    {
        if(ext == '.')
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
const bool is_extension( const char* path, const char* expected )
{
    const char* ext = get_extension(path);
    if(ext == NULL)
    {
        return false;
    }
    return strcmp(ext, expected) == 0;
}

/**
 * @brief scans the given configuration directory
 */
bool scan( dmfsi_context_t context, const char* path, const char* driver_name )
{
    void* dir = Dmod_OpenDir(path);
    if(dir == NULL)
    {
        DMOD_LOG_ERROR("No such directory: '%s'\n", path);
        return false;
    }

    for(const Dmod_DirEntry_t* dir_entry = Dmod_ReadDirEx(dir); dir_entry != NULL; dir_entry = Dmod_ReadDirEx(dir))
    {
        const char* full_path = Dmod_StrMerge(3, path, "/", dir_entry->name);
        if(dir_entry->type == Dmod_DirEntryType_Dir )
        {
            scan(context, full_path, dir_entry->name);
        }
        else if(dir_entry->type == Dmod_DirEntryType_File)
        {
            scan_file(context, full_path, dir_entry->name);
        }
        Dmod_Free(full_path);
    }

    Dmod_CloseDir(dir);

    return true;
}

/**
 * @brief scans the given configuration file
 */
bool scan_file( dmfsi_context_t context, const char* path, const char* driver_name )
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

    int section_count = dmini_section_count(ini_context);
    if(section_count == 0)
    {
        
    }

    return true;
}