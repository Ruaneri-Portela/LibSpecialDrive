#ifdef _WIN32
#include <LibSpecialDrive.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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
    return (char *)path;
}

void LibSpecialDrivePartitionGetPathMount(LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type)
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
                    ext->StartingOffset.QuadPart == (type == PARTITION_TYPE_GPT ? part->partitionMeta.gpt.startingLba : part->partitionMeta.mbr.lbaStart) * (*part->lbaSize))
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

LibSpecialDrive_Partition *LibSpecialDriverGetPartition(LibSpecialDrive_BlockDevice *blk, HANDLE hDevice)
{
    if (!blk || !blk->path || !hDevice || !blk->signature)
        return NULL;

    LibSpeicalDrive_GPT_Header *header = malloc(512);
    if (!header)
        return NULL;

    DWORD bytesRead;
    LARGE_INTEGER offset;

    offset.QuadPart = blk->signature->partitions->lbaStart * blk->lbaSize;
    if (SetFilePointer(hDevice, offset.LowPart, &offset.HighPart, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
    {
        free(header);
        return NULL;
    }

    if (!ReadFile(hDevice, header, 512, &bytesRead, NULL) ||
        bytesRead != 512)
    {
        free(header);
        return NULL;
    }

    if (memcmp(&header->signature, GPT_SIGNATURE, 8) != 0)
    {
        free(header);
        blk->type = PARTITION_TYPE_MBR;
        LibSpecialDriverMapperPartitionsMBR(blk);
        return blk->partitions;
    }

    DWORD tableSize = header->numPartitionEntries * header->sizeOfPartitionEntry;
    LibSpeicalDrive_GPT_Partition_Entry *partitionBuffer = malloc(tableSize);
    if (!partitionBuffer)
    {
        free(header);
        return NULL;
    }

    offset.QuadPart = header->partitionEntriesLba * blk->lbaSize;
    if (SetFilePointer(hDevice, offset.LowPart, &offset.HighPart, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
    {
        free(header);
        free(partitionBuffer);
        return NULL;
    }

    if (!ReadFile(hDevice, partitionBuffer, tableSize, &bytesRead, NULL) || bytesRead != tableSize)
    {
        free(header);
        free(partitionBuffer);
        return NULL;
    }

    blk->type = PARTITION_TYPE_GPT;
    LibSpecialDriverMapperPartitionsGPT(header, (uint8_t *)partitionBuffer, blk);
    free(header);
    return blk->partitions;
}

LibSpecialDrive_BlockDevice *LibSpecialDriverGetBlock(const char *path)
{
    if (!path)
        return NULL;

    LibSpecialDrive_Protective_MBR *MBR = malloc(sizeof(LibSpecialDrive_Protective_MBR));
    if (!MBR)
        return NULL;

    HANDLE hDevice = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
        goto error_mbr;

    DWORD bytesRead;
    if (!ReadFile(hDevice, MBR, sizeof(LibSpecialDrive_Protective_MBR), &bytesRead, NULL) || bytesRead != sizeof(LibSpecialDrive_Protective_MBR))
        goto error_device;

    GET_LENGTH_INFORMATION lenInfo;
    if (!DeviceIoControl(hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lenInfo, sizeof(lenInfo), &bytesRead, NULL))
        goto error_device;

    DISK_GEOMETRY geometry;
    if (!DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &geometry, sizeof(geometry), &bytesRead, NULL))
        goto error_device;

    LibSpecialDrive_BlockDevice *blk = calloc(1, sizeof(LibSpecialDrive_BlockDevice));
    if (!blk)
        goto error_device;

    blk->path = strdup(path);
    if (!blk->path)
        goto error_blk;

    blk->signature = MBR;
    blk->size = lenInfo.Length.QuadPart;
    blk->lbaSize = geometry.BytesPerSector;

    // Verifica se o dispositivo é removível
    STORAGE_PROPERTY_QUERY query = {StorageDeviceProperty, PropertyStandardQuery};
    BYTE buffer[1024] = {0};
    if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &bytesRead, NULL))
    {
        STORAGE_DEVICE_DESCRIPTOR *desc = (STORAGE_DEVICE_DESCRIPTOR *)buffer;
        if (desc->RemovableMedia)
            blk->flags |= BLOCK_FLAG_IS_REMOVABLE;
    }

    // Tenta obter partições
    if (!LibSpecialDriverGetPartition(blk, hDevice))
    {
        goto error_path;
    }

    CloseHandle(hDevice);
    return blk;

error_path:
    free(blk->path);
error_blk:
    free(blk);
error_device:
    CloseHandle(hDevice);
error_mbr:
    free(MBR);
    return NULL;
}

LibSpecialDrive *LibSpecialDriverGet(void)
{
    LibSpecialDrive *driver = calloc(1, sizeof(LibSpecialDrive));
    if (!driver)
        return NULL;

    char path[MAX_PATH];
    int failed = 0;
    for (size_t i = 0;; i++)
    {
        snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%zu", i);
        LibSpecialDrive_BlockDevice *blk = LibSpecialDriverGetBlock(path);
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

bool LibSpecialDriveMark(LibSpecialDrive *ctx, int blockNumber)
{
    if (!ctx || blockNumber < 0 || blockNumber >= ctx->commonBlockDeviceCount)
        return false;

    LibSpecialDrive_BlockDevice *blk = &ctx->commonBlockDevices[blockNumber];

    LibSpecialDrive_Flag flag = {0xFF, LIBSPECIAL_MAGIC_STRING, {0}};
    uint8_t *uuid = LibSpecialDriverGenUUID();

    memcpy(flag.uuid, uuid, 16);
    free(uuid);

    HANDLE hDevice = CreateFileA(blk->path, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
        return false;

    DWORD bytesRead;
    if (!ReadFile(hDevice, blk->signature, sizeof(LibSpecialDrive_Protective_MBR), &bytesRead, NULL) || bytesRead != sizeof(LibSpecialDrive_Protective_MBR))
    {
        CloseHandle(hDevice);
        return false;
    }

    memcpy(blk->signature->boot_code, &flag, sizeof(LibSpecialDrive_Flag));

    DWORD bytesWritten;
    BOOL success = WriteFile(hDevice, blk->signature, sizeof(LibSpecialDrive_Protective_MBR), &bytesWritten, NULL);

    CloseHandle(hDevice);
    LibSpecialDriverReload(ctx);
    return (success && bytesWritten == sizeof(LibSpecialDrive_Protective_MBR));
}

bool LibSpecialDriveUnmark(LibSpecialDrive *ctx, int blockNumber)
{
    if (!ctx || blockNumber < 0 || blockNumber >= ctx->specialBlockDeviceCount)
        return false;

    LibSpecialDrive_BlockDevice *blk = &ctx->specialBlockDevices[blockNumber];
    if (!blk->signature)
        return false;

    HANDLE hDevice = CreateFileA(blk->path, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE)
        return false;

    DWORD bytesRead;
    if (!ReadFile(hDevice, blk->signature, sizeof(LibSpecialDrive_Protective_MBR), &bytesRead, NULL) || bytesRead != sizeof(LibSpecialDrive_Protective_MBR))
    {
        CloseHandle(hDevice);
        return false;
    }

    memset(blk->signature->boot_code, 0, sizeof(LibSpecialDrive_Flag));
    DWORD bytesWritten;
    BOOL success = WriteFile(hDevice, blk->signature, sizeof(LibSpecialDrive_Protective_MBR), &bytesWritten, NULL);

    CloseHandle(hDevice);
    LibSpecialDriverReload(ctx);
    return (success && bytesWritten == sizeof(LibSpecialDrive_Protective_MBR));
}

#endif