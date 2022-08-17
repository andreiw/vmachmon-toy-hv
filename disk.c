#define LOG_PFX DISK
#include "disk.h"
#include "list.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

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
  if (index == 0) {
    part->off = 0;
    part->length = disk->st.st_size;
    return ERR_NONE;
  }

  return ERR_NOT_FOUND;
}
