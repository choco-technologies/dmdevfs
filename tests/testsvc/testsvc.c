#include "dmod.h"
#include "dmosi.h"
#include "dmhaman.h"
#include "libsystemd.h"

/*
 * Stand-in for dmdevmon/automount: stays running while its unit is active.
 *
 * Exits with 1 right away unless argv[1] (the unit's %v, i.e. the node path
 * dmdevfs reported) is an absolute path - so a running unit also proves the
 * path was handed over.
 *
 * It stops gracefully (libsystemd_set_stop_semaphore()) and registers a
 * dmhaman handler named after argv[1] once it is set up: the test only stops
 * a unit after seeing that handler. Killing a process that is still starting
 * up inside the loader can leave a global loader lock held and hang every
 * later module operation - exactly what graceful stop avoids.
 */

static int ready_marker(void* parameters, void* user_ctx)
{
    (void)parameters;
    (void)user_ctx;
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 2 || argv[1][0] != '/')
    {
        return 1;
    }
    dmosi_semaphore_t wakeup = dmosi_semaphore_create(0, 1);
    if (wakeup == NULL)
    {
        return 1;
    }
    libsystemd_set_stop_semaphore(wakeup);
    dmhaman_register_handler(argv[1], ready_marker, NULL);   /* argv outlives the registration */

    while (!libsystemd_stop_requested())
    {
        dmosi_semaphore_wait(wakeup, 1, 1000);
    }

    dmhaman_unregister_handler(argv[1], ready_marker);
    libsystemd_set_stop_semaphore(NULL);
    dmosi_semaphore_destroy(wakeup);
    return 0;
}
