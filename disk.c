/*
 * Disk partition access code.
 *
 * Copyright (C) 2013 Andrei Warkentin <andrey.warkentin@gmail.com>
 * (C) Copyright 2004
 * esd gmbh <www.esd-electronics.com>
 * Reinhard Arlt <reinhard.arlt@esd-electronics.com>
 *
 * based on code of fs/reiserfs/dev.c by
 *
 * (C) Copyright 2003 - 2004
 * Sysgo AG, <www.elinos.com>, Pavel Bartusek <pba@sysgo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define LOG_PFX DISK
#include "disk.h"
#include "list.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define SECTOR_SIZE 512

typedef struct {
   uint8_t  boot_ind;   /* 0x80 - active */
   uint8_t  head;       /* starting head */
   uint8_t  sector;     /* starting sector */
   uint8_t  cyl;        /* starting cylinder */
   uint8_t  sys_ind;    /* What partition type */
   uint8_t  end_head;   /* end head */
   uint8_t  end_sector; /* end sector */
   uint8_t  end_cyl;    /* end cylinder */
   uint32_t start_sect; /* starting sector counting from 0 */
   uint32_t nr_sects;   /* nr of sectors in partition */
} dos_part_t;

struct disk_s {
  struct list_head link;
  char *path;
  int fd;
  struct stat st;
};

LIST_HEAD(disks);

void
disk_close(disk_t *disk)
{
  /* Nothing. */
}

disk_t *
disk_open(const char *disk_path)
{
  int ret;
  disk_t *disk;

  list_for_each_entry(disk, &disks, link) {
    if (!strcmp(disk_path, disk->path)) {
      return disk;
    }
  }

  disk = malloc(sizeof(*disk));
  if (disk == NULL) {
    POSIX_ERROR(errno, "could not alloc disk '%s'", disk_path);
    goto posix_err;
  }
  memset(disk, 0, sizeof(*disk));

  disk->path = strdup(disk_path);
  if (disk->path == NULL) {
    POSIX_ERROR(errno, "could not alloc for disk '%s'", disk_path);
    goto posix_err;
  }

  ret = open(disk_path, O_RDWR);
  if (ret < 0) {
    POSIX_ERROR(errno, "could not open disk '%s'", disk_path);
    goto posix_err;
  }

  INIT_LIST_HEAD(&disk->link);
  disk->fd = ret;
  ret = fstat(disk->fd, &disk->st);
  ON_POSIX_ERROR("disk stat", ret, posix_err);
  list_add_tail(&disk->link, &disks);

  return disk;
 posix_err:
  if (disk != NULL) {
    if (disk->path != NULL) {
      free(disk->path);
    }
    free(disk);
  }
  return NULL;
}

void
disk_bye(void)
{
  disk_t *disk;
  disk_t *n;

  list_for_each_entry_safe(disk, n, &disks, link) {
    close(disk->fd);
    list_del(&disk->link);
    free(disk);
  }
}

length_t
disk_in(disk_t *disk,
        uint8_t *buf,
        length_t expected)
{
  int ret;

  ret = read(disk->fd, buf, expected);
  if (ret < 0) {
    ret = 0;
  }

  return ret;
}

length_t
disk_out(disk_t *disk,
         const uint8_t *buf,
         length_t len)
{
  int ret;

  ret = write(disk->fd, buf, len);
  if (ret < 0) {
    ret = 0;
  }

  return ret;
}

err_t
disk_seek(disk_t *disk,
          offset_t offset)
{
  int ret;

  ret = lseek(disk->fd, offset, SEEK_SET);
  if (ret < 0) {
    return ERR_POSIX;
  }

  return ERR_NONE;
}

err_t
disk_find_part(disk_t *disk,
               unsigned index,
               disk_part_t *part)
{
  uint8_t blk[SECTOR_SIZE];
  dos_part_t *d;
  length_t len;
  unsigned i;
  err_t err;

  if (index == 0) {
    part->off = 0;
    part->length = disk->st.st_size;
    return ERR_NONE;
  }

  err = disk_seek(disk, 0);
  if (err != ERR_NONE) {
    return err;
  }

  len = disk_in(disk, blk, SECTOR_SIZE);
  if (len != SECTOR_SIZE) {
    return ERR_IO_ERROR;
  }

  /* check the MSDOS partition magic */
  if ((blk[0x1fe] != 0x55) || (blk[0x1ff] != 0xaa)) {
    return ERR_NOT_FOUND;
  }

  if (index >= 4) {
    return ERR_NOT_FOUND;
  }

  d = (dos_part_t *) &blk[0x1be];
  for (i = 1; i != index; i++, d++);

  part->off = le32_to_cpu(d->start_sect) * SECTOR_SIZE;
  part->length = le32_to_cpu(d->nr_sects) * SECTOR_SIZE;

  return ERR_NONE;
}
