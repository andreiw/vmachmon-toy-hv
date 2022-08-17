#pragma once
#include "pvp.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

typedef struct disk_s disk_t;

typedef struct disk_part_s {
  offset_t off;
  length_t length;
} disk_part_t;

disk_t *disk_open(const char *disk_path);
void disk_close(disk_t *disk);
void disk_bye();
err_t disk_seek(disk_t *d, offset_t offset);
length_t disk_out(disk_t *d, const uint8_t *buf, length_t len);
length_t disk_in(disk_t *d, uint8_t *buf, length_t expected);
err_t disk_find_part(disk_t *disk, unsigned index,
                     disk_part_t *part);
