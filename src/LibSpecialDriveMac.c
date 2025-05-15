#ifdef __APPLE__
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/disk.h>
#include <sys/mount.h>
#include <unistd.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <CoreFoundation/CoreFoundation.h>
#include <LibSpecialDrive.h>

char *LibSpecialDriverPartitionPathLookup(const char *path, int partitionNumber)
{
    if (!path || partitionNumber < 0)
        return NULL;

    char partitionPath[PATH_MAX];
    snprintf(partitionPath, PATH_MAX, "%ss%d", path, partitionNumber + 1);
    return strdup(partitionPath);
}

void LibSpecialDrivePartitionGetPathMount(LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type)
{
    (void)type;
    if (!part || !part->path)
        return;

    struct statfs *mounts;
    int count = getmntinfo(&mounts, MNT_NOWAIT);
    if (count == 0)
    {
        perror("getmntinfo");
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        if (strcmp(mounts[i].f_mntfromname, part->path) == 0)
        {
            part->mountPoint = strdup(mounts[i].f_mntonname);
            return;
        }
    }
}

LibSpecialDrive_Partition *LibSpecialDriverGetPartition(LibSpecialDrive_BlockDevice *blk)
{
    if (!blk || !blk->path)
        return NULL;

    int fd = open(blk->path, O_RDONLY);
    if (fd < 0)
        return NULL;

    uint8_t buffer[SECTOR_SIZE];
    ssize_t bytesRead;

    if ((bytesRead = pread(fd, buffer, SECTOR_SIZE, GPT_HEADER_OFFSET)) != SECTOR_SIZE)
    {
        close(fd);
        return NULL;
    }

    LibSpeicalDrive_GPT_Header *header = (LibSpeicalDrive_GPT_Header *)buffer;
    if (bytesRead != SECTOR_SIZE || memcmp(&header->signature, GPT_SIGNATURE, 8) != 0)
    {
        blk->type = PARTITION_TYPE_MBR;
        LibSpecialDriverMapperPartitionsMBR(blk);
        close(fd);
        return blk->partitions;
    }

    size_t tableSize = header->numPartitionEntries * header->sizeOfPartitionEntry;
    uint8_t *partitionBuffer = malloc(tableSize);
    if (!partitionBuffer)
    {
        close(fd);
        return NULL;
    }

    if (pread(fd, partitionBuffer, tableSize, header->partitionEntriesLba * SECTOR_SIZE) != tableSize)
    {
        free(partitionBuffer);
        close(fd);
        return NULL;
    }

    blk->type = PARTITION_TYPE_GPT;
    LibSpecialDriverMapperPartitionsGPT(header, partitionBuffer, blk);

    free(partitionBuffer);
    close(fd);
    return blk->partitions;
}

LibSpecialDrive_BlockDevice *LibSpecialDriverGetBlock(const char *path)
{
    if (!path)
        return NULL;

    LibSpecialDrive_Protective_MBR *MBR = malloc(sizeof(LibSpecialDrive_Protective_MBR));
    if (!MBR)
        return NULL;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        free(MBR);
        perror("open");
        return NULL;
    }

    ssize_t bytesRead = read(fd, MBR, sizeof(LibSpecialDrive_Protective_MBR));
    if (bytesRead < 0)
    {
        free(MBR);
        perror("read");
        close(fd);
        return NULL;
    }

    if (bytesRead != sizeof(LibSpecialDrive_Protective_MBR))
    {
        free(MBR);
        fprintf(stderr, "read: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    LibSpecialDrive_BlockDevice *blk = malloc(sizeof(LibSpecialDrive_BlockDevice));
    if (!blk)
    {
        close(fd);
        free(MBR);
        return NULL;
    }

    blk->path = strdup(path);
    if (!blk->path)
    {
        close(fd);
        free(MBR);
        free(blk);
        return NULL;
    }

    uint64_t blockCount = 0;

    if (ioctl(fd, DKIOCGETBLOCKSIZE, &blk->lbaSize) < 0)
    {
        perror("ioctl(DKIOCGETBLOCKSIZE)");
        // continue mesmo assim, talvez com valor padrão
    }

    if (ioctl(fd, DKIOCGETBLOCKCOUNT, &blockCount) < 0)
    {
        perror("ioctl(DKIOCGETBLOCKCOUNT)");
    }

    close(fd);

    blk->size = blk->lbaSize * blockCount;
    blk->signature = MBR;
    blk->partitions = LibSpecialDriverGetPartition(blk);

    return blk;
}

LibSpecialDrive *LibSpecialDriverGet(void)
{
    CFMutableDictionaryRef matchingDict = IOServiceMatching(kIOMediaClass);
    if (!matchingDict)
    {
        fprintf(stderr, "IOServiceMatching failed\n");
        return NULL;
    }

    CFDictionarySetValue(matchingDict, CFSTR(kIOMediaWholeKey), kCFBooleanTrue);

    io_iterator_t iterator;
    kern_return_t result = IOServiceGetMatchingServices(kIOMainPortDefault, matchingDict, &iterator);
    if (result != KERN_SUCCESS)
    {
        fprintf(stderr, "IOServiceGetMatchingServices failed: %x\n", result);
        return NULL;
    }

    io_object_t media;
    LibSpecialDrive *ctx = NULL;
    while ((media = IOIteratorNext(iterator)))
    {
        CFStringRef bsdName = IORegistryEntryCreateCFProperty(media, CFSTR("BSD Name"), kCFAllocatorDefault, 0);
        if (bsdName)
        {
            char name[PATH_MAX];
            char path[PATH_MAX];

            CFStringGetCString(bsdName, name, sizeof(name), kCFStringEncodingUTF8);
            snprintf(path, sizeof(path), "/dev/%s", name);
            CFRelease(bsdName);

            LibSpecialDrive_BlockDevice *blk = LibSpecialDriverGetBlock(path);
            if (blk)
            {
                if (!ctx)
                    ctx = calloc(1, sizeof(LibSpecialDrive));

                CFBooleanRef removable = (CFBooleanRef)IORegistryEntryCreateCFProperty(media, CFSTR("Removable"), kCFAllocatorDefault, 0);
                if (removable)
                {
                    // Se a propriedade Removable existir e for verdadeira, o dispositivo é removível
                    if (CFBooleanGetValue(removable))
                    {
                        blk->flags |= BLOCK_FLAG_IS_REMOVABLE;
                    }
                    CFRelease(removable);
                }

                LibSpecialDriverBlockAppend(ctx, &blk);
            }
        }
        IOObjectRelease(media);
    }

    IOObjectRelease(iterator);
    return ctx;
}

bool LibSpecialDriverUmount(const char *path)
{
    const char cmd[] = "diskutil unmountDisk force ";

    int cmdLen = sizeof(cmd) - 1;
    int pathLen = strlen(path);
    char *fullCmd = malloc(cmdLen + pathLen + 1);
    if (!fullCmd)
        return false;
    memcpy(fullCmd, cmd, cmdLen);
    memcpy(fullCmd + cmdLen, path, pathLen);
    fullCmd[cmdLen + pathLen] = '\0';

    int ret = system(fullCmd);

    free(fullCmd);

    return ret >= 0;
}

static char *LibSpecialDriveConvertRawPath(const char *diskPath)
{
    if (!diskPath)
        return NULL;

    const char *rawPath = "/dev/r";
    const char *name = strrchr(diskPath, '/') + 1;
    if (!name)
        return NULL;
    char *rawDiskPath = malloc(strlen(rawPath) + strlen(name) + 1);
    if (!rawDiskPath)
        return NULL;
    strcpy(rawDiskPath, rawPath);
    strcat(rawDiskPath, name);

    return rawDiskPath;
}

bool LibSpecialDriveMark(LibSpecialDrive *ctx, int blockNumber)
{
    if (!ctx || blockNumber < 0 || blockNumber >= ctx->commonBlockDeviceCount)
        return false;

    LibSpecialDrive_BlockDevice *blk = &ctx->commonBlockDevices[blockNumber];

    LibSpecialDrive_Flag flag = {0xFF, LIBSPECIAL_MAGIC_STRING, {0}};
    uint8_t *uuid = LibSpecialDriverGenUUID();

    if (LibSpecialDriverIsSpecial(blk->signature))
        return false;

    memcpy(flag.uuid, uuid, 16);
    free(uuid);

    LibSpecialDrive_Protective_MBR *mbr = malloc(sizeof(LibSpecialDrive_Protective_MBR));
    if (!mbr)
        return false;
    memcpy(mbr, blk->signature, sizeof(LibSpecialDrive_Protective_MBR));
    memcpy(mbr->boot_code, &flag, sizeof(LibSpecialDrive_Flag));

    if (!LibSpecialDriverUmount(blk->path))
    {
        free(mbr);
        return false;
    }

    char *path = LibSpecialDriveConvertRawPath(blk->path);
    int fd = open(path, O_WRONLY);
    if (fd < 0)
    {
        free(path);
        free(mbr);
        perror("open failed");
        return false;
    }

    if (flock(fd, LOCK_EX) < 0)
    {
        free(path);
        free(mbr);
        close(fd);
        perror("flock failed");
        return false;
    }

    ssize_t bytesWritten = write(fd, mbr, sizeof(LibSpecialDrive_Protective_MBR));

    free(path);
    free(mbr);
    fsync(fd);
    close(fd);
    LibSpecialDriverReload(ctx);
    return (bytesWritten == sizeof(LibSpecialDrive_Protective_MBR));
}

bool LibSpecialDriveUnmark(LibSpecialDrive *ctx, int blockNumber)
{
    if (!ctx || blockNumber < 0 || blockNumber >= ctx->specialBlockDeviceCount)
        return false;

    LibSpecialDrive_BlockDevice *blk = &ctx->specialBlockDevices[blockNumber];
    if (!blk->signature)
        return false;

    LibSpecialDrive_Protective_MBR *mbr = malloc(sizeof(LibSpecialDrive_Protective_MBR));
    if (!mbr)
        return false;

    if (!LibSpecialDriverUmount(blk->path))
    {
        free(mbr);
        return false;
    }

    // Tentando abrir com O_RDONLY como alternativa
    char *path = LibSpecialDriveConvertRawPath(blk->path);
    int fd = open(path, O_RDWR);
    if (fd < 0)
    {
        free(path);
        free(mbr);
        perror("open failed");
        return false;
    }

    if (flock(fd, LOCK_EX) < 0)
    {
        free(path);
        free(mbr);
        close(fd);
        perror("flock failed");
        return false;
    }

    ssize_t bytesRead = read(fd, mbr, sizeof(LibSpecialDrive_Protective_MBR));
    if (bytesRead != sizeof(LibSpecialDrive_Protective_MBR))
    {
        free(path);
        free(mbr);
        close(fd);
        return false;
    }

    memset(mbr->boot_code, 0, sizeof(mbr->boot_code));

    ssize_t bytesWritten = write(fd, mbr, sizeof(LibSpecialDrive_Protective_MBR));

    if (bytesWritten != sizeof(LibSpecialDrive_Protective_MBR))
    {
        free(path);
        free(mbr);
        close(fd);
        return false;
    }

    bytesRead = read(fd, mbr, sizeof(LibSpecialDrive_Protective_MBR));
    if (bytesRead != sizeof(LibSpecialDrive_Protective_MBR))
    {
        free(path);
        free(mbr);
        close(fd);
        return false;
    }

    for (int i = 0; i < sizeof(mbr->boot_code); i++)
    {
        if (mbr->boot_code[i] != 0)
        {
            free(path);
            free(mbr);
            close(fd);
            return false;
        }
    }

    free(path);
    free(mbr);
    fsync(fd);
    close(fd);
    LibSpecialDriverReload(ctx);
    return (bytesWritten == sizeof(LibSpecialDrive_Protective_MBR));
}
#endif
