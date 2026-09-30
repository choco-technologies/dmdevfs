#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmlist.h"
#include "dmdevfs_mockdrv.h"
#include <errno.h>
#include <string.h>

/*
 * Test driver for the libsystemd node reporting of dmdevfs.
 *
 * Configuration keys: `major` (device number) and `monitor` ("true": the host
 * node implements the monitor contract). Its policy comes from
 * `event_handler`, `settle_ms` and `poll_interval_ms`; MONITOR_EVENT and
 * MONITOR_REFRESH calls are only counted (DMDEVFS_MOCKDRV_IOCTL_GET_STATS).
 * The hot-plugged child (minor 0) is a block device on a sparse medium of
 * DMDEVFS_MOCKDRV_BLOCK_SIZE blocks (default 8, DMDEVFS_MOCKDRV_IOCTL_SET_MEDIA):
 * only sectors ever written are stored, so 64-bit sized media cost nothing.
 * The medium can be prepared through the host node while the child is not
 * plugged (DMDEVFS_MOCKDRV_IOCTL_WRITE_SECTOR). Each context remembers when it
 * was created relative to the others (create_seq in the stats), so the order
 * dmdevfs configures drivers in is observable. Everything else a dmdrvi driver
 * has to provide is a minimal no-op.
 */

#define MOCKDRV_CONTEXT_MAGIC   0x4D4F434Bu     /* 'MOCK' */
#define MOCKDRV_HANDLE_MAGIC    0x4D4F4348u     /* 'MOCH' */

struct dmdrvi_context
{
    uint32_t    magic;
    uint8_t     major;
    bool        monitor;
    bool        plugged;
    dmdrvi_monitor_policy_t     policy;
    volatile uint32_t           events;     /* MONITOR_EVENT calls */
    volatile uint32_t           refreshes;  /* MONITOR_REFRESH calls */
    uint32_t                    create_seq; /* Value of g_create_count after this _create */
    uint64_t                    block_count;/* Medium size of the child */
    dmlist_context_t*           sectors;    /* dmdevfs_mockdrv_sector_t* written so far */
};

typedef struct
{
    uint32_t    magic;
    bool        is_child;
} mockdrv_handle_t;

/* Contexts created so far - only ever grows, so create_seq orders them. */
static uint32_t g_create_count;

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

/* ---- sparse medium ---- */

static int compare_lba(const void* data, const void* user_data)
{
    const dmdevfs_mockdrv_sector_t* sector = (const dmdevfs_mockdrv_sector_t*)data;
    return (sector->lba == *(const uint64_t*)user_data) ? 0 : 1;
}

static void clear_medium(dmdrvi_context_t ctx)
{
    dmdevfs_mockdrv_sector_t* sector;
    while ((sector = dmlist_pop_front(ctx->sectors)) != NULL)
    {
        Dmod_Free(sector);
    }
}

/* The stored sector, created (zeroed) on demand when @p create. */
static dmdevfs_mockdrv_sector_t* sector_at(dmdrvi_context_t ctx, uint64_t lba, bool create)
{
    dmdevfs_mockdrv_sector_t* sector = dmlist_find(ctx->sectors, &lba, compare_lba);
    if (sector == NULL && create && (sector = Dmod_Malloc(sizeof(*sector))) != NULL)
    {
        memset(sector, 0, sizeof(*sector));
        sector->lba = lba;
        if (!dmlist_push_back(ctx->sectors, sector))
        {
            Dmod_Free(sector);
            sector = NULL;
        }
    }
    return sector;
}

static uint64_t medium_bytes(dmdrvi_context_t ctx)
{
    return ctx->block_count * DMDEVFS_MOCKDRV_BLOCK_SIZE;
}

/* Clip a request to the medium: bytes that can be transferred at @p offset. */
static size_t clip(dmdrvi_context_t ctx, dmdrvi_offset_t offset, size_t size)
{
    uint64_t end = medium_bytes(ctx);
    if (offset < 0 || (uint64_t)offset >= end)
    {
        return 0;
    }
    uint64_t left = end - (uint64_t)offset;
    return (size > left) ? (size_t)left : size;
}

static dmdrvi_ssize_t medium_read(dmdrvi_context_t ctx, uint8_t* buffer, size_t size, dmdrvi_offset_t offset)
{
    size = clip(ctx, offset, size);
    for (size_t done = 0; done < size; )
    {
        uint64_t pos = (uint64_t)offset + done;
        size_t in_sector = (size_t)(pos % DMDEVFS_MOCKDRV_BLOCK_SIZE);
        size_t chunk = DMDEVFS_MOCKDRV_BLOCK_SIZE - in_sector;
        chunk = (chunk > size - done) ? size - done : chunk;
        dmdevfs_mockdrv_sector_t* sector = sector_at(ctx, pos / DMDEVFS_MOCKDRV_BLOCK_SIZE, false);
        for (size_t i = 0; i < chunk; i++)
        {
            buffer[done + i] = (sector != NULL) ? sector->data[in_sector + i] : 0;
        }
        done += chunk;
    }
    return (dmdrvi_ssize_t)size;
}

static dmdrvi_ssize_t medium_write(dmdrvi_context_t ctx, const uint8_t* buffer, size_t size, dmdrvi_offset_t offset)
{
    if (clip(ctx, offset, 1) == 0)
    {
        return -ENOSPC;
    }
    size = clip(ctx, offset, size);
    for (size_t done = 0; done < size; )
    {
        uint64_t pos = (uint64_t)offset + done;
        size_t in_sector = (size_t)(pos % DMDEVFS_MOCKDRV_BLOCK_SIZE);
        size_t chunk = DMDEVFS_MOCKDRV_BLOCK_SIZE - in_sector;
        chunk = (chunk > size - done) ? size - done : chunk;
        dmdevfs_mockdrv_sector_t* sector = sector_at(ctx, pos / DMDEVFS_MOCKDRV_BLOCK_SIZE, true);
        if (sector == NULL)
        {
            return -ENOMEM;
        }
        memcpy(sector->data + in_sector, buffer + done, chunk);
        done += chunk;
    }
    return (dmdrvi_ssize_t)size;
}

static int medium_erase(dmdrvi_context_t ctx, const dmdrvi_block_range_t* range)
{
    uint64_t end = medium_bytes(ctx);
    if (range == NULL || range->offset < 0 || (uint64_t)range->offset > end ||
        range->length > end - (uint64_t)range->offset ||
        range->offset % DMDEVFS_MOCKDRV_BLOCK_SIZE != 0 || range->length % DMDEVFS_MOCKDRV_BLOCK_SIZE != 0)
    {
        return -EINVAL;
    }
    uint64_t first = (uint64_t)range->offset / DMDEVFS_MOCKDRV_BLOCK_SIZE;
    uint64_t last = first + range->length / DMDEVFS_MOCKDRV_BLOCK_SIZE;
    for (size_t i = dmlist_size(ctx->sectors); i-- > 0; )
    {
        dmdevfs_mockdrv_sector_t* sector = dmlist_get(ctx->sectors, i);
        if (sector->lba >= first && sector->lba < last)
        {
            dmlist_remove_at(ctx->sectors, i);
            Dmod_Free(sector);
        }
    }
    return 0;
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
    ctx->events = 0;
    ctx->refreshes = 0;
    ctx->create_seq = ++g_create_count;
    ctx->block_count = 8;
    ctx->sectors = dmlist_create();
    memset(&ctx->policy, 0, sizeof(ctx->policy));
    Dmod_SnPrintf(ctx->policy.event_handler, sizeof(ctx->policy.event_handler), "%s",
                  dmini_get_string(config, NULL, "event_handler", ""));
    ctx->policy.settle_ms = (uint32_t)dmini_get_int(config, NULL, "settle_ms", 0);
    ctx->policy.poll_interval_ms = (uint32_t)dmini_get_int(config, NULL, "poll_interval_ms", 0);

    memset(dev_num, 0, sizeof(*dev_num));
    dev_num->flags = DMDRVI_NUM_MAJOR;
    dev_num->major = ctx->major;
    return ctx;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, void, _free, ( dmdrvi_context_t context ))
{
    if (is_valid(context))
    {
        clear_medium(context);
        dmlist_destroy(context->sectors);
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
    mockdrv_handle_t* h = (mockdrv_handle_t*)handle;
    if (!is_valid(context) || h == NULL || h->magic != MOCKDRV_HANDLE_MAGIC)
    {
        return -EINVAL;
    }
    return h->is_child ? medium_read(context, (uint8_t*)buffer, size, offset) : 0;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmdevfs_mockdrv, dmdrvi_ssize_t, _write,
    ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    mockdrv_handle_t* h = (mockdrv_handle_t*)handle;
    if (!is_valid(context) || h == NULL || h->magic != MOCKDRV_HANDLE_MAGIC)
    {
        return -EINVAL;
    }
    return h->is_child ? medium_write(context, (const uint8_t*)buffer, size, offset) : (dmdrvi_ssize_t)size;
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

static int monitor_ioctl(dmdrvi_context_t context, int command, void* arg)
{
    if (!context->monitor)
    {
        return -ENOTTY;
    }
    switch (command)
    {
        case DMDRVI_IOCTL_MONITOR_GET_POLICY:
            *(dmdrvi_monitor_policy_t*)arg = context->policy;
            return 0;
        case DMDRVI_IOCTL_MONITOR_EVENT:
            context->events++;
            return 0;
        default:
            context->refreshes++;
            return context->plugged ? 0 : -ENODEV;
    }
}

static int host_ioctl(dmdrvi_context_t context, int command, void* arg)
{
    switch (command)
    {
        case DMDRVI_IOCTL_MONITOR_GET_POLICY:
        case DMDRVI_IOCTL_MONITOR_EVENT:
        case DMDRVI_IOCTL_MONITOR_REFRESH:
            return monitor_ioctl(context, command, arg);
        case DMDEVFS_MOCKDRV_IOCTL_SET_MEDIA:
            if (context->plugged || arg == NULL)
            {
                return -EBUSY;
            }
            clear_medium(context);
            context->block_count = *(const uint64_t*)arg;
            return 0;
        case DMDEVFS_MOCKDRV_IOCTL_WRITE_SECTOR:
        {
            const dmdevfs_mockdrv_sector_t* raw = (const dmdevfs_mockdrv_sector_t*)arg;
            dmdevfs_mockdrv_sector_t* sector = (raw != NULL && raw->lba < context->block_count)
                                             ? sector_at(context, raw->lba, true) : NULL;
            if (sector == NULL)
            {
                return -EINVAL;
            }
            memcpy(sector->data, raw->data, sizeof(sector->data));
            return 0;
        }
        case DMDEVFS_MOCKDRV_IOCTL_GET_STATS:
        {
            dmdevfs_mockdrv_stats_t* stats = (dmdevfs_mockdrv_stats_t*)arg;
            stats->events = context->events;
            stats->refreshes = context->refreshes;
            stats->create_seq = context->create_seq;
            return 0;
        }
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
    if (command == DMDRVI_IOCTL_BLOCK_ERASE)
    {
        return medium_erase(context, (const dmdrvi_block_range_t*)arg);
    }
    if (command != DMDRVI_IOCTL_BLOCK_GET_INFO)
    {
        return -ENOTTY;
    }
    dmdrvi_block_info_t* info = (dmdrvi_block_info_t*)arg;
    info->logical_block_size = DMDEVFS_MOCKDRV_BLOCK_SIZE;
    info->erase_block_size   = DMDEVFS_MOCKDRV_BLOCK_SIZE;
    info->block_count        = context->block_count;
    info->flags              = DMDRVI_BLOCK_FLAG_REMOVABLE | DMDRVI_BLOCK_FLAG_ERASE_SUPPORTED;
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
    if (!is_valid(context) || path == NULL || stat == NULL)
    {
        return -EINVAL;
    }
    /* "/dmdevfs_mockdrv<major>/0" is the child: the medium's size. */
    const char* last = strrchr(path, '/');
    bool child = (last != NULL && last != path && strcmp(last + 1, "0") == 0);
    stat->size = (child && context->plugged) ? medium_bytes(context) : 0;
    stat->mode = 0666;
    return 0;
}
