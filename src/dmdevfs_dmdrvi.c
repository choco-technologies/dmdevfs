#include "dmdevfs_dmdrvi.h"
#include "dmdevfs_driver.h"

/**
 * @brief calls the create function of the dmdrvi interface
 */
dmdrvi_context_t dmdrvi_create( driver_t* driver, dmini_context_t config, dmdrvi_dev_num_t* dev_num )
{
    dmdrvi_context_t context = NULL;

    if(driver_lock(driver))
    {
        if(!driver_enable(driver))
        {
            DMOD_LOG_ERROR("Cannot configure driver %s - it cannot be enabled\n", driver->name);
        }
        driver_begin_usage(driver);
        if(driver->api.create == NULL)
        {
            driver->api.create = Dmod_GetDifFunction( driver->context, dmod_dmdrvi_create_sig );
        }
        if(driver->api.create != NULL)
        {
            context = driver->api.create(config, dev_num);
        }

        driver_end_usage(driver);
        driver_unlock(driver);
    }

    return context;
}