#ifdef _WIN32
#include <LibSpecialDrive.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <winioctl.h>

static int ExtractDiskNumber(const char *path)
{
    const char *prefix = "\\\\.\\PhysicalDrive";
    const char *pos = strstr(path, prefix);
    if (!pos)
        return -1;
    pos += strlen(prefix);
    return (*pos) ? atoi(pos) : -1;
}

static bool IsMatchingExtent(const DISK_EXTENT *ext, DWORD diskNumber, LONGLONG lbaStart, DWORD lbaSize)
{
    return ext->DiskNumber == diskNumber &&
           ext->StartingOffset.QuadPart == lbaStart * lbaSize;
}

char *LibSpecialDriverPartitionPathLookup(const char *path, int partitionNumber)
{
    (void)partitionNumber;
    return strdup(path);
}

void LibSpecialDrivePartitionGetPathMount(LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type)
{
    if (!part || !part->path)
        return;

    int diskNumber = ExtractDiskNumber(part->path);
    free(part->path);

    if (diskNumber < 0)
        return;

    part->path = NULL;

    CHAR volumeName[MAX_PATH];
    HANDLE hVol = FindFirstVolumeA(volumeName, MAX_PATH);
    if (hVol == INVALID_HANDLE_VALUE)
        return;

    do
    {
        size_t len = strlen(volumeName);
        if (len > 0 && volumeName[len - 1] == '\\')
            volumeName[len - 1] = '\0';

        HANDLE hVolume = CreateFileA(volumeName, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hVolume == INVALID_HANDLE_VALUE)
            continue;

        VOLUME_DISK_EXTENTS extents;
        DWORD bytesReturned;
        if (DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                            NULL, 0, &extents, sizeof(extents), &bytesReturned, NULL))
        {

            LONGLONG lbaStart = (LONGLONG)((type == PARTITION_TYPE_GPT)
                                               ? part->partitionMeta.gpt.startingLba
                                               : part->partitionMeta.mbr.lbaStart);

            for (DWORD i = 0; i < extents.NumberOfDiskExtents; ++i)
            {
                if (IsMatchingExtent(&extents.Extents[i], (DWORD)diskNumber, lbaStart, *part->lbaSize))
                {
                    char mountPaths[MAX_PATH] = {0};
                    DWORD pathLen = 0;

                    part->path = strdup(volumeName); // Retém o nome da volume

                    if (GetVolumePathNamesForVolumeNameA(volumeName, mountPaths, MAX_PATH, &pathLen) && pathLen > 0)
                    {
                        if (strlen(mountPaths) >= 2)
                            part->mountPoint = strdup(mountPaths);
                        CloseHandle(hVolume);
                        FindVolumeClose(hVol);
                        return;
                    }
                }
            }
        }

        CloseHandle(hVolume);
    } while (FindNextVolumeA(hVol, volumeName, MAX_PATH));

    FindVolumeClose(hVol);
}

bool LibSpecialDriveLookUpSizes(LibSpecialDrive_DeviceHandle device, LibSpecialDrive_BlockDevice *blk)
{
    GET_LENGTH_INFORMATION lenInfo;
    DISK_GEOMETRY geometry;

    if (!DeviceIoControl(device, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                         &lenInfo, sizeof(lenInfo), NULL, NULL))
        return false;

    if (!DeviceIoControl(device, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
                         &geometry, sizeof(geometry), NULL, NULL))
        return false;

    blk->size = (uint64_t)lenInfo.Length.QuadPart;
    blk->lbaSize = geometry.BytesPerSector;
    return true;
}

bool LibSpecialDriveLookUpIsRemovable(LibSpecialDrive_DeviceHandle device, LibSpecialDrive_BlockDevice *blk)
{
    STORAGE_PROPERTY_QUERY query = {StorageDeviceProperty, PropertyStandardQuery, {0}};
    BYTE buffer[1024] = {0};

    if (DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                        buffer, sizeof(buffer), NULL, NULL))
    {

        STORAGE_DEVICE_DESCRIPTOR *desc = (STORAGE_DEVICE_DESCRIPTOR *)buffer;
        if (desc->RemovableMedia)
        {
            blk->flags |= BLOCK_FLAG_IS_REMOVABLE;
            return true;
        }
    }

    return false;
}

LibSpecialDrive *LibSpecialDriverGet(void)
{
    LibSpecialDrive *driver = calloc(1, sizeof(LibSpecialDrive));
    if (!driver)
        return NULL;

    char path[MAX_PATH];
    int failed = 0;

    for (DWORD i = 0;; ++i)
    {
        snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%lu", i);
        LibSpecialDrive_BlockDevice *blk = LibSpecialDriverGetBlock(path);

        if (!blk)
        {
            if (++failed > 8)
                break;
            continue;
        }

        failed = 0;
        LibSpecialDriverBlockAppend(driver, &blk);
    }

    return driver;
}

LibSpecialDrive_DeviceHandle LibSpecialDriveOpenDevice(const char *path, enum LibSpecialDrive_DeviceHandle_Flags flags)
{
    if (!path)
        return INVALID_HANDLE_VALUE;

    DWORD access = 0;
    if (flags & DEVICE_FLAG_READ)
        access |= GENERIC_READ;
    if (flags & DEVICE_FLAG_WRITE)
        access |= GENERIC_WRITE;

    HANDLE hDevice = CreateFileA(path, access, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Failed to open device %s (Error: %lu)\n", path, GetLastError());
    }

    return hDevice;
}

bool LibSpecialDriveSeek(LibSpecialDrive_DeviceHandle device, int64_t offset)
{
    LARGE_INTEGER li;
    li.QuadPart = offset;

    DWORD result = SetFilePointer(device, li.HighPart, NULL, FILE_BEGIN);
    if (result == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
    {
        fprintf(stderr, "Seek failed to offset %lld (Error: %lu)\n", offset, GetLastError());
        return false;
    }

    return true;
}

int64_t LibSpecialDriveRead(LibSpecialDrive_DeviceHandle device, int64_t len, uint8_t *target)
{
    DWORD bytesRead = 0;
    if (!ReadFile(device, target, (DWORD)len, &bytesRead, NULL))
        return -1;
    return bytesRead;
}

int64_t LibSpecialDriveWrite(LibSpecialDrive_DeviceHandle device, int64_t len, const uint8_t *source)
{
    DWORD bytesWritten = 0;
    if (!WriteFile(device, source, (DWORD)len, &bytesWritten, NULL))
        return -1;
    return bytesWritten;
}

void LibSpecialDriveCloseDevice(LibSpecialDrive_DeviceHandle device)
{
    if (!CloseHandle(device))
    {
        fprintf(stderr, "CloseHandle failed (Error: %lu)\n", GetLastError());
    }
}
#else
#define LIBSPECIALDRIVEMWIN_C_EMPTY
void LibSpecialDriveWIN_dummy(void) {}
#endif