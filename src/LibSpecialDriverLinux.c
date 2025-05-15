#ifdef __linux__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <mntent.h>
#include <limits.h>
#include <LibSpecialDrive.h>
#include <ctype.h>

char *LibSpecialDriverPartitionPathLookup(const char *path, int partitionNumber)
{
    if (!path || partitionNumber < 0)
        return NULL;

    char partitionPath[PATH_MAX];
    snprintf(partitionPath, PATH_MAX, "%s%d", path, partitionNumber + 1);
    int fd = open(partitionPath, O_RDONLY);
    if (!fd)
        snprintf(partitionPath, PATH_MAX, "%sp%d", path, partitionNumber + 1);
    else
        close(fd);
    return strdup(partitionPath);
}

void LibSpecialDrivePartitionGetPathMount(LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type)
{
    (void)type;
    if (!part || !part->path)
        return;

    FILE *fp = setmntent("/etc/mtab", "r");
    if (!fp)
        return;

    struct mntent *mnt;
    while ((mnt = getmntent(fp)) != NULL)
    {
        if (strcmp(mnt->mnt_fsname, part->path) == 0)
        {
            part->mountPoint = strdup(mnt->mnt_dir);
            break;
        }
    }

    endmntent(fp);
}

LibSpecialDrive_Partition *LibSpecialDriverGetPartition(LibSpecialDrive_BlockDevice *blk, int fd)
{
    if (!blk || !blk->path || fd < 0)
        return NULL;

    fsync(fd);

    LibSpeicalDrive_GPT_Header *header = malloc(sizeof(LibSpeicalDrive_GPT_Header));
    ssize_t bytesRead;

    if ((bytesRead = pread(fd, header, sizeof(LibSpeicalDrive_GPT_Header), blk->signature->partitions->lbaStart * blk->lbaSize)) != sizeof(LibSpeicalDrive_GPT_Header))
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

    size_t tableSize = header->numPartitionEntries * header->sizeOfPartitionEntry;
    LibSpeicalDrive_GPT_Partition_Entry *partitionBuffer = malloc(tableSize);
    if (!partitionBuffer)
    {
        free(header);
        return NULL;
    }

    if (pread(fd, partitionBuffer, tableSize, header->partitionEntriesLba * blk->lbaSize) != tableSize)
    {
        free(header);
        free(partitionBuffer);
        return NULL;
    }

    blk->type = PARTITION_TYPE_GPT;
    LibSpecialDriverMapperPartitionsGPT(header, (uint8_t *)partitionBuffer, blk);

    free(header);
    free(partitionBuffer);
    return blk->partitions;
}

LibSpecialDrive_BlockDevice *LibSpecialDriverGetBlock(const char *path)
{
    if (!path)
        return NULL;

    LibSpecialDrive_Protective_MBR *MBR = calloc(1, sizeof(LibSpecialDrive_Protective_MBR));
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
    if (bytesRead != sizeof(LibSpecialDrive_Protective_MBR))
    {
        perror("read");
        close(fd);
        free(MBR);
        return NULL;
    }

    LibSpecialDrive_BlockDevice *blk = calloc(1, sizeof(LibSpecialDrive_BlockDevice));
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

    if (ioctl(fd, BLKSSZGET, &blk->lbaSize) < 0)
    {
        close(fd);
        free(MBR);
        free(blk);
        perror("ioctl(BLKSSZGET)");
    }

    if (ioctl(fd, BLKGETSIZE64, &blk->size) < 0)
    {
        close(fd);
        free(MBR);
        free(blk);
        perror("ioctl(BLKGETSIZE64)");
    }

    blk->signature = MBR;
    blk->partitions = LibSpecialDriverGetPartition(blk, fd);
    close(fd);
    return blk;
}

LibSpecialDrive *LibSpecialDriverGet(void)
{
    LibSpecialDrive *ctx = calloc(1, sizeof(LibSpecialDrive));

    FILE *fp = fopen("/proc/partitions", "r");
    if (!fp)
    {
        perror("fopen");
        return NULL;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        unsigned int major, minor;
        unsigned long long blocks;
        char name[128];

        if (sscanf(line, " %u %u %llu %127s", &major, &minor, &blocks, name) != 4)
            continue;

        if (strchr(name, '/'))
            continue; // pular nomes inválidos
        if (isdigit(name[strlen(name) - 1]))
            continue; // pular partições

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/dev/%s", name);

        LibSpecialDrive_BlockDevice *blk = LibSpecialDriverGetBlock(path);
        if (!blk)
            continue;

        blk->flags |= BLOCK_FLAG_IS_REMOVABLE; // pode-se melhorar usando udev

        LibSpecialDriverBlockAppend(ctx, &blk);
    }

    fclose(fp);
    return ctx;
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
    memcpy(blk->signature->boot_code, &flag, sizeof(LibSpecialDrive_Flag));

    int fd = open(blk->path, O_WRONLY);
    if (fd < 0)
    {
        perror("open");
        return false;
    }

    ssize_t bytesWritten = write(fd, blk->signature, sizeof(LibSpecialDrive_Protective_MBR));
    close(fd);

    LibSpecialDriverReload(ctx);

    return (bytesWritten == sizeof(LibSpecialDrive_Protective_MBR));
}

bool LibSpecialDriveUnmark(LibSpecialDrive *ctx, int blockNumber)
{
    if (!ctx || blockNumber < 0 || blockNumber >= ctx->specialBlockDeviceCount)
        return false;

    LibSpecialDrive_BlockDevice *blk = &ctx->specialBlockDevices[blockNumber];

    int fd = open(blk->path, O_WRONLY);
    if (fd < 0)
    {
        perror("open");
        return false;
    }

    memset(blk->signature->boot_code, 0, sizeof(LibSpecialDrive_Flag));
    ssize_t bytesWritten = write(fd, blk->signature, sizeof(LibSpecialDrive_Protective_MBR)) != sizeof(LibSpecialDrive_Protective_MBR);
    close(fd);

    LibSpecialDriverReload(ctx);
    return (bytesWritten == sizeof(LibSpecialDrive_Protective_MBR));
}

#endif // __linux__