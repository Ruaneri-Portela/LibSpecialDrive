#ifdef __linux__

#include <LibSpecialDrive.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fs.h>
#include <mntent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

void LibSpecialDriveDiretoryFreeSpaceLookup(LibSpecialDrive_Partition *part) {
  if (!part || !part->mountPoint)
    return;

  struct statvfs stat;
  if (statvfs(part->mountPoint, &stat) != 0) {
    part->freeSpace = 0;
    return;
  }

  uint64_t block_size = (uint64_t)stat.f_frsize;
  part->freeSpace = (uint64_t)stat.f_bavail * block_size;
}

char *LibSpecialDrivePartitionPathLookup(const char *path,
                                         int partitionNumber) {
  if (!path || partitionNumber < 0)
    return NULL;

  char partitionPath[PATH_MAX];
  int targetPartIdx = partitionNumber + 1;

  // Tenta formato padrão /dev/sdX1
  snprintf(partitionPath, sizeof(partitionPath), "%s%d", path, targetPartIdx);
  int fd = open(partitionPath, O_RDONLY);
  if (fd >= 0) {
    close(fd);
    return strdup(partitionPath);
  }

  // Tenta formato de partição indexada /dev/nvme0n1p1 ou /dev/mmcblk0p1
  snprintf(partitionPath, sizeof(partitionPath), "%sp%d", path, targetPartIdx);
  fd = open(partitionPath, O_RDONLY);
  if (fd >= 0) {
    close(fd);
    return strdup(partitionPath);
  }

  return NULL;
}

void LibSpecialDrivePartitionGetPathMount(
    LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type) {
  (void)type;
  if (!part || !part->path)
    return;

  FILE *fp = setmntent("/etc/mtab", "r");
  if (!fp) {
    fp = setmntent("/proc/mounts", "r");
    if (!fp)
      return;
  }

  struct mntent *mnt;
  while ((mnt = getmntent(fp)) != NULL) {
    if (strcmp(mnt->mnt_fsname, part->path) == 0) {
      part->mountPoint = strdup(mnt->mnt_dir);
      break;
    }
  }

  endmntent(fp);
}

bool LibSpecialDriveLookUpSizes(LibSpecialDrive_DeviceHandle device,
                                LibSpecialDrive_BlockDevice *blk) {
  if (device < 0 || !blk)
    return false;

  uint32_t sector_size = 0;
  uint64_t total_bytes = 0;

  if (ioctl(device, BLKSSZGET, &sector_size) < 0) {
    perror("ioctl(BLKSSZGET)");
    return false;
  }

  if (ioctl(device, BLKGETSIZE64, &total_bytes) < 0) {
    perror("ioctl(BLKGETSIZE64)");
    return false;
  }

  blk->lbaSize = sector_size;
  blk->size = total_bytes;
  return true;
}

// Checagem real via interface /sys/block/<dev>/removable
bool LibSpecialDriveLookUpIsRemovable(LibSpecialDrive_DeviceHandle device,
                                      LibSpecialDrive_BlockDevice *blk) {
  (void)device;
  if (!blk || !blk->path)
    return false;

  const char *dev_name = strrchr(blk->path, '/');
  if (!dev_name)
    dev_name = blk->path;
  else
    dev_name++;

  char sys_path[PATH_MAX];
  snprintf(sys_path, sizeof(sys_path), "/sys/block/%s/removable", dev_name);

  FILE *fp = fopen(sys_path, "r");
  if (!fp)
    return false;

  int removable = 0;
  if (fscanf(fp, "%d", &removable) == 1 && removable == 1) {
    blk->flags |= BLOCK_FLAG_IS_REMOVABLE;
    fclose(fp);
    return true;
  }

  fclose(fp);
  return false;
}

LibSpecialDrive *LibSpecialDriveGet(void) {
  LibSpecialDrive *ctx = calloc(1, sizeof(LibSpecialDrive));
  if (!ctx)
    return NULL;

  FILE *fp = fopen("/proc/partitions", "r");
  if (!fp) {
    perror("fopen(/proc/partitions)");
    free(ctx);
    return NULL;
  }

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    unsigned int major, minor;
    unsigned long long blocks;
    char name[128];

    if (sscanf(line, " %u %u %llu %127s", &major, &minor, &blocks, name) != 4)
      continue;

    if (strchr(name, '/'))
      continue;

    // Ignora partições /dev/sdXN (ex: sda1) mantendo nvme0n1 e mmcblk0
    size_t name_len = strlen(name);
    if (name_len > 0 && isdigit(name[name_len - 1])) {
      // Trata nomenclatura NVMe/MMC: ignora se contiver sufixo de partição
      // "p<número>"
      if (strstr(name, "nvme") || strstr(name, "mmcblk")) {
        if (strrchr(name, 'p') && isdigit(*(strrchr(name, 'p') + 1)))
          continue;
      } else {
        // Dispositivos IDE/SATA tradicionais (sda1, sdb2)
        continue;
      }
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/dev/%s", name);

    LibSpecialDrive_BlockDevice *blk = LibSpecialDriveGetBlock(path);
    if (!blk)
      continue;

    LibSpecialDriveBlockAppend(ctx, &blk);
  }

  fclose(fp);
  return ctx;
}

LibSpecialDrive_DeviceHandle
LibSpecialDriveOpenDevice(const char *path,
                          enum LibSpecialDrive_DeviceHandle_Flags flags) {
  if (!path) {
    errno = EINVAL;
    return -1;
  }

  int accessMode = 0;
  if ((flags & DEVICE_FLAG_READ) && (flags & DEVICE_FLAG_WRITE))
    accessMode = O_RDWR;
  else if (flags & DEVICE_FLAG_READ)
    accessMode = O_RDONLY;
  else if (flags & DEVICE_FLAG_WRITE)
    accessMode = O_WRONLY;
  else {
    errno = EINVAL;
    return -1;
  }

  int fd = open(path, accessMode);
  if (fd < 0 && !(flags & DEVICE_FLAG_SILENCE))
    perror("open");

  return fd;
}

bool LibSpecialDriveSeek(LibSpecialDrive_DeviceHandle device, int64_t offset) {
  if (device < 0)
    return false;

  off_t result = lseek(device, (off_t)offset, SEEK_SET);
  if (result == (off_t)-1) {
    perror("lseek");
    return false;
  }
  return true;
}

int64_t LibSpecialDriveRead(LibSpecialDrive_DeviceHandle device, int64_t len,
                            uint8_t *target) {
  if (device < 0 || !target || len < 0)
    return -1;

  ssize_t bytesRead = read(device, target, (size_t)len);
  if (bytesRead < 0) {
    perror("read");
    return -1;
  }
  return (int64_t)bytesRead;
}

int64_t LibSpecialDriveWrite(LibSpecialDrive_DeviceHandle device, int64_t len,
                             const uint8_t *source) {
  if (device < 0 || !source || len < 0)
    return -1;

  ssize_t bytesWritten = write(device, source, (size_t)len);
  if (bytesWritten < 0) {
    perror("write");
    return -1;
  }
  return (int64_t)bytesWritten;
}

void LibSpecialDriveCloseDevice(LibSpecialDrive_DeviceHandle device) {
  if (device >= 0)
    close(device);
}

#else
#define LIBSPECIALDRIVEMLINUX_C_EMPTY
void LibSpecialDriveLINUX_dummy(void) {}
#endif // __linux__