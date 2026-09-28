#include "dmod.h"
#include "dmosi.h"

/*
 * Stand-in for dmdevmon/automount: stays running while its unit is active.
 * Exits with 1 right away unless argv[1] (the unit's %v, i.e. the node path
 * dmdevfs reported) is an absolute path - so a running unit also proves the
 * path was handed over.
 */
int main(int argc, char* argv[])
{
    if (argc < 2 || argv[1][0] != '/')
    {
        return 1;
    }
    for (;;)
    {
        dmosi_thread_sleep(1000);
    }
    return 0;
}
