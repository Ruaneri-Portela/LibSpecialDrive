#include <LibSpecialDrive.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Estado interno
static bool randIsSeeded = false;

// --- UUID Functions ---

uint8_t *LibSpecialDriverGenUUID(void)
{
    if (!randIsSeeded)
    {
        srand((unsigned int)time(NULL));
        randIsSeeded = true;
    }

    uint8_t *uuid = malloc(16);
    if (!uuid)
        return NULL;

    for (size_t i = 0; i < 16; i++)
        uuid[i] = rand() % 256;

    uuid[6] = (uuid[6] & 0x0F) | 0x40; // UUIDv4
    uuid[8] = (uuid[8] & 0x3F) | 0x80; // Variant

    return uuid;
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

LibSpecialDrive_Flag *LibSpecialDriverIsSpecial(ProtectiveMBR *ptr)
{
    if (!ptr)
        return NULL;

    LibSpecialDrive_Flag *flag = (LibSpecialDrive_Flag *)ptr->boot_code;

    if (flag->hex != (char)0xFF)
        return NULL;

    if (strncmp(flag->libspecialDriveName, LIBSPECIAL_MAGIC_STRING, sizeof(flag->libspecialDriveName)) == 0)
        return flag;

    return NULL;
}

// --- Manipulação de blocos e partições ---

bool LibSpecialDriverBlockAppend(LibSpecialDrive *driver, LibSpecialDrive_BlockDevice **blockDevice)
{
    if (!driver || !blockDevice || !(*blockDevice)->signature)
        return false;

    LibSpecialDrive_Flag *flag = LibSpecialDriverIsSpecial((*blockDevice)->signature);
    LibSpecialDrive_BlockDevice **list = flag ? &driver->specialBlockDevices : &driver->commonBlockDevices;
    size_t *count = flag ? &driver->specialBlockDeviceCount : &driver->commonBlockDeviceCount;

    LibSpecialDrive_BlockDevice *newList = realloc(*list, (*count + 1) * sizeof(LibSpecialDrive_BlockDevice));
    if (!newList)
        return false;

    *list = newList;
    (*list)[*count] = *(*blockDevice);
    (*count)++;

    free(*blockDevice);
    *blockDevice = NULL;

    return true;
}

void LibSpecialDriverDestroyPartition(LibSpecialDrive_Partition *part)
{
    if (part)
    {
        free((char *)part->path);
        free((char *)part->mountPoint);
    }
}

void LibSpecialDriverDestroyBlock(LibSpecialDrive_BlockDevice *blk)
{
    if (blk)
    {
        free((char *)blk->path);

        for (int i = 0; i < blk->partitionCount; i++)
            LibSpecialDriverDestroyPartition(&blk->partitions[i]);

        free(blk->partitions);
    }
}

void LibSpecialDriverMapperPartitionsMBR(LibSpecialDrive_BlockDevice *blk)
{
    if (!blk)
        return;

    for (uint32_t i = 0; i < 4; i++)
    {
        MBR_Partition_Entry *entry = &blk->signature->partitions[i];
        if (entry->type == 0x00)
            continue;

        blk->partitions = realloc(blk->partitions, (blk->partitionCount + 1) * sizeof(LibSpecialDrive_Partition));
        memset(&blk->partitions[blk->partitionCount], 0, sizeof(LibSpecialDrive_Partition));
        blk->partitions[blk->partitionCount].path = LibSpecialDriverPartitionPathLookup(blk->path, blk->partitionCount);
        memcpy(&blk->partitions[blk->partitionCount].partitionMeta.mbr, entry, sizeof(MBR_Partition_Entry));
        LibSpecialDrivePartitionGetPathMount(&blk->partitions[blk->partitionCount],blk->type);
        blk->partitionCount++;
    }
}

void LibSpecialDriverMapperPartitionsGPT(LibSpeicalDrive_GPT_Header *header, uint8_t *partitionBuffer, LibSpecialDrive_BlockDevice *blk)
{
    if (!header || !partitionBuffer || !blk)
        return;

    for (uint32_t i = 0; i < header->numPartitionEntries; i++)
    {
        LibSpeicalDrive_GPT_Partition_Entry *entry = (LibSpeicalDrive_GPT_Partition_Entry *)(partitionBuffer + i * header->sizeOfPartitionEntry);

        int isEmpty = 1;
        for (int j = 0; j < 16; j++)
        {
            if (entry->partitionTypeGuid[j] != 0)
            {
                isEmpty = 0;
                break;
            }
        }
        if (isEmpty)
            continue;

        blk->partitions = realloc(blk->partitions, (blk->partitionCount + 1) * sizeof(LibSpecialDrive_Partition));
        memset(&blk->partitions[blk->partitionCount], 0, sizeof(LibSpecialDrive_Partition));
        blk->partitions[blk->partitionCount].path = LibSpecialDriverPartitionPathLookup(blk->path, blk->partitionCount);
        memcpy(&blk->partitions[blk->partitionCount].partitionMeta.gpt, entry, sizeof(LibSpeicalDrive_GPT_Partition_Entry));
        LibSpecialDrivePartitionGetPathMount(&blk->partitions[blk->partitionCount],blk->type);
        blk->partitionCount++;
    }
}

// --- Gerenciamento de contexto ---

bool LibSpecialDriverReload(LibSpecialDrive *ctx)
{
    LibSpecialDrive *newCtx = LibSpecialDriverGet();
    if (!newCtx)
        return false;

    if (ctx->commonBlockDevices)
    {
        for (size_t i = 0; i < ctx->commonBlockDeviceCount; i++)
            LibSpecialDriverDestroyBlock(&ctx->commonBlockDevices[i]);
        free(ctx->commonBlockDevices);
    }

    if (ctx->specialBlockDevices)
    {
        for (size_t i = 0; i < ctx->specialBlockDeviceCount; i++)
            LibSpecialDriverDestroyBlock(&ctx->specialBlockDevices[i]);
        free(ctx->specialBlockDevices);
    }

    *ctx = *newCtx;
    free(newCtx);

    return true;
}

void LibSpecialDriverDestroy(LibSpecialDrive **ctx)
{
    if (!ctx || !*ctx)
        return;

    LibSpecialDrive *context = *ctx;

    if (context->commonBlockDevices)
        for (size_t i = 0; i < context->commonBlockDeviceCount; i++)
            LibSpecialDriverDestroyBlock(&context->commonBlockDevices[i]);

    if (context->specialBlockDevices)
        for (size_t i = 0; i < context->specialBlockDeviceCount; i++)
            LibSpecialDriverDestroyBlock(&context->specialBlockDevices[i]);

    free(context);
    *ctx = NULL;
}
