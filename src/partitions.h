/**
 * @file partitions.h
 * @brief Partition table parser (MBR incl. extended/logical, GPT)
 *
 * Pure parser without any dmdevfs state: it reads the medium through a
 * caller supplied callback and reports every partition it finds through
 * another one. dmdevfs uses it to expose partition nodes (see
 * scan_partitions() in dmdevfs.c); it is kept self-contained so the
 * filesystem probing library can reuse it.
 */
#ifndef DMDEVFS_PARTITIONS_H
#define DMDEVFS_PARTITIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Kind of partition table found on a medium. */
typedef enum
{
    dmdevfs_part_table_none = 0,    /**< No (valid) table - e.g. a superfloppy or a blank medium */
    dmdevfs_part_table_mbr,         /**< MBR, possibly with an extended partition */
    dmdevfs_part_table_gpt,         /**< GPT (primary or backup header) */
} dmdevfs_part_table_t;

/**
 * @brief Read exactly @p size bytes at byte @p offset of the medium
 * @return 0 on success, a negative value if the read failed or was short
 */
typedef int (*dmdevfs_part_read_t)(void* ctx, uint64_t offset, void* buffer, size_t size);

/**
 * @brief Called for every partition found
 *
 * @param number    Partition number: MBR primaries 1-4 by slot, logical
 *                  partitions from 5 in chain order; GPT entry index + 1.
 * @param first_lba First block of the partition.
 * @param lba_count Number of blocks.
 * @return false to stop the scan.
 */
typedef bool (*dmdevfs_part_found_t)(void* ctx, uint32_t number, uint64_t first_lba, uint64_t lba_count);

/**
 * @brief Find the partitions of a medium
 *
 * @param read        Medium access.
 * @param read_ctx    Passed to @p read.
 * @param block_size  Logical block size in bytes (512, 1024, 2048 or 4096).
 * @param block_count Number of logical blocks of the medium.
 * @param found       Called for every partition, in table order.
 * @param found_ctx   Passed to @p found.
 * @return The kind of table the partitions were found in.
 */
dmdevfs_part_table_t dmdevfs_partitions_scan(dmdevfs_part_read_t read, void* read_ctx,
                                             uint32_t block_size, uint64_t block_count,
                                             dmdevfs_part_found_t found, void* found_ctx);

#endif // DMDEVFS_PARTITIONS_H
