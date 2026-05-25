#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ROOT_DIRECTORY_NAME "/"
#define MAX_NAME_LENGTH 64
#define DMFSI_OK 0
#define DMFSI_ERR_NOT_FOUND -2

typedef struct
{
    char path[MAX_NAME_LENGTH];
    char alt_path[MAX_NAME_LENGTH];
    bool has_alt;
} mock_driver_t;

typedef struct
{
    char name[MAX_NAME_LENGTH];
} mock_dir_entry_t;

typedef struct
{
    const mock_driver_t* drivers;
    size_t driver_count;
    size_t primary_index;
    size_t alt_index;
    bool in_alt_phase;
    const char* directory_path;
} mock_dir_iter_t;

static const char* normalize_directory_path(const char* path)
{
    if (path != NULL && path[0] == '\0')
    {
        return ROOT_DIRECTORY_NAME;
    }
    return path;
}

static int compare_paths_ignore_trailing_slash(const char* path1, const char* path2)
{
    if (path1 == NULL && path2 == NULL) return 0;
    if (path1 == NULL || path2 == NULL) return 1;

    size_t len1 = strlen(path1);
    size_t len2 = strlen(path2);
    while (len1 > 1 && path1[len1 - 1] == '/') len1--;
    while (len2 > 1 && path2[len2 - 1] == '/') len2--;

    if (len1 != len2) return 1;
    return strncmp(path1, path2, len1);
}

static void read_base_name(const char* path, char* base_name, size_t size)
{
    const char* last_slash = strrchr(path, '/');
    const char* name_start = (last_slash != NULL) ? last_slash + 1 : path;
    strncpy(base_name, name_start, size);
    base_name[size - 1] = '\0';
}

static void read_parent_dir(const char* path, char* parent, size_t size)
{
    const char* last_slash = strrchr(path, '/');
    if (last_slash == NULL || last_slash == path)
    {
        strncpy(parent, ROOT_DIRECTORY_NAME, size);
        parent[size - 1] = '\0';
        return;
    }

    size_t parent_len = (size_t)(last_slash - path);
    if (parent_len >= size) parent_len = size - 1;
    memcpy(parent, path, parent_len);
    parent[parent_len] = '\0';
}

static int mock_readdir(mock_dir_iter_t* it, mock_dir_entry_t* entry)
{
    if (!it->in_alt_phase)
    {
        while (it->primary_index < it->driver_count)
        {
            const mock_driver_t* driver = &it->drivers[it->primary_index++];
            char parent[MAX_NAME_LENGTH];
            read_parent_dir(driver->path, parent, sizeof(parent));

            if (compare_paths_ignore_trailing_slash(it->directory_path, parent) == 0)
            {
                read_base_name(driver->path, entry->name, sizeof(entry->name));
                return DMFSI_OK;
            }
        }

        it->in_alt_phase = true;
    }

    while (it->alt_index < it->driver_count)
    {
        const mock_driver_t* driver = &it->drivers[it->alt_index++];
        if (!driver->has_alt) continue;
        if (compare_paths_ignore_trailing_slash(normalize_directory_path(it->directory_path), ROOT_DIRECTORY_NAME) != 0)
        {
            continue;
        }

        read_base_name(driver->alt_path, entry->name, sizeof(entry->name));
        return DMFSI_OK;
    }

    return DMFSI_ERR_NOT_FOUND;
}

int main(void)
{
    const mock_driver_t drivers[] = {
        {.path = "/dmgpio3", .alt_path = "/temp_sensor", .has_alt = true},
        {.path = "/dmuart1", .alt_path = "", .has_alt = false},
    };

    mock_dir_iter_t it = {
        .drivers = drivers,
        .driver_count = sizeof(drivers) / sizeof(drivers[0]),
        .primary_index = 0,
        .alt_index = 0,
        .in_alt_phase = false,
        .directory_path = "",
    };

    mock_dir_entry_t entry;
    bool found_alt = false;

    while (mock_readdir(&it, &entry) == DMFSI_OK)
    {
        if (strcmp(entry.name, "temp_sensor") == 0)
        {
            found_alt = true;
            break;
        }
    }

    assert(found_alt && "Alternative root name should be listed for empty root path");
    printf("ok: alternative name listed in mocked driver flow\n");
    return 0;
}
