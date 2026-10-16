#ifdef __APPLE__

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <LibSpecialDrive.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/mount.h>
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

  uint64_t block_size = stat.f_frsize;
  part->freeSpace = (uint64_t)stat.f_bavail * block_size;
}

// Cria caminho para uma partição: ex. "/dev/disk2" + 0 => "/dev/disk2s1"
char *LibSpecialDrivePartitionPathLookup(const char *path,
                                         int partitionNumber) {
  if (!path || partitionNumber < 0)
    return NULL;

  char partitionPath[PATH_MAX];
  snprintf(partitionPath, sizeof(partitionPath), "%ss%d", path,
           partitionNumber + 1);
  return strdup(partitionPath);
}

// Obtém o ponto de montagem da partição
void LibSpecialDrivePartitionGetPathMount(
    LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type) {
  (void)type;
  if (!part || !part->path)
    return;

  struct statfs *mounts = NULL;
  int count = getmntinfo(&mounts, MNT_NOWAIT);
  if (count <= 0)
    return;

  for (int i = 0; i < count; ++i) {
    if (strcmp(mounts[i].f_mntfromname, part->path) == 0) {
      part->mountPoint = strdup(mounts[i].f_mntonname);
      return;
    }
  }
}

// Obtém tamanho do dispositivo em bytes e tamanho do setor lógico
bool LibSpecialDriveLookUpSizes(LibSpecialDrive_DeviceHandle device,
                                LibSpecialDrive_BlockDevice *blk) {
  if (device < 0 || !blk)
    return false;

  uint64_t blockCount = 0;
  uint32_t sectorSize = 0;

  if (ioctl(device, DKIOCGETBLOCKSIZE, &sectorSize) < 0) {
    perror("ioctl(DKIOCGETBLOCKSIZE)");
    return false;
  }

  if (ioctl(device, DKIOCGETBLOCKCOUNT, &blockCount) < 0) {
    perror("ioctl(DKIOCGETBLOCKCOUNT)");
    return false;
  }

  blk->lbaSize = sectorSize;
  blk->size = (uint64_t)sectorSize * blockCount;
  return true;
}

bool LibSpecialDriveLookUpIsRemovable(LibSpecialDrive_DeviceHandle device,
                                      LibSpecialDrive_BlockDevice *blk) {
  (void)device;
  (void)blk;
  return true;
}

// Desmontagem nativa via unmount(2) sem invocação de shell
static bool LibSpecialDriveUnmount(const char *path) {
  if (!path)
    return false;

  // Tenta unmount direto no sistema de arquivos
  if (unmount(path, MNT_FORCE) == 0)
    return true;

  return false;
}

// Abre dispositivo com permissões estipuladas
LibSpecialDrive_DeviceHandle
LibSpecialDriveOpenDevice(const char *path,
                          enum LibSpecialDrive_DeviceHandle_Flags flags) {
  if (!path) {
    errno = EINVAL;
    return -1;
  }

  int accessMode = 0;
  if ((flags & DEVICE_FLAG_READ) && (flags & DEVICE_FLAG_WRITE)) {
    LibSpecialDriveUnmount(path);
    accessMode = O_RDWR;
  } else if (flags & DEVICE_FLAG_READ) {
    accessMode = O_RDONLY;
  } else if (flags & DEVICE_FLAG_WRITE) {
    accessMode = O_WRONLY;
  } else {
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

  if (lseek(device, (off_t)offset, SEEK_SET) == (off_t)-1) {
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

// Varre a árvore de IOMedia do macOS via IOKit
LibSpecialDrive *LibSpecialDriveGet(void) {
  CFMutableDictionaryRef matchingDict = IOServiceMatching(kIOMediaClass);
  if (!matchingDict) {
    fprintf(stderr, "IOServiceMatching failed\n");
    return NULL;
  }

  CFDictionarySetValue(matchingDict, CFSTR(kIOMediaWholeKey), kCFBooleanTrue);

  io_iterator_t iterator = IO_OBJECT_NULL;

  // Compatibilidade com macOS Monterey (12.0+) e versoes anteriores
  mach_port_t mainPort = 0;
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 120000
  mainPort = kIOMainPortDefault;
#endif

  if (IOServiceGetMatchingServices(mainPort, matchingDict, &iterator) !=
      KERN_SUCCESS) {
    fprintf(stderr, "IOServiceGetMatchingServices failed\n");
    return NULL;
  }

  LibSpecialDrive *ctx = NULL;
  io_object_t media = IO_OBJECT_NULL;

  while ((media = IOIteratorNext(iterator))) {
    CFStringRef bsdName = (CFStringRef)IORegistryEntryCreateCFProperty(
        media, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
    if (bsdName) {
      char name[PATH_MAX] = {0};
      char path[PATH_MAX] = {0};

      if (CFStringGetCString(bsdName, name, sizeof(name),
                             kCFStringEncodingUTF8)) {
        snprintf(path, sizeof(path), "/dev/%s", name);

        LibSpecialDrive_BlockDevice *blk = LibSpecialDriveGetBlock(path);
        if (blk) {
          if (!ctx)
            ctx = (LibSpecialDrive *)calloc(1, sizeof(LibSpecialDrive));

          CFBooleanRef removable =
              (CFBooleanRef)IORegistryEntryCreateCFProperty(
                  media, CFSTR(kIOMediaRemovableKey), kCFAllocatorDefault, 0);
          if (removable) {
            if (CFBooleanGetValue(removable)) {
              blk->flags |= BLOCK_FLAG_IS_REMOVABLE;
            }
            CFRelease(removable); // Liberado independentemente do valor
          }

          LibSpecialDriveBlockAppend(ctx, &blk);
        }
      }
      CFRelease(bsdName);
    }
    IOObjectRelease(media);
  }

  IOObjectRelease(iterator);
  return ctx;
}

#else
#define LIBSPECIALDRIVEMAC_C_EMPTY
void LibSpecialDriveMAC_dummy(void) {}
#endif // __APPLE__