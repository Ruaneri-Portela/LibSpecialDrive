#include <LibSpecialDrive.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// --- Estado interno ---

static bool randIsSeeded = false;
static uint8_t zeroGuid[16] = {0};

// --- UUID ---

void LibSpecialDriverGenUUID(uint8_t *uuid)
{
    if (!uuid)
        return;

    if (!randIsSeeded)
    {
        srand((unsigned int)time(NULL));
        randIsSeeded = true;
    }

    for (size_t i = 0; i < 16; i++)
        uuid[i] = (uint8_t)(rand() % 256);

    uuid[6] = (uuid[6] & 0x0F) | 0x40; // UUIDv4
    uuid[8] = (uuid[8] & 0x3F) | 0x80; // Variante
}

char *LibSpecialDriverGenUUIDString(uint8_t *uuid)
{
    if (!uuid)
        return NULL;

    char *uuidStr = malloc(37);
    if (!uuidStr)
        return NULL;

    snprintf(uuidStr, 37,
             "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             uuid[0], uuid[1], uuid[2], uuid[3],
             uuid[4], uuid[5], uuid[6], uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11],
             uuid[12], uuid[13], uuid[14], uuid[15]);

    return uuidStr;
}

// --- Verificação de assinatura especial ---

LibSpecialDrive_Flag *LibSpecialDriverIsSpecial(LibSpecialDrive_Protective_MBR *mbr)
{
    if (!mbr)
        return NULL;

    LibSpecialDrive_Flag *flag = (LibSpecialDrive_Flag *)mbr->boot_code;

    if (flag->hex != 0xFF)
        return NULL;

    if (strncmp(flag->libspecialDriveName, LIBSPECIAL_MAGIC_STRING, sizeof(flag->libspecialDriveName)) == 0)
        return flag;

    return NULL;
}

// --- Manipulação de dispositivos e partições ---

bool LibSpecialDriverBlockAppend(LibSpecialDrive *ctx, LibSpecialDrive_BlockDevice **blk)
{
    if (!ctx || !blk || !(*blk) || !(*blk)->signature)
        return false;

    LibSpecialDrive_Flag *flag = LibSpecialDriverIsSpecial((*blk)->signature);
    LibSpecialDrive_BlockDevice **targetList = flag ? &ctx->specialBlockDevices : &ctx->commonBlockDevices;
    size_t *count = flag ? &ctx->specialBlockDeviceCount : &ctx->commonBlockDeviceCount;

    LibSpecialDrive_BlockDevice *newList = realloc(*targetList, (*count + 1) * sizeof(**targetList));
    if (!newList)
        return false;

    *targetList = newList;
    (*targetList)[*count] = *(*blk);
    (*count)++;

    free(*blk);
    *blk = NULL;
    return true;
}

void LibSpecialDriverDestroyPartition(LibSpecialDrive_Partition *p)
{
    if (p)
    {
        free((char *)p->path);
        free((char *)p->mountPoint);
    }
}

void LibSpecialDriverDestroyBlock(LibSpecialDrive_BlockDevice *blk)
{
    if (!blk)
        return;

    free((char *)blk->path);
    for (int i = 0; i < blk->partitionCount; i++)
        LibSpecialDriverDestroyPartition(&blk->partitions[i]);

    free(blk->signature);
    free(blk->partitions);
}

void LibSpecialDriverMapperPartitionsMBR(LibSpecialDrive_BlockDevice *blk)
{
    if (!blk)
        return;

    for (uint32_t i = 0; i < 4; i++)
    {
        LibSpecialDrive_MBR_Partition_Entry *entry = &blk->signature->partitions[i];
        if (entry->partitionType == 0x00)
            continue;

        blk->partitions = realloc(blk->partitions, ((size_t)blk->partitionCount + 1) * sizeof(*blk->partitions));
        if (!blk->partitions)
            return;

        LibSpecialDrive_Partition *part = &blk->partitions[blk->partitionCount];
        memset(part, 0, sizeof(*part));
        part->path = LibSpecialDriverPartitionPathLookup(blk->path, blk->partitionCount);
        memcpy(&part->partitionMeta.mbr, entry, sizeof(*entry));
        part->lbaSize = &blk->lbaSize;
        LibSpecialDrivePartitionGetPathMount(part, blk->type);
        LibSpecialDriverDiretoryFreeSpaceLookup(part);
        blk->partitionCount++;
    }
}

void LibSpecialDriverMapperPartitionsGPT(LibSpecialDrive_GPT_Header *hdr, uint8_t *table, LibSpecialDrive_BlockDevice *blk)
{
    if (!hdr || !table || !blk)
        return;

    for (uint32_t i = 0; i < hdr->numPartitionEntries; i++)
    {
        LibSpecialDrive_GPT_Partition_Entry *entry = (void *)(table + i * hdr->sizeOfPartitionEntry);
        if (memcmp(entry->uniquePartitionGuid, zeroGuid, 16) == 0)
            continue;

        blk->partitions = realloc(blk->partitions, ((size_t)blk->partitionCount + 1) * sizeof(*blk->partitions));
        if (!blk->partitions)
            return;

        LibSpecialDrive_Partition *part = &blk->partitions[blk->partitionCount];
        memset(part, 0, sizeof(*part));
        part->path = LibSpecialDriverPartitionPathLookup(blk->path, blk->partitionCount);
        memcpy(&part->partitionMeta.gpt, entry, sizeof(*entry));
        part->lbaSize = &blk->lbaSize;
        LibSpecialDrivePartitionGetPathMount(part, blk->type);
        LibSpecialDriverDiretoryFreeSpaceLookup(part);
        blk->partitionCount++;
    }
}

// --- Gerenciamento de contexto ---

bool LibSpecialDriverReload(LibSpecialDrive *ctx)
{
    if (!ctx)
        return false;

    for (size_t i = 0; i < ctx->commonBlockDeviceCount; i++)
        LibSpecialDriverDestroyBlock(&ctx->commonBlockDevices[i]);
    free(ctx->commonBlockDevices);

    for (size_t i = 0; i < ctx->specialBlockDeviceCount; i++)
        LibSpecialDriverDestroyBlock(&ctx->specialBlockDevices[i]);
    free(ctx->specialBlockDevices);

    LibSpecialDrive *newCtx = LibSpecialDriverGet();
    if (!newCtx)
        return false;

    *ctx = *newCtx;
    free(newCtx);
    return true;
}

void LibSpecialDriverDestroy(LibSpecialDrive **ctx)
{
    if (!ctx || !*ctx)
        return;

    for (size_t i = 0; i < (*ctx)->commonBlockDeviceCount; i++)
        LibSpecialDriverDestroyBlock(&(*ctx)->commonBlockDevices[i]);

    for (size_t i = 0; i < (*ctx)->specialBlockDeviceCount; i++)
        LibSpecialDriverDestroyBlock(&(*ctx)->specialBlockDevices[i]);

    free(*ctx);
    *ctx = NULL;
}

// --- Acesso a blocos e partições ---

LibSpecialDrive_Partition *LibSpecialDriverGetPartition(LibSpecialDrive_BlockDevice *blk, LibSpecialDrive_DeviceHandle device)
{
    if (!blk || !blk->path || device == DEVICE_INVALID || !blk->signature)
        return NULL;

    LibSpecialDrive_GPT_Header *header = malloc(512);
    if (!header)
        return NULL;

    if (!LibSpecialDriveSeek(device, blk->signature->partitions->firstLBA * blk->lbaSize) ||
        LibSpecialDriveRead(device, 512, (uint8_t *)header) < 0)
    {
        free(header);
        blk->type = PARTITION_TYPE_MBR;
        LibSpecialDriverMapperPartitionsMBR(blk);
        return blk->partitions;
    }

    if (memcmp(&header->signature, GPT_SIGNATURE, 8) != 0)
    {
        free(header);
        blk->type = PARTITION_TYPE_MBR;
        LibSpecialDriverMapperPartitionsMBR(blk);
        return blk->partitions;
    }

    int64_t tableSize = header->numPartitionEntries * header->sizeOfPartitionEntry;
    uint8_t *partitionTable = malloc((size_t)tableSize);
    if (!partitionTable)
    {
        free(header);
        return NULL;
    }

    if (!LibSpecialDriveSeek(device, (int64_t)(header->partitionEntriesLba * blk->lbaSize)) ||
        LibSpecialDriveRead(device, tableSize, partitionTable) < 0)
    {
        free(header);
        free(partitionTable);
        return NULL;
    }

    blk->type = PARTITION_TYPE_GPT;
    LibSpecialDriverMapperPartitionsGPT(header, partitionTable, blk);
    free(header);
    free(partitionTable);
    return blk->partitions;
}

LibSpecialDrive_BlockDevice *LibSpecialDriverGetBlock(const char *path)
{
    if (!path)
        return NULL;

    LibSpecialDrive_Protective_MBR *mbr = malloc(sizeof(*mbr));
    if (!mbr)
        return NULL;

    LibSpecialDrive_DeviceHandle device = LibSpecialDriveOpenDevice(path, DEVICE_FLAG_READ | DEVICE_FLAG_SILENCE);
    if (device == DEVICE_INVALID)
    {
        free(mbr);
        return NULL;
    }

    if (LibSpecialDriveRead(device, sizeof(*mbr), (uint8_t *)mbr) < 0)
        goto error_device;

    LibSpecialDrive_BlockDevice *blk = calloc(1, sizeof(*blk));
    if (!blk)
        goto error_device;

    blk->path = strdup(path);
    blk->signature = mbr;

    if (!blk->path || !LibSpecialDriveLookUpSizes(device, blk))
        goto error_blk;

    LibSpecialDriveLookUpIsRemovable(device, blk);

    if (!LibSpecialDriverGetPartition(blk, device))
        goto error_blk;

    LibSpecialDriveCloseDevice(device);
    return blk;

error_blk:
    free(blk->path);
    free(blk);
error_device:
    LibSpecialDriveCloseDevice(device);
    free(mbr);
    return NULL;
}

// --- Marcações ---

bool LibSpecialDriveMark(LibSpecialDrive *ctx, int idx)
{
    if (!ctx || idx < 0 || (size_t)idx >= ctx->commonBlockDeviceCount)
        return false;

    LibSpecialDrive_BlockDevice *blk = &ctx->commonBlockDevices[idx];
    LibSpecialDrive_Flag flag = LIBSPECIAL_FLAG;
    LibSpecialDriverGenUUID((uint8_t *)&flag.uuid);

    LibSpecialDrive_DeviceHandle device = LibSpecialDriveOpenDevice(blk->path, DEVICE_FLAG_READ | DEVICE_FLAG_WRITE);
    if (device == DEVICE_INVALID)
        return false;

    if (LibSpecialDriveRead(device, sizeof(*blk->signature), (uint8_t *)blk->signature) < 0)
        goto error_device;

    if (!LibSpecialDriveSeek(device, 0))
        goto error_device;

    memcpy(blk->signature->boot_code, &flag, sizeof(LibSpecialDrive_Flag));
    int64_t written = LibSpecialDriveWrite(device, sizeof(*blk->signature), (uint8_t *)blk->signature);

    return (written == sizeof(*blk->signature)) && LibSpecialDriverReload(ctx);
error_device:
    LibSpecialDriveCloseDevice(device);
    return false;
}

bool LibSpecialDriveUnmark(LibSpecialDrive *ctx, int idx)
{
    if (!ctx || idx < 0 || (size_t)idx >= ctx->specialBlockDeviceCount)
        return false;

    LibSpecialDrive_BlockDevice *blk = &ctx->specialBlockDevices[idx];
    if (!blk->signature)
        return false;

    LibSpecialDrive_DeviceHandle device = LibSpecialDriveOpenDevice(blk->path, DEVICE_FLAG_READ | DEVICE_FLAG_WRITE);
    if (device == DEVICE_INVALID)
        return false;

    if (LibSpecialDriveRead(device, sizeof(*blk->signature), (uint8_t *)blk->signature) < 0)
        goto error_device;

    if (!LibSpecialDriveSeek(device, 0))
        goto error_device;

    memset(blk->signature->boot_code, 0, sizeof(LibSpecialDrive_Flag));
    int64_t written = LibSpecialDriveWrite(device, sizeof(*blk->signature), (uint8_t *)blk->signature);
    LibSpecialDriveCloseDevice(device);
    return (written == sizeof(*blk->signature)) && LibSpecialDriverReload(ctx);
error_device:
    LibSpecialDriveCloseDevice(device);
    return false;
}
