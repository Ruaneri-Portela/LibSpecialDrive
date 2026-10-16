#ifdef _WIN32
#include <LibSpecialDrive.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winioctl.h>

static int ExtractDiskNumber(const char *path) {
  if (!path)
    return -1;

  const char *prefix = "\\\\.\\PhysicalDrive";
  const char *pos = strstr(path, prefix);
  if (!pos)
    return -1;

  pos += strlen(prefix);
  return (*pos) ? atoi(pos) : -1;
}

static bool IsMatchingExtent(const DISK_EXTENT *ext, DWORD diskNumber,
                             LONGLONG lbaStart, DWORD lbaSize) {
  if (!ext || lbaSize == 0)
    return false;

  return (ext->DiskNumber == diskNumber) &&
         (ext->StartingOffset.QuadPart == (lbaStart * (LONGLONG)lbaSize));
}

void LibSpecialDriveDiretoryFreeSpaceLookup(LibSpecialDrive_Partition *part) {
  if (!part)
    return;

  part->freeSpace = 0;
  if (!part->path || part->path[0] == '\0')
    return;

  ULARGE_INTEGER freeBytesAvailable = {0};
  ULARGE_INTEGER totalBytes = {0};
  ULARGE_INTEGER totalFreeBytes = {0};

  if (GetDiskFreeSpaceExA(part->path, &freeBytesAvailable, &totalBytes,
                          &totalFreeBytes)) {
    part->freeSpace = freeBytesAvailable.QuadPart;
  }
}

char *LibSpecialDrivePartitionPathLookup(const char *path,
                                         int partitionNumber) {
  (void)partitionNumber;
  return path ? _strdup(path) : NULL;
}

void LibSpecialDrivePartitionGetPathMount(
    LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type) {
  if (!part || !part->path || !part->lbaSize || *part->lbaSize == 0)
    return;

  int diskNumber = ExtractDiskNumber(part->path);
  free(part->path);
  part->path = NULL;

  if (diskNumber < 0)
    return;

  char volumeName[MAX_PATH] = {0};
  HANDLE hVol = FindFirstVolumeA(volumeName, MAX_PATH);
  if (hVol == INVALID_HANDLE_VALUE)
    return;

  LONGLONG lbaStart = (type == PARTITION_TYPE_GPT)
                          ? (LONGLONG)part->partitionMeta.gpt.startingLba
                          : (LONGLONG)part->partitionMeta.mbr.firstLBA;

  do {
    // Garante que o nome do volume termina com barra invertida para abertura
    // correta do volume
    size_t len = strlen(volumeName);
    if (len > 0 && volumeName[len - 1] != '\\') {
      if (len + 1 < MAX_PATH) {
        volumeName[len] = '\\';
        volumeName[len + 1] = '\0';
      }
    }

    // Abre manipulador para o volume tirando a barra no final
    char deviceVolumePath[MAX_PATH] = {0};
    strncpy_s(deviceVolumePath, MAX_PATH, volumeName, _TRUNCATE);
    len = strlen(deviceVolumePath);
    if (len > 0 && deviceVolumePath[len - 1] == '\\') {
      deviceVolumePath[len - 1] = '\0';
    }

    HANDLE hVolume =
        CreateFileA(deviceVolumePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (hVolume == INVALID_HANDLE_VALUE)
      continue;

    // Suporte dinamico a multiplos extents caso o volume esteja fragmentado
    BYTE extBuffer[1024] = {0};
    PVOLUME_DISK_EXTENTS pExtents = (PVOLUME_DISK_EXTENTS)extBuffer;
    DWORD bytesReturned = 0;

    if (DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
                        pExtents, sizeof(extBuffer), &bytesReturned, NULL)) {
      for (DWORD i = 0; i < pExtents->NumberOfDiskExtents; ++i) {
        if (IsMatchingExtent(&pExtents->Extents[i], (DWORD)diskNumber, lbaStart,
                             *part->lbaSize)) {
          part->path = _strdup(volumeName);

          // Aloca dinamicamente o buffer de caminhos de montagem para evitar
          // estouro
          DWORD charCount = 0;
          GetVolumePathNamesForVolumeNameA(volumeName, NULL, 0, &charCount);
          if (charCount > 0) {
            char *mountPaths = (char *)calloc(charCount, sizeof(char));
            if (mountPaths) {
              if (GetVolumePathNamesForVolumeNameA(volumeName, mountPaths,
                                                   charCount, &charCount)) {
                if (mountPaths[0] != '\0') {
                  part->mountPoint = _strdup(mountPaths);
                }
              }
              free(mountPaths);
            }
          }

          CloseHandle(hVolume);
          FindVolumeClose(hVol);
          return;
        }
      }
    }

    CloseHandle(hVolume);
  } while (FindNextVolumeA(hVol, volumeName, MAX_PATH));

  FindVolumeClose(hVol);
}

bool LibSpecialDriveLookUpSizes(LibSpecialDrive_DeviceHandle device,
                                LibSpecialDrive_BlockDevice *blk) {
  if (device == INVALID_HANDLE_VALUE || !blk)
    return false;

  GET_LENGTH_INFORMATION lenInfo = {0};
  DISK_GEOMETRY geometry = {0};
  DWORD bytesReturned = 0;

  if (!DeviceIoControl(device, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lenInfo,
                       sizeof(lenInfo), &bytesReturned, NULL))
    return false;

  if (!DeviceIoControl(device, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
                       &geometry, sizeof(geometry), &bytesReturned, NULL))
    return false;

  blk->size = (uint64_t)lenInfo.Length.QuadPart;
  blk->lbaSize = geometry.BytesPerSector;
  return true;
}

bool LibSpecialDriveLookUpIsRemovable(LibSpecialDrive_DeviceHandle device,
                                      LibSpecialDrive_BlockDevice *blk) {
  if (device == INVALID_HANDLE_VALUE || !blk)
    return false;

  STORAGE_PROPERTY_QUERY query = {
      StorageDeviceProperty, PropertyStandardQuery, {0}};
  BYTE buffer[1024] = {0};
  DWORD bytesReturned = 0;

  if (DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query,
                      sizeof(query), buffer, sizeof(buffer), &bytesReturned,
                      NULL)) {
    STORAGE_DEVICE_DESCRIPTOR *desc = (STORAGE_DEVICE_DESCRIPTOR *)buffer;
    if (desc->RemovableMedia) {
      blk->flags |= BLOCK_FLAG_IS_REMOVABLE;
      return true;
    }
  }

  return false;
}

LibSpecialDrive *LibSpecialDriveGet(void) {
  LibSpecialDrive *driver =
      (LibSpecialDrive *)calloc(1, sizeof(LibSpecialDrive));
  if (!driver)
    return NULL;

  char path[MAX_PATH];
  int failed = 0;

  for (DWORD i = 0;; ++i) {
    snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%lu", i);
    LibSpecialDrive_BlockDevice *blk = LibSpecialDriveGetBlock(path);

    if (!blk) {
      if (++failed > 8)
        break;
      continue;
    }

    failed = 0;
    LibSpecialDriveBlockAppend(driver, &blk);
  }

  return driver;
}

LibSpecialDrive_DeviceHandle
LibSpecialDriveOpenDevice(const char *path,
                          enum LibSpecialDrive_DeviceHandle_Flags flags) {
  if (!path)
    return INVALID_HANDLE_VALUE;

  DWORD access = 0;
  if (flags & DEVICE_FLAG_READ)
    access |= GENERIC_READ;
  if (flags & DEVICE_FLAG_WRITE)
    access |= GENERIC_WRITE;

  HANDLE hDevice = CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
  if (hDevice == INVALID_HANDLE_VALUE) {
    if (!(flags & DEVICE_FLAG_SILENCE)) {
      fprintf(stderr, "Failed to open device %s (Error: %lu)\n", path,
              GetLastError());
    }
  }

  return hDevice;
}

bool LibSpecialDriveSeek(LibSpecialDrive_DeviceHandle device, int64_t offset) {
  if (device == INVALID_HANDLE_VALUE)
    return false;

  LARGE_INTEGER li;
  li.QuadPart = offset;

  // Correção: Uso de SetFilePointerEx garante suporte nativo a offsets de
  // 64-bits
  if (!SetFilePointerEx(device, li, NULL, FILE_BEGIN)) {
    fprintf(stderr, "Seek failed to offset %lld (Error: %lu)\n", offset,
            GetLastError());
    return false;
  }

  return true;
}

int64_t LibSpecialDriveRead(LibSpecialDrive_DeviceHandle device, int64_t len,
                            uint8_t *target) {
  if (device == INVALID_HANDLE_VALUE || !target || len < 0)
    return -1;

  DWORD bytesRead = 0;
  if (!ReadFile(device, target, (DWORD)len, &bytesRead, NULL))
    return -1;

  return (int64_t)bytesRead;
}

int64_t LibSpecialDriveWrite(LibSpecialDrive_DeviceHandle device, int64_t len,
                             const uint8_t *source) {
  if (device == INVALID_HANDLE_VALUE || !source || len < 0)
    return -1;

  DWORD bytesWritten = 0;
  if (!WriteFile(device, source, (DWORD)len, &bytesWritten, NULL))
    return -1;

  return (int64_t)bytesWritten;
}

void LibSpecialDriveCloseDevice(LibSpecialDrive_DeviceHandle device) {
  if (device != INVALID_HANDLE_VALUE && device != NULL) {
    if (!CloseHandle(device)) {
      fprintf(stderr, "CloseHandle failed (Error: %lu)\n", GetLastError());
    }
  }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
  (void)hinstDLL;
  (void)lpvReserved;

  switch (fdwReason) {
  case DLL_PROCESS_ATTACH:
    DisableThreadLibraryCalls(hinstDLL);
    break;
  case DLL_PROCESS_DETACH:
    break;
  }
  return TRUE;
}
#else
#define LIBSPECIALDRIVEMWIN_C_EMPTY
void LibSpecialDriveWIN_dummy(void) {}
#endif