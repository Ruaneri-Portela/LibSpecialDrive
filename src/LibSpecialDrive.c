#include <LibSpecialDrive.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// --- Constantes e Estado Interno ---

static const uint8_t zeroGuid[16] = {0};

// --- UUID RFC 4122 v4 Pseudo-Random Engine ---

static uint32_t xorshift32(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return *state = x;
}

void LibSpecialDriveGenUUID(uint8_t *uuid) {
  if (!uuid)
    return;

  static uint32_t seed = 0;
  if (seed == 0) {
    seed = (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)uuid;
  }

  for (size_t i = 0; i < 16; i += 4) {
    uint32_t val = xorshift32(&seed);
    uuid[i] = (uint8_t)(val & 0xFF);
    uuid[i + 1] = (uint8_t)((val >> 8) & 0xFF);
    uuid[i + 2] = (uint8_t)((val >> 16) & 0xFF);
    uuid[i + 3] = (uint8_t)((val >> 24) & 0xFF);
  }

  // Ajusta bits conforme especificação UUIDv4
  uuid[6] = (uuid[6] & 0x0F) | 0x40; // Version 4
  uuid[8] = (uuid[8] & 0x3F) | 0x80; // Variant 10xx
}

char *LibSpecialDriveGenUUIDString(uint8_t *uuid) {
  if (!uuid)
    return NULL;

  char *uuidStr = (char *)malloc(37);
  if (!uuidStr)
    return NULL;

  snprintf(
      uuidStr, 37,
      "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
      uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
      uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14],
      uuid[15]);

  return uuidStr;
}

// --- Verificação de assinatura especial ---

LibSpecialDrive_Flag *
LibSpecialDriveIsSpecial(LibSpecialDrive_Protective_MBR *mbr) {
  if (!mbr)
    return NULL;

  LibSpecialDrive_Flag *flag = (LibSpecialDrive_Flag *)mbr->boot_code;

  if (flag->hex != 0xFF)
    return NULL;

  if (strncmp(flag->libspecialDriveName, LIBSPECIAL_MAGIC_STRING,
              sizeof(flag->libspecialDriveName)) == 0)
    return flag;

  return NULL;
}

// --- Manipulação de dispositivos e partições ---

bool LibSpecialDriveBlockAppend(LibSpecialDrive *ctx,
                                LibSpecialDrive_BlockDevice **blk) {
  if (!ctx || !blk || !(*blk) || !(*blk)->signature)
    return false;

  LibSpecialDrive_Flag *flag = LibSpecialDriveIsSpecial((*blk)->signature);
  LibSpecialDrive_BlockDevice **targetList =
      flag ? &ctx->specialBlockDevices : &ctx->commonBlockDevices;
  size_t *count =
      flag ? &ctx->specialBlockDeviceCount : &ctx->commonBlockDeviceCount;

  LibSpecialDrive_BlockDevice *newList = (LibSpecialDrive_BlockDevice *)realloc(
      *targetList, (*count + 1) * sizeof(**targetList));
  if (!newList)
    return false;

  *targetList = newList;
  (*targetList)[*count] = *(*blk);
  (*count)++;

  free(*blk);
  *blk = NULL;
  return true;
}

void LibSpecialDriveDestroyPartition(LibSpecialDrive_Partition *p) {
  if (p) {
    if (p->path) {
      free((char *)p->path);
      p->path = NULL;
    }
    if (p->mountPoint) {
      free((char *)p->mountPoint);
      p->mountPoint = NULL;
    }
  }
}

void LibSpecialDriveDestroyBlock(LibSpecialDrive_BlockDevice *blk) {
  if (!blk)
    return;

  if (blk->path) {
    free((char *)blk->path);
    blk->path = NULL;
  }

  if (blk->partitions) {
    for (int i = 0; i < blk->partitionCount; i++) {
      LibSpecialDriveDestroyPartition(&blk->partitions[i]);
    }
    free(blk->partitions);
    blk->partitions = NULL;
  }

  if (blk->signature) {
    free(blk->signature);
    blk->signature = NULL;
  }
}

void LibSpecialDriveMapperPartitionsMBR(LibSpecialDrive_BlockDevice *blk) {
  if (!blk || !blk->signature)
    return;

  for (uint32_t i = 0; i < 4; i++) {
    LibSpecialDrive_MBR_Partition_Entry *entry = &blk->signature->partitions[i];
    if (entry->partitionType == 0x00)
      continue;

    LibSpecialDrive_Partition *newParts =
        realloc(blk->partitions,
                ((size_t)blk->partitionCount + 1) * sizeof(*blk->partitions));
    if (!newParts)
      return;

    blk->partitions = newParts;
    LibSpecialDrive_Partition *part = &blk->partitions[blk->partitionCount];
    memset(part, 0, sizeof(*part));

    part->path =
        LibSpecialDrivePartitionPathLookup(blk->path, blk->partitionCount);
    memcpy(&part->partitionMeta.mbr, entry, sizeof(*entry));
    part->lbaSize = &blk->lbaSize;

    LibSpecialDrivePartitionGetPathMount(part, blk->type);
    LibSpecialDriveDiretoryFreeSpaceLookup(part);
    blk->partitionCount++;
  }
}

void LibSpecialDriveMapperPartitionsGPT(LibSpecialDrive_GPT_Header *hdr,
                                        uint8_t *table,
                                        LibSpecialDrive_BlockDevice *blk) {
  if (!hdr || !table || !blk)
    return;

  for (uint32_t i = 0; i < hdr->numPartitionEntries; i++) {
    LibSpecialDrive_GPT_Partition_Entry *entry =
        (LibSpecialDrive_GPT_Partition_Entry *)(table +
                                                i * hdr->sizeOfPartitionEntry);
    if (memcmp(entry->uniquePartitionGuid, zeroGuid, 16) == 0)
      continue;

    LibSpecialDrive_Partition *newParts =
        realloc(blk->partitions,
                ((size_t)blk->partitionCount + 1) * sizeof(*blk->partitions));
    if (!newParts)
      return;

    blk->partitions = newParts;
    LibSpecialDrive_Partition *part = &blk->partitions[blk->partitionCount];
    memset(part, 0, sizeof(*part));

    part->path =
        LibSpecialDrivePartitionPathLookup(blk->path, blk->partitionCount);
    memcpy(&part->partitionMeta.gpt, entry, sizeof(*entry));
    part->lbaSize = &blk->lbaSize;

    LibSpecialDrivePartitionGetPathMount(part, blk->type);
    LibSpecialDriveDiretoryFreeSpaceLookup(part);
    blk->partitionCount++;
  }
}

// --- Gerenciamento de contexto ---

bool LibSpecialDriveReload(LibSpecialDrive *ctx) {
  if (!ctx)
    return false;

  LibSpecialDrive *newCtx = LibSpecialDriveGet();
  if (!newCtx)
    return false;

  // Limpa estado antigo
  for (size_t i = 0; i < ctx->commonBlockDeviceCount; i++)
    LibSpecialDriveDestroyBlock(&ctx->commonBlockDevices[i]);
  free(ctx->commonBlockDevices);

  for (size_t i = 0; i < ctx->specialBlockDeviceCount; i++)
    LibSpecialDriveDestroyBlock(&ctx->specialBlockDevices[i]);
  free(ctx->specialBlockDevices);

  // Copia dados do novo contexto
  *ctx = *newCtx;
  free(newCtx);
  return true;
}

void LibSpecialDriveDestroy(LibSpecialDrive **ctx) {
  if (!ctx || !*ctx)
    return;

  for (size_t i = 0; i < (*ctx)->commonBlockDeviceCount; i++)
    LibSpecialDriveDestroyBlock(&(*ctx)->commonBlockDevices[i]);

  for (size_t i = 0; i < (*ctx)->specialBlockDeviceCount; i++)
    LibSpecialDriveDestroyBlock(&(*ctx)->specialBlockDevices[i]);

  free((*ctx)->commonBlockDevices);
  free((*ctx)->specialBlockDevices);
  free(*ctx);
  *ctx = NULL;
}

// --- Acesso a blocos e partições ---

LibSpecialDrive_Partition *
LibSpecialDriveGetPartition(LibSpecialDrive_BlockDevice *blk,
                            LibSpecialDrive_DeviceHandle device) {
  if (!blk || !blk->path || device == DEVICE_INVALID || !blk->signature)
    return NULL;

  LibSpecialDrive_GPT_Header *header =
      (LibSpecialDrive_GPT_Header *)malloc(512);
  if (!header)
    return NULL;

  // GPT Header sempre reside no LBA 1 (Offset = 1 * lbaSize)
  int64_t gptLbaOffset = (int64_t)blk->lbaSize;

  if (!LibSpecialDriveSeek(device, gptLbaOffset) ||
      LibSpecialDriveRead(device, 512, (uint8_t *)header) < 0) {
    free(header);
    blk->type = PARTITION_TYPE_MBR;
    LibSpecialDriveMapperPartitionsMBR(blk);
    return blk->partitions;
  }

  if (memcmp(&header->signature, GPT_SIGNATURE, 8) != 0) {
    free(header);
    blk->type = PARTITION_TYPE_MBR;
    LibSpecialDriveMapperPartitionsMBR(blk);
    return blk->partitions;
  }

  int64_t tableSize = (int64_t)header->numPartitionEntries *
                      (int64_t)header->sizeOfPartitionEntry;
  uint8_t *partitionTable = (uint8_t *)malloc((size_t)tableSize);
  if (!partitionTable) {
    free(header);
    return NULL;
  }

  if (!LibSpecialDriveSeek(
          device, (int64_t)(header->partitionEntriesLba * blk->lbaSize)) ||
      LibSpecialDriveRead(device, tableSize, partitionTable) < 0) {
    free(header);
    free(partitionTable);
    return NULL;
  }

  blk->type = PARTITION_TYPE_GPT;
  LibSpecialDriveMapperPartitionsGPT(header, partitionTable, blk);
  free(header);
  free(partitionTable);
  return blk->partitions;
}

LibSpecialDrive_BlockDevice *LibSpecialDriveGetBlock(const char *path) {
  if (!path)
    return NULL;

  LibSpecialDrive_Protective_MBR *mbr =
      (LibSpecialDrive_Protective_MBR *)calloc(1, sizeof(*mbr));
  if (!mbr)
    return NULL;

  LibSpecialDrive_DeviceHandle device =
      LibSpecialDriveOpenDevice(path, DEVICE_FLAG_READ | DEVICE_FLAG_SILENCE);
  if (device == DEVICE_INVALID) {
    free(mbr);
    return NULL;
  }

  if (LibSpecialDriveRead(device, sizeof(*mbr), (uint8_t *)mbr) < 0)
    goto error_device;

  LibSpecialDrive_BlockDevice *blk =
      (LibSpecialDrive_BlockDevice *)calloc(1, sizeof(*blk));
  if (!blk)
    goto error_device;

  blk->path = strdup(path);
  blk->signature = mbr;

  if (!blk->path || !LibSpecialDriveLookUpSizes(device, blk))
    goto error_blk;

  LibSpecialDriveLookUpIsRemovable(device, blk);

  if (!LibSpecialDriveGetPartition(blk, device))
    goto error_blk;

  LibSpecialDriveCloseDevice(device);
  return blk;

error_blk:
  if (blk->path)
    free(blk->path);
  free(blk);
error_device:
  LibSpecialDriveCloseDevice(device);
  free(mbr);
  return NULL;
}

// --- Marcações ---

bool LibSpecialDriveMark(LibSpecialDrive *ctx, int idx) {
  if (!ctx || idx < 0 || (size_t)idx >= ctx->commonBlockDeviceCount)
    return false;

  LibSpecialDrive_BlockDevice *blk = &ctx->commonBlockDevices[idx];
  LibSpecialDrive_Flag flag = LIBSPECIAL_FLAG;
  LibSpecialDriveGenUUID((uint8_t *)&flag.uuid);

  LibSpecialDrive_DeviceHandle device = LibSpecialDriveOpenDevice(
      blk->path, DEVICE_FLAG_READ | DEVICE_FLAG_WRITE);
  if (device == DEVICE_INVALID)
    return false;

  if (LibSpecialDriveRead(device, sizeof(*blk->signature),
                          (uint8_t *)blk->signature) < 0)
    goto error_device;

  if (!LibSpecialDriveSeek(device, 0))
    goto error_device;

  memcpy(blk->signature->boot_code, &flag, sizeof(LibSpecialDrive_Flag));
  int64_t written = LibSpecialDriveWrite(device, sizeof(*blk->signature),
                                         (uint8_t *)blk->signature);

  LibSpecialDriveCloseDevice(device);

  if (written == (int64_t)sizeof(*blk->signature)) {
    return LibSpecialDriveReload(ctx);
  }
  return false;

error_device:
  LibSpecialDriveCloseDevice(device);
  return false;
}

bool LibSpecialDriveUnmark(LibSpecialDrive *ctx, int idx) {
  if (!ctx || idx < 0 || (size_t)idx >= ctx->specialBlockDeviceCount)
    return false;

  LibSpecialDrive_BlockDevice *blk = &ctx->specialBlockDevices[idx];
  if (!blk->signature)
    return false;

  LibSpecialDrive_DeviceHandle device = LibSpecialDriveOpenDevice(
      blk->path, DEVICE_FLAG_READ | DEVICE_FLAG_WRITE);
  if (device == DEVICE_INVALID)
    return false;

  if (LibSpecialDriveRead(device, sizeof(*blk->signature),
                          (uint8_t *)blk->signature) < 0)
    goto error_device;

  if (!LibSpecialDriveSeek(device, 0))
    goto error_device;

  memset(blk->signature->boot_code, 0, sizeof(LibSpecialDrive_Flag));
  int64_t written = LibSpecialDriveWrite(device, sizeof(*blk->signature),
                                         (uint8_t *)blk->signature);

  LibSpecialDriveCloseDevice(device);

  if (written == (int64_t)sizeof(*blk->signature)) {
    return LibSpecialDriveReload(ctx);
  }
  return false;

error_device:
  LibSpecialDriveCloseDevice(device);
  return false;
}

void LibSpecialDriveFree(void *ptr) {
  if (ptr) {
    free(ptr);
  }
}