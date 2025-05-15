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

void LibSpecialDrivePartitionGetPathMount(struct LibSpeicalDrive_Partition *part)
{
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

struct LibSpeicalDrive_Partition *LibSpecialDriverGetPartition(struct LibSpeicalDrive_BlockDevice *blk)
{
    if (!blk || !blk->path)
        return NULL;

    int fd = open(blk->path, O_RDONLY);
    if (fd < 0)
        return NULL;

    uint8_t buffer[SECTOR_SIZE];
    ssize_t bytesRead = pread(fd, buffer, SECTOR_SIZE, GPT_HEADER_OFFSET);
    if (bytesRead != SECTOR_SIZE)
    {
        close(fd);
        return NULL;
    }

    LibSpeicalDrive_GPT_Header *header = (LibSpeicalDrive_GPT_Header *)buffer;
    if (memcmp(&header->signature, GPT_SIGNATURE, 8) != 0)
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

struct LibSpeicalDrive_BlockDevice *LibSpecialDriverGetBlock(const char *path)
{
    if (!path)
        return NULL;

    ProtectiveMBR *MBR = malloc(sizeof(ProtectiveMBR));
    if (!MBR)
        return NULL;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        free(MBR);
        perror("open");
        return NULL;
    }

    ssize_t bytesRead = read(fd, MBR, sizeof(ProtectiveMBR));
    if (bytesRead != sizeof(ProtectiveMBR))
    {
        perror("read");
        close(fd);
        free(MBR);
        return NULL;
    }

    struct LibSpeicalDrive_BlockDevice *blk = malloc(sizeof(*blk));
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

    unsigned long blockSize = 512;
    unsigned long long blockCount = 0;

    if (ioctl(fd, BLKSSZGET, &blockSize) < 0)
        perror("ioctl(BLKSSZGET)");

    if (ioctl(fd, BLKGETSIZE64, &blk->size) < 0)
        perror("ioctl(BLKGETSIZE64)");

    close(fd);

    blk->signature = MBR;
    blk->partitions = LibSpecialDriverGetPartition(blk);
    return blk;
}

struct LibSpecialDrive *LibSpecialDriverGet(void)
{
    struct LibSpecialDrive *ctx = calloc(1, sizeof(struct LibSpecialDrive));

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

        struct LibSpeicalDrive_BlockDevice *blk = LibSpecialDriverGetBlock(path);
        if (!blk)
            continue;

        blk->flags |= BLOCK_FLAG_IS_REMOVABLE; // pode-se melhorar usando udev

        LibSpecialDriverBlockAppend(ctx, &blk);
    }

    fclose(fp);
    return ctx;
}

static char *LibSpecialDriveConvertRawPath(const char *diskPath)
{
    return strdup(diskPath); // no Linux, o caminho é o mesmo
}

bool LibSpecialDriveMark(struct LibSpecialDrive *ctx, int blockNumber)
{
    if (!ctx || blockNumber < 0 || blockNumber >= ctx->commonBlockDeviceCount)
        return false;

    struct LibSpeicalDrive_BlockDevice *blk = &ctx->commonBlockDevices[blockNumber];
    struct LibSpecialFlag flag = {0xFF, LIBSPECIAL_MAGIC_STRING, {0}};
    uint8_t *uuid = LibSpecialDriverGenUUID();

    if (LibSpecialDriverIsSpecial(blk->signature))
        return false;

    memcpy(flag.uuid, uuid, 16);
    free(uuid);

    ProtectiveMBR *mbr = malloc(sizeof(ProtectiveMBR));
    memcpy(mbr, blk->signature, sizeof(*mbr));
    memcpy(mbr->boot_code, &flag, sizeof(flag));

    int fd = open(blk->path, O_WRONLY);
    if (fd < 0)
    {
        perror("open");
        free(mbr);
        return false;
    }

    flock(fd, LOCK_EX);
    ssize_t bytesWritten = write(fd, mbr, sizeof(*mbr));
    fsync(fd);
    close(fd);
    free(mbr);

    LibSpecialDriverReload(ctx);
    return (bytesWritten == sizeof(ProtectiveMBR));
}

bool LibSpecialDriveUnmark(struct LibSpecialDrive *ctx, int blockNumber)
{
    if (!ctx || blockNumber < 0 || blockNumber >= ctx->specialBlockDeviceCount)
        return false;

    struct LibSpeicalDrive_BlockDevice *blk = &ctx->specialBlockDevices[blockNumber];

    int fd = open(blk->path, O_RDWR);
    if (fd < 0)
    {
        perror("open");
        return false;
    }

    flock(fd, LOCK_EX);

    ProtectiveMBR mbr;
    if (read(fd, &mbr, sizeof(mbr)) != sizeof(mbr))
    {
        close(fd);
        return false;
    }

    memset(mbr.boot_code, 0, sizeof(mbr.boot_code));

    if (write(fd, &mbr, sizeof(mbr)) != sizeof(mbr))
    {
        close(fd);
        return false;
    }

    fsync(fd);
    close(fd);
    LibSpecialDriverReload(ctx);
    return true;
}

#endif // __linux__