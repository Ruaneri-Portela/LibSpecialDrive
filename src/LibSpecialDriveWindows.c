#ifdef _WIN32
#include <LibSpecialDrive.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <windows.h>
#include <winioctl.h>

static int LibSpecialDriveExtractDiskNumber(const char *path)
{
    const char *prefix = "\\\\.\\PhysicalDrive";
    const char *pos = strstr(path, prefix);
    if (!pos)
        return -1;

    pos += strlen(prefix);
    return (*pos) ? atoi(pos) : -1;
}

char *LibSpecialDriverPartitionPathLookup(const char *path, int partitionNumber)
{
    return path
}

void LibSpecialDrivePartitionGetPath(struct LibSpeicalDrive_Partition *part)
{
    if (!part || !part->path)
        return;

    int diskNumber = LibSpecialDriveExtractDiskNumber(part->path);
    if (diskNumber < 0)
        return;

    part->path = NULL;

    char volumePath[MAX_PATH];
    HANDLE hVol = FindFirstVolumeA(volumePath, MAX_PATH);
    if (hVol == INVALID_HANDLE_VALUE)
        return;

    do
    {
        int volumeLen = strlen(volumePath);
        if (volumePath[volumeLen - 1] == '\\')
            volumePath[volumeLen - 1] = '\0';

        HANDLE hVolume = CreateFileA(volumePath, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hVolume == INVALID_HANDLE_VALUE)
            continue;

        if (volumePath[volumeLen - 1] == '\0')
            volumePath[volumeLen - 1] = '\\';

        VOLUME_DISK_EXTENTS extents;
        DWORD retBytes;
        if (DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
                            &extents, sizeof(extents), &retBytes, NULL))
        {

            for (DWORD i = 0; i < extents.NumberOfDiskExtents; i++)
            {
                DISK_EXTENT *ext = &extents.Extents[i];

                if (ext->DiskNumber == diskNumber &&
                    ext->StartingOffset.QuadPart == part->partitionMeta.starting_lba * SECTOR_SIZE)
                {

                    char mountPaths[MAX_PATH] = {0};
                    DWORD len = 0;
                    part->path = strdup(volumePath);
                    if (GetVolumePathNamesForVolumeNameA(volumePath, mountPaths, MAX_PATH, &len) && len > 0)
                    {
                        CloseHandle(hVolume);
                        FindVolumeClose(hVol);
                        size_t mountPathLen = strlen(mountPaths);
                        if (mountPathLen < 2)
                            return;
                        part->mountPoint = strdup(mountPaths);
                    }
                    return;
                }
            }
        }
        CloseHandle(hVolume);
    } while (FindNextVolumeA(hVol, volumePath, MAX_PATH));

    FindVolumeClose(hVol);
}

struct LibSpeicalDrive_Partition *LibSpecialDriverGetPartition(struct LibSpeicalDrive_BlockDevice *blk)
{
    if (!blk || !blk->path)
        return NULL;

    HANDLE hDevice = CreateFileA(blk->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
        return NULL;

    uint8_t buffer[SECTOR_SIZE];
    DWORD bytesRead;

    SetFilePointer(hDevice, GPT_HEADER_OFFSET, NULL, FILE_BEGIN);
    if (!ReadFile(hDevice, buffer, SECTOR_SIZE, &bytesRead, NULL))
    {
        CloseHandle(hDevice);
        return NULL;
    }

    LibSpeicalDrive_GPT_Header *header = (LibSpeicalDrive_GPT_Header *)buffer;
    if (bytesRead != SECTOR_SIZE || memcmp(&header->signature, GPT_SIGNATURE, 8) != 0)
    {
        CloseHandle(hDevice);
        return NULL;
    }

    size_t tableSize = header->num_partition_entries * header->size_of_partition_entry;
    uint8_t *partitionBuffer = malloc(tableSize);
    if (!partitionBuffer)
    {
        CloseHandle(hDevice);
        return NULL;
    }

    SetFilePointer(hDevice, header->partition_entries_lba * SECTOR_SIZE, NULL, FILE_BEGIN);
    if (!ReadFile(hDevice, partitionBuffer, tableSize, &bytesRead, NULL) || bytesRead != tableSize)
    {
        free(partitionBuffer);
        CloseHandle(hDevice);
        return NULL;
    }

    LibSpecialDriverMapperPartitions(header, partitionBuffer, blk);

    free(partitionBuffer);
    CloseHandle(hDevice);
    return blk->partitions;
}

struct LibSpeicalDrive_BlockDevice *LibSpecialDriverGetBlock(const char *path)
{
    if (!path)
        return NULL;

    ProtectiveMBR *MBR = malloc(sizeof(ProtectiveMBR));
    if (!MBR)
        return NULL;

    HANDLE hDevice = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
        goto error;

    DWORD bytesRead;
    if (!ReadFile(hDevice, MBR, SECTOR_SIZE, &bytesRead, NULL) || bytesRead != sizeof(ProtectiveMBR))
        goto error;

    GET_LENGTH_INFORMATION lenInfo;
    if (!DeviceIoControl(hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lenInfo, sizeof(lenInfo), &bytesRead, NULL))
        goto error;

    struct LibSpeicalDrive_BlockDevice *blk = calloc(1, sizeof(struct LibSpeicalDrive_BlockDevice));
    blk->size = lenInfo.Length.QuadPart;
    blk->path = strdup(path);
    blk->signature = MBR;

    STORAGE_PROPERTY_QUERY query = {StorageDeviceProperty, PropertyStandardQuery};
    BYTE buffer[1024] = {0};

    if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &bytesRead, NULL))
    {
        STORAGE_DEVICE_DESCRIPTOR *desc = (STORAGE_DEVICE_DESCRIPTOR *)buffer;
        if (desc->RemovableMedia)
            blk->flags |= BLOCK_FLAG_IS_REMOVABLE;
    }

    CloseHandle(hDevice);
    LibSpecialDriverGetPartition(blk);
    return blk;

error:
    if (hDevice != INVALID_HANDLE_VALUE)
        CloseHandle(hDevice);
    free(MBR);
    return NULL;
}

struct LibSpecialDrive *LibSpecialDriverGet(void)
{
    struct LibSpecialDrive *driver = calloc(1, sizeof(struct LibSpecialDrive));
    if (!driver)
        return NULL;

    char path[MAX_PATH];
    int failed = 0;
    for (size_t i = 0;; i++)
    {
        snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%zu", i);
        struct LibSpeicalDrive_BlockDevice *blk = LibSpecialDriverGetBlock(path);
        if (!blk)
        {
            if (failed++ > 8)
                break;
            continue;
        }
        failed = 0;
        LibSpecialDriverBlockAppend(driver, &blk);
    }

    return driver;
}

bool LibSpecialDriveMark(struct LibSpecialDrive *ctx, int blockNumber)
{
    if (!ctx || blockNumber < 0 || blockNumber >= ctx->specialBlockDeviceCount)
        return false;

    struct LibSpeicalDrive_BlockDevice *blk = &ctx->specialBlockDevices[blockNumber];

    struct LibSpecialFlag flag = {0xFF, LIBSPECIAL_MAGIC_STRING, {0}};
    uint8_t *uuid = LibSpecialDriverGenUUID();

    if (LibSpecialDriverIsSpecial(blk->signature))
        return false;

    memcpy(flag.uuid, uuid, 16);
    free(uuid);

    ProtectiveMBR *mbr = malloc(sizeof(ProtectiveMBR));
    if (!mbr)
        return false;
    memcpy(mbr, blk->signature, sizeof(ProtectiveMBR));
    memcpy(mbr->boot_code, &flag, sizeof(struct LibSpecialFlag));

    HANDLE hDevice = CreateFileA(blk->path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
        return false;

    DWORD bytesWritten;
    BOOL success = WriteFile(hDevice, mbr, sizeof(ProtectiveMBR), &bytesWritten, NULL);
    free(mbr);
    CloseHandle(hDevice);
    LibSpecialDriverReload(ctx);
    return (success && bytesWritten == sizeof(ProtectiveMBR));
}

bool LibSpecialDriveUnmark(struct LibSpecialDrive *ctx, int blockNumber)
{
    if (!ctx || blockNumber < 0 || blockNumber >= ctx->specialBlockDeviceCount)
        return false;

    struct LibSpeicalDrive_BlockDevice *blk = &ctx->specialBlockDevices[blockNumber];
    if (!blk->signature)
        return false;

    ProtectiveMBR *mbr = malloc(sizeof(ProtectiveMBR));
    if (!mbr)
        return false;

    HANDLE hDevice = CreateFileA(blk->path, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
        return false;
    DWORD bytesRead;
    if (!ReadFile(hDevice, mbr, sizeof(ProtectiveMBR), &bytesRead, NULL) || bytesRead != sizeof(ProtectiveMBR))
    {
        free(mbr);
        CloseHandle(hDevice);
        return false;
    }
    memset(mbr->boot_code, 0, sizeof(mbr->boot_code));
    DWORD bytesWritten;
    BOOL success = WriteFile(hDevice, mbr, sizeof(ProtectiveMBR), &bytesWritten, NULL);
    free(mbr);
    CloseHandle(hDevice);
    LibSpecialDriverReload(ctx);
    return (success && bytesWritten == sizeof(ProtectiveMBR));
}

#endif