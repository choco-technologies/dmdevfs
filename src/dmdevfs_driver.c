#include "dmdevfs_driver.h"

/**
 * @brief creates new driver
 */
driver_t* driver_new( const char* module_name )
{
    driver_t* driver = Dmod_Malloc( sizeof(driver_t) );
    if(driver == NULL)  
    {
        DMOD_LOG_ERROR("Cannot allocate memory for the driver\n");
        return NULL;
    }

    driver->magic = DMDEVFS_DRIVER_MAGIC;
    driver->mutex = Dmod_Mutex_New(true);
    driver->context = NULL;
    if(driver->mutex == NULL)
    {
        DMOD_LOG_ERROR("Cannot create driver mutex\n");
        driver_delete(driver);
        return NULL;
    }

    driver->name = Dmod_StrDup( module_name );
    if(driver->name == NULL)
    {
        DMOD_LOG_ERROR("Cannot allocate memory for module name: '%s'\n", module_name);
        driver_delete(driver);
        return NULL;
    }

    return driver;
}

/**
 * @brief deletes the driver
 */
void driver_delete( driver_t* driver )
{
    if(!driver_is_valid(driver))
    {
        DMOD_LOG_ERROR("Cannot delete invalid driver\n");
        return;
    }

    driver->magic = 0;
    if(driver->mutex != NULL)
    {
        Dmod_Mutex_Delete(driver->mutex);
    }
    if(driver->name != NULL)
    {
        Dmod_Free(driver->name);
    }

    Dmod_Free(driver);
}

/**
 * @brief checks if the given driver structure is valid
 */
bool driver_is_valid( driver_t* driver )
{
    Dmod_EnterCritical();
    bool valid = driver != NULL && driver->magic == DMDEVFS_DRIVER_MAGIC;
    Dmod_ExitCritical();
    return valid;
}

/**
 * locks the driver
 */
bool driver_lock( driver_t* driver )
{
    return driver_is_valid(driver) && Dmod_Mutex_Lock(driver->mutex);
}

/**
 * @brief unlocks the given driver
 */
void driver_unlock( driver_t* driver )
{
    driver_is_valid(driver) && Dmod_Mutex_Unlock(driver->mutex);
}

/**
 * @brief checks if the driver is loaded
 */
bool driver_is_loaded( driver_t* driver )
{
    return driver_is_valid(driver) && Dmod_IsModuleLoaded(driver->name);
}

/**
 * @brief checks if the driver is enabled
 */
bool driver_is_enabled( driver_t* driver )
{
    return driver_is_valid(driver) && Dmod_IsModuleEnabled(driver->name);
}

/**
 * @brief loads the driver
 */
bool driver_load( driver_t* driver )
{
    if(driver_lock(driver))
    {
        if(!driver_is_loaded(driver))
        {
            driver->context = Dmod_LoadModuleByName(driver->name);
        }
        else 
        {
            driver->context = Dmod_GetModuleContext(driver->name);
        }
        driver_unlock(driver);
        return driver->context != NULL;
    }
    return false;
}

/**
 * @brief enables the driver
 */
bool driver_enable( driver_t* driver )
{
    if(driver_lock(driver))
    {
        bool loaded = driver_load(driver);
        if(!loaded)
        {
            DMOD_LOG_ERROR("Cannot enable module '%s' - module cannot be loaded\n", driver->name);
            driver_unlock(driver);
            return false;
        }
        bool enabled = driver_is_enabled(driver);
        if(!enabled)
        {
            enabled = Dmod_EnableModule(driver->name, false, NULL);
            if(!enabled)
            {
                DMOD_LOG_ERROR("Cannot enable module '%s'\n", driver->name);
            }
        }
        driver_unlock(driver);
        return enabled;
    }
    return false;
}