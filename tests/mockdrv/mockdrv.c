#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmdevfs_mockdrv.h"
#include <errno.h>
#include <string.h>

/*
 * Test driver for the libsystemd node reporting of dmdevfs.
 *
 * Configuration keys: `major` (device number) and `monitor` ("true": the host
 * node answers DMDRVI_IOCTL_MONITOR_GET_POLICY). The hot-plugged child
 * (minor 0) always answers DMDRVI_IOCTL_BLOCK_GET_INFO. Everything else a
 * dmdrvi driver has to provide is a minimal no-op.
 */

#define MOCKDRV_CONTEXT_MAGIC   0x4D4F434Bu     /* 'MOCK' */
#define MOCKDRV_HANDLE_MAGIC    0x4D4F4348u     /* 'MOCH' */

struct dmdrvi_context
{
    uint32_t    magic;
    uint8_t     major;
    bool        monitor;
    bool        plugged;
};

typedef struct
{
    uint32_t    magic;
    bool        is_child;
} mockdrv_handle_t;

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}

static bool is_valid(dmdrvi_context_t ctx)
{
    return ctx != NULL && ctx->magic == MOCKDRV_CONTEXT_MAGIC;
}

static dmdrvi_dev_num_t child_num(dmdrvi_context_t ctx)
{
    dmdrvi_dev_num_t num;
    memset(&num, 0, sizeof(num));
    num.flags = DMDRVI_NUM_MAJOR | DMDRVI_NUM_MINOR;
    num.major = ctx->major;
    num.minor = 0;
    return num;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, dmdrvi_context_t, _create,
    ( dmini_context_t config, dmdrvi_dev_num_t* dev_num ))
{
    struct dmdrvi_context* ctx = Dmod_Malloc(sizeof(*ctx));
    if (ctx == NULL || dev_num == NULL)
    {
        Dmod_Free(ctx);
        return NULL;
    }
    const char* monitor = dmini_get_string(config, NULL, "monitor", "false");
    ctx->magic   = MOCKDRV_CONTEXT_MAGIC;
    ctx->major   = (uint8_t)dmini_get_int(config, NULL, "major", 0);
    ctx->monitor = (strcmp(monitor, "true") == 0);
    ctx->plugged = false;

    memset(dev_num, 0, sizeof(*dev_num));
    dev_num->flags = DMDRVI_NUM_MAJOR;
    dev_num->major = ctx->major;
    return ctx;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, void, _free, ( dmdrvi_context_t context ))
{
    if (is_valid(context))
    {
        context->magic = 0;
        Dmod_Free(context);
    }
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, void*, _open,
    ( dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t* dev_num ))
{
    (void)flags;
    bool is_child = (dev_num != NULL) && (dev_num->flags & DMDRVI_NUM_MINOR) != 0;
    if (!is_valid(context) || (is_child && !context->plugged))
    {
        return NULL;
    }
    mockdrv_handle_t* handle = Dmod_Malloc(sizeof(*handle));
    if (handle != NULL)
    {
        handle->magic = MOCKDRV_HANDLE_MAGIC;
        handle->is_child = is_child;
    }
    return handle;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, void, _close, ( dmdrvi_context_t context, void* handle ))
{
    mockdrv_handle_t* h = (mockdrv_handle_t*)handle;
    if (is_valid(context) && h != NULL && h->magic == MOCKDRV_HANDLE_MAGIC)
    {
        h->magic = 0;
        Dmod_Free(h);
    }
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, dmdrvi_ssize_t, _read,
    ( dmdrvi_context_t context, void* handle, void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    (void)context; (void)handle; (void)buffer; (void)size; (void)offset;
    return 0;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, dmdrvi_ssize_t, _write,
    ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    (void)context; (void)handle; (void)buffer; (void)offset;
    return (dmdrvi_ssize_t)size;
}

static int set_plugged(dmdrvi_context_t context, bool plugged)
{
    if (context->plugged == plugged)
    {
        return -EALREADY;
    }
    context->plugged = plugged;
    dmdrvi_dev_num_t num = child_num(context);
    if (plugged)
    {
        dmdrvi_device_available(context, &num);
    }
    else
    {
        dmdrvi_device_unavailable(context, &num);
    }
    return 0;
}

static int host_ioctl(dmdrvi_context_t context, int command, void* arg)
{
    switch (command)
    {
        case DMDRVI_IOCTL_MONITOR_GET_POLICY:
            if (!context->monitor)
            {
                return -ENOTTY;
            }
            memset(arg, 0, sizeof(dmdrvi_monitor_policy_t));
            return 0;
        case DMDEVFS_MOCKDRV_IOCTL_PLUG:
            return set_plugged(context, true);
        case DMDEVFS_MOCKDRV_IOCTL_UNPLUG:
            return set_plugged(context, false);
        default:
            return -ENOTTY;
    }
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, int, _ioctl,
    ( dmdrvi_context_t context, void* handle, int command, void* arg ))
{
    mockdrv_handle_t* h = (mockdrv_handle_t*)handle;
    if (!is_valid(context) || h == NULL || h->magic != MOCKDRV_HANDLE_MAGIC)
    {
        return -EINVAL;
    }
    if (!h->is_child)
    {
        return host_ioctl(context, command, arg);
    }
    if (command != DMDRVI_IOCTL_BLOCK_GET_INFO)
    {
        return -ENOTTY;
    }
    dmdrvi_block_info_t* info = (dmdrvi_block_info_t*)arg;
    info->logical_block_size = 512;
    info->erase_block_size   = 0;
    info->block_count        = 8;
    info->flags              = DMDRVI_BLOCK_FLAG_REMOVABLE;
    return 0;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, int, _flush, ( dmdrvi_context_t context, void* handle ))
{
    (void)context; (void)handle;
    return 0;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, int, _stat,
    ( dmdrvi_context_t context, const char* path, dmdrvi_stat_t* stat ))
{
    (void)context; (void)path;
    if (stat == NULL)
    {
        return -EINVAL;
    }
    stat->size = 0;
    stat->mode = 0666;
    return 0;
}
