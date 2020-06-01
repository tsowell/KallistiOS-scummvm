/* KallistiOS ##version##

   fat.c
   Copyright (C) 2012, 2013, 2019 Lawrence Sebald
*/

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "fatfs.h"
#include "fatinternal.h"

/* This is basically the same as bgrad_cache from fs_iso9660 */
static void make_mru(fat_fs_t *fs, fat_cache_t **cache, int block) {
    int i;
    fat_cache_t *tmp;

    /* Don't try it with the end block */
    if(block < 0 || block >= fs->cache_size - 1)
        return;

    /* Make a copy and scoot everything down */
    tmp = cache[block];

    for(i = block; i < fs->cache_size - 1; ++i) {
        cache[i] = cache[i + 1];
    }

    cache[fs->cache_size - 1] = tmp;
}


static int fat_fatblock_read_nc(fat_fs_t *fs, uint32_t bn, uint8_t *rv) {
    if(fs->sb.fat_size <= bn)
        return -EINVAL;

    if(fs->dev->read_blocks(fs->dev, bn, 1, rv))
        return -EIO;

    return 0;
}

static int fat_fatblock_write_nc(fat_fs_t *fs, uint32_t bn,
                                 const uint8_t *blk) {
    if(fs->sb.fat_size <= bn)
        return -EINVAL;

    if(fs->dev->write_blocks(fs->dev, bn, 1, blk))
        return -EIO;

    return 0;
}

static uint8_t *fat_read_fatblock(fat_fs_t *fs, uint32_t block, int *err) {
    int i;
    uint8_t *rv;
    fat_cache_t **cache = fs->fcache;

    /* Look through the cache from the most recently used to the least recently
       used entry. */
    for(i = fs->fcache_size - 1; i >= 0; --i) {
        if(cache[i]->block == block && cache[i]->flags) {
            rv = cache[i]->data;
            make_mru(fs, cache, i);
            goto out;
        }
    }

    /* If we didn't get anything, did we end up with an invalid entry or do we
       need to boot someone out? */
    if(i < 0) {
        i = 0;

        /* Make sure that if the block is dirty, we write it back out. */
        if(cache[0]->flags & FAT_CACHE_FLAG_DIRTY) {
            if(fat_fatblock_write_nc(fs, cache[0]->block, cache[0]->data)) {
                /* XXXX: Uh oh... */
                *err = EIO;
                return NULL;
            }
        }
    }

    /* Try to read the block in question. */
    if(fat_fatblock_read_nc(fs, block, cache[i]->data)) {
        *err = EIO;
        cache[i]->flags = 0;            /* Mark it as invalid... */
        return NULL;
    }

    cache[i]->block = block;
    cache[i]->flags = FAT_CACHE_FLAG_VALID;
    rv = cache[i]->data;
    make_mru(fs, cache, i);

out:
    return rv;
}

static int fat_fatblock_mark_dirty(fat_fs_t *fs, uint32_t bn) {
    int i;
    fat_cache_t **cache = fs->fcache;

    /* Look through the cache from the most recently used to the least recently
       used entry. */
    for(i = fs->fcache_size - 1; i >= 0; --i) {
        if(cache[i]->block == bn && cache[i]->flags) {
            cache[i]->flags |= FAT_CACHE_FLAG_DIRTY;
            make_mru(fs, cache, i);
            return 0;
        }
    }

    return -EINVAL;
}

int fat_fatblock_cache_wb(fat_fs_t *fs) {
    int i, err;
    fat_cache_t **cache = fs->fcache;

    /* Don't even bother if we're mounted read-only. */
    if(!(fs->mnt_flags & FAT_MNT_FLAG_RW))
        return 0;

    for(i = fs->fcache_size - 1; i >= 0; --i) {
        if(cache[i]->flags & FAT_CACHE_FLAG_DIRTY) {
            if((err = fat_fatblock_write_nc(fs, cache[i]->block,
                                            cache[i]->data)))
                return err;

            cache[i]->flags &= ~FAT_CACHE_FLAG_DIRTY;
        }
    }

    return 0;
}

uint32_t fat_read_fat(fat_fs_t *fs, uint32_t cl, int *err) {
    uint32_t sn, off, val;
    const uint8_t *blk, *blk2;

    /* Figure out what sector the value is on... */
    switch(fs->sb.fs_type) {
        case FAT_FS_FAT32:
            cl <<= 2;
            sn = fs->sb.reserved_sectors + (cl / fs->sb.bytes_per_sector);
            off = cl & (fs->sb.bytes_per_sector - 1);

            /* Read the FAT block. */
            blk = fat_read_fatblock(fs, sn, err);
            if(!blk)
                return FAT_INVALID_CLUSTER;

            val = blk[off] | (blk[off + 1] << 8) | (blk[off + 2] << 16) |
                (blk[off + 3] << 24);
            break;

        case FAT_FS_FAT16:
            cl <<= 1;
            sn = fs->sb.reserved_sectors + (cl / fs->sb.bytes_per_sector);
            off = cl & (fs->sb.bytes_per_sector - 1);

            /* Read the FAT block. */
            blk = fat_read_fatblock(fs, sn, err);
            if(!blk)
                return FAT_INVALID_CLUSTER;

            val = blk[off] | (blk[off + 1] << 8);
            break;

        case FAT_FS_FAT12:
            off = (cl >> 1) + cl;
            sn = fs->sb.reserved_sectors + (off / fs->sb.bytes_per_sector);
            off = off & (fs->sb.bytes_per_sector - 1);

            /* Read the FAT block. */
            blk = fat_read_fatblock(fs, sn, err);
            if(!blk)
                return 0xFFFFFFFF;

            /* See if we have the very special case of the entry spanning two
               blocks... This is why we can't have nice things... */
            if(off == (uint16_t)(fs->sb.bytes_per_sector - 1)) {
                blk2 = fat_read_fatblock(fs, sn + 1, err);

                if(!blk2)
                    return FAT_INVALID_CLUSTER;

                /* The bright side here is that we at least know that the
                   cluster number is odd... */
                val = (blk[off] | (blk2[0] << 8)) >> 4;
            }
            else {
                val = blk[off] | (blk[off + 1] << 8);

                /* Which 12 bits do we want? */
                if(cl & 1)
                    val = val >> 4;
                else
                    val = val & 0x0FFF;
            }
            break;

        default:
            *err = EBADF;
            return FAT_INVALID_CLUSTER;
    }

    return val;
}

int fat_write_fat(fat_fs_t *fs, uint32_t cl, uint32_t val) {
    uint32_t sn, off;
    uint8_t *blk, *blk2;
    int err;

    /* Don't let us write to the FAT if we're on a read-only FS. */
    if(!(fs->mnt_flags & FAT_MNT_FLAG_RW))
        return -EROFS;

    /* Figure out what sector the value is on... */
    switch(fs->sb.fs_type) {
        case FAT_FS_FAT32:
            cl <<= 2;
            sn = fs->sb.reserved_sectors + (cl / fs->sb.bytes_per_sector);
            off = cl & (fs->sb.bytes_per_sector - 1);

            /* Read the FAT block. */
            blk = fat_read_fatblock(fs, sn, &err);
            if(!blk)
                return err;

            blk[off] = (uint8_t)val;
            blk[off + 1] = (uint8_t)(val >> 8);
            blk[off + 2] = (uint8_t)(val >> 16);

            /* Don't overwrite the top 4 bits... */
            blk[off + 3] = (blk[off + 3] & 0xF0) | ((val >> 24) & 0x0F);

            /* Mark it as dirty... */
            fat_fatblock_mark_dirty(fs, sn);
            break;

        case FAT_FS_FAT16:
            cl <<= 1;
            sn = fs->sb.reserved_sectors + (cl / fs->sb.bytes_per_sector);
            off = cl & (fs->sb.bytes_per_sector - 1);

            /* Read the FAT block. */
            blk = fat_read_fatblock(fs, sn, &err);
            if(!blk)
                return err;

            blk[off] = (uint8_t)val;
            blk[off + 1] = (uint8_t)(val >> 8);

            /* Mark it as dirty... */
            fat_fatblock_mark_dirty(fs, sn);
            break;

        case FAT_FS_FAT12:
            off = (cl >> 1) + cl;
            sn = fs->sb.reserved_sectors + (off / fs->sb.bytes_per_sector);
            off = off & (fs->sb.bytes_per_sector - 1);

            /* Read the FAT block. */
            blk = fat_read_fatblock(fs, sn, &err);
            if(!blk)
                return err;

            /* See if we have the very special case of the entry spanning two
               blocks... This is why we can't have nice things... */
            if(off == (uint16_t)(fs->sb.bytes_per_sector - 1)) {
                blk2 = fat_read_fatblock(fs, sn + 1, &err);

                if(!blk2)
                    return err;

                /* The bright side here is that we at least know that the
                   cluster number is odd... */
                val <<= 4;
                blk[off] = (uint8_t)((blk[off] & 0x0F) | (val & 0xF0));
                blk2[0] = (uint8_t)(val >> 8);

                /* Mark it as dirty... */
                fat_fatblock_mark_dirty(fs, sn);
                fat_fatblock_mark_dirty(fs, sn + 1);
            }
            else {
                if(cl & 1) {
                    val <<= 4;
                    blk[off] = (uint8_t)((blk[off] & 0x0F) | (val & 0xF0));
                    blk[off + 1] = (uint8_t)(val >> 8);
                }
                else {
                    blk[off + 1] = (uint8_t)((blk[off + 1] & 0xF0) |
                        ((val >> 8) & 0x0F));
                    blk[off] = (uint8_t)(val);
                }

                /* Mark it as dirty... */
                fat_fatblock_mark_dirty(fs, sn);
            }
            break;
    }

    return 0;
}

int fat_is_eof(fat_fs_t *fs, uint32_t cl) {
    switch(fs->sb.fs_type) {
        case FAT_FS_FAT32:
            return ((cl & 0x0FFFFFFF) >= FAT_EOC_FAT32);

        case FAT_FS_FAT16:
            return (cl >= FAT_EOC_FAT16 && !(cl & 0x80000000));

        case FAT_FS_FAT12:
            return (cl >= FAT_EOC_FAT12 && !(cl & 0x80000000));
    }

    return -1;
}

uint32_t fat_allocate_cluster(fat_fs_t *fs, int *err) {
    uint32_t sn, off, val;
    uint8_t *blk;
    uint32_t cl, i, cps, last;
    int tries = 1;

    /* Don't let us write to the FAT if we're on a read-only FS. */
    if(!(fs->mnt_flags & FAT_MNT_FLAG_RW)) {
        *err = EROFS;
        return FAT_INVALID_CLUSTER;
    }

    i = fs->sb.last_alloc_cluster + 1;
    last = fs->sb.num_clusters + 2;

    /* Search for a free cluster in the FAT...
       There are optimized versions here for FAT32 and FAT16. Perhaps I'll write
       one for FAT12 at some point too... */
    switch(fs->sb.fs_type) {
        case FAT_FS_FAT32:
retry_fat32:
            cps = (fs->sb.bytes_per_sector >> 2) - 1;
            cl = i << 2;
            sn = fs->sb.reserved_sectors + (cl / fs->sb.bytes_per_sector);

            if(!(blk = fat_read_fatblock(fs, sn, err)))
                return FAT_INVALID_CLUSTER;

            while(i < last) {
                off = cl & (fs->sb.bytes_per_sector - 1);
                val = blk[off] | (blk[off + 1] << 8) | (blk[off + 2] << 16) |
                    (blk[off + 3] << 24);

                /* Did we find a free cluster? */
                if(val == 0) {
                    /* Put an end of chain marker in to allocate it. */
                    blk[off] = 0xFF;
                    blk[off + 1] = 0xFF;
                    blk[off + 2] = 0xFF;
                    blk[off + 3] = (blk[off + 3] & 0xF0) | 0x0F;

                    /* Mark the block as dirty so it can get flushed to the
                       backing store at some point. */
                    fat_fatblock_mark_dirty(fs, sn);

                    fs->sb.last_alloc_cluster = i;
                    --fs->sb.free_clusters;
		    fs->flags |= FAT_FS_FLAG_SB_DIRTY;
                    return i;
                }

                ++i;
                cl += 4;

                /* Do we have to read a new block? */
                if(!(i & cps) && i < fs->sb.num_clusters + 2) {
                    ++sn;

                    if(!(blk = fat_read_fatblock(fs, sn, err)))
                        return FAT_INVALID_CLUSTER;
                }
            }

            /* If we get here, then restart the search and try one more time.
               If we get here a second time, then there really aren't any
               clusters left. */
            if(tries--) {
                i = 2;
                last = fs->sb.last_alloc_cluster;
                goto retry_fat32;
            }

            *err = ENOSPC;
            return FAT_INVALID_CLUSTER;

        case FAT_FS_FAT16:
retry_fat16:
            cps = (fs->sb.bytes_per_sector >> 1) - 1;
            cl = i << 1;
            sn = fs->sb.reserved_sectors + (cl / fs->sb.bytes_per_sector);

            if(!(blk = fat_read_fatblock(fs, sn, err)))
                return FAT_INVALID_CLUSTER;

            while(i < last) {
                off = cl & (fs->sb.bytes_per_sector - 1);
                val = blk[off] | (blk[off + 1] << 8);

                /* Did we find a free cluster? */
                if(val == 0) {
                    /* Put an end of chain marker in to allocate it. */
                    blk[off] = 0xFF;
                    blk[off + 1] = 0xFF;

                    /* Mark the block as dirty so it can get flushed to the
                       backing store at some point. */
                    fat_fatblock_mark_dirty(fs, sn);

                    fs->sb.last_alloc_cluster = i;
		    fs->flags |= FAT_FS_FLAG_SB_DIRTY;
                    return i;
                }

                ++i;
                cl += 2;

                /* Do we have to read a new block? */
                if(!(i & cps) && i < fs->sb.num_clusters + 2) {
                    ++sn;

                    if(!(blk = fat_read_fatblock(fs, sn, err)))
                        return FAT_INVALID_CLUSTER;
                }
            }

            /* If we get here, then restart the search and try one more time.
               If we get here a second time, then there really aren't any
               clusters left. */
            if(tries--) {
                i = 2;
                last = fs->sb.last_alloc_cluster;
                goto retry_fat16;
            }

            *err = ENOSPC;
            return FAT_INVALID_CLUSTER;

        case FAT_FS_FAT12:
            /* Do this twice, so the search can loop around. */
            for(i = fs->sb.last_alloc_cluster + 1; i < fs->sb.num_clusters + 2;
                ++i) {
                if(!(cl = fat_read_fat(fs, i, err))) {
                    /* Allocate it by adding in an end of chain marker. */
                    if((*err = fat_write_fat(fs, i, 0x0FFF)) < 0)
                        return FAT_INVALID_CLUSTER;

                    fs->sb.last_alloc_cluster = i;
		    fs->flags |= FAT_FS_FLAG_SB_DIRTY;
                }
                else if(cl == FAT_INVALID_CLUSTER) {
                    return cl;
                }
            }

            for(i = 2; i < fs->sb.last_alloc_cluster + 1; ++i) {
                if(!(cl = fat_read_fat(fs, i, err))) {
                    /* Allocate it by adding in an end of chain marker. */
                    if((*err = fat_write_fat(fs, i, 0x0FFF)) < 0)
                        return FAT_INVALID_CLUSTER;

                    fs->sb.last_alloc_cluster = i;
		    fs->flags |= FAT_FS_FLAG_SB_DIRTY;
                }
                else if(cl == FAT_INVALID_CLUSTER) {
                    return cl;
                }
            }

            /* If we get here, there really wasn't anything left. */
            *err = ENOSPC;
            return FAT_INVALID_CLUSTER;

        default:
            *err = EBADF;
            return FAT_INVALID_CLUSTER;
    }

    return val;
}

/* This function could be made better/more optimized... However, it takes the
   simplest/most clear approach to this for now. */
int fat_erase_chain(fat_fs_t *fs, uint32_t cluster) {
    uint32_t next;
    int err = 0;

    /* Don't let us write to the FAT if we're on a read-only FS. */
    if(!(fs->mnt_flags & FAT_MNT_FLAG_RW)) {
        return -EROFS;
    }

    while(!fat_is_eof(fs, cluster)) {
        next = fat_read_fat(fs, cluster, &err);
        if(next == FAT_INVALID_CLUSTER) {
            dbglog(DBG_WARNING, "Error reading FAT while erasing chain at "
                   "cluster %" PRIu32 ": %s\n", cluster, strerror(err));
            return -err;
        }

        if((err = fat_write_fat(fs, cluster, FAT_FREE_CLUSTER))) {
            dbglog(DBG_WARNING, "Error writing to FAT while erasing chain at "
                   "cluster %" PRIu32 ": %s\n", cluster, strerror(-err));
        }

        cluster = next;
        ++fs->sb.free_clusters;
	fs->flags |= FAT_FS_FLAG_SB_DIRTY;
    }

    return 0;
}
