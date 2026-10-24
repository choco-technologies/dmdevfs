#include "dmdevfs_scan.h"
#include "dmini.h"

/**
 * @brief scans the given configuration directory
 */
bool scan( dmfsi_context_t context, const char* path )
{
    void* dir = Dmod_OpenDir(path);
    if(dir == NULL)
    {
        DMOD_LOG_ERROR("No such directory: '%s'\n", path);
        return false;
    }

    for(const Dmod_DirEntry_t* dir_entry = Dmod_ReadDirEx(dir); dir_entry != NULL; dir_entry = Dmod_ReadDirEx(dir))
    {
        
    }

    Dmod_CloseDir(dir);

    return true;
}