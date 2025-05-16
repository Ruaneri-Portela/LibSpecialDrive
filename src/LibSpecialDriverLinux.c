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
    snprintf(partitionPath, sizeof(partitionPath), "%s%d", path, partitionNumber + 1);

    int fd = open(partitionPath, O_RDONLY);
    if (!(fd < 0))
    {
        close(fd);
        return strdup(partitionPath);
    }
    snprintf(partitionPath, sizeof(partitionPath), "%sp%d", path, partitionNumber + 1);
    fd = open(partitionPath, O_RDONLY);
    if (fd < 0)
    {
        return NULL;
    }

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

bool LibSpecialDriveLookUpSizes(LibSpecialDrive_DeviceHandle device, LibSpecialDrive_BlockDevice *blk)
{
    if (ioctl(device, BLKSSZGET, &blk->lbaSize) < 0)
    {
        perror("ioctl(BLKSSZGET)");
        return false;
    }

    if (ioctl(device, BLKGETSIZE64, &blk->size) < 0)
    {
        perror("ioctl(BLKGETSIZE64)");
        return false;
    }

    return true;
}

bool LibSpecialDriveLookUpIsRemovable(LibSpecialDrive_DeviceHandle device, LibSpecialDrive_BlockDevice *blk)
{
    (void)device;
    (void)blk;
    return true; // Pode ser melhorado com verificação real via udev/sysfs
}

LibSpecialDrive *LibSpecialDriverGet(void)
{
    LibSpecialDrive *ctx = calloc(1, sizeof(LibSpecialDrive));
    if (!ctx)
        return NULL;

    FILE *fp = fopen("/proc/partitions", "r");
    if (!fp)
    {
        perror("fopen");
        free(ctx);
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
            continue; // ignorar nomes inválidos
        if (isdigit(name[strlen(name) - 1]))
            continue; // pular partições (ex: sda1, sdb2)

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/dev/%s", name);

        LibSpecialDrive_BlockDevice *blk = LibSpecialDriverGetBlock(path);
        if (!blk)
            continue;

        LibSpecialDriverBlockAppend(ctx, &blk);
    }

    fclose(fp);
    return ctx;
}

LibSpecialDrive_DeviceHandle LibSpecialDriveOpenDevice(const char *path, enum LibSpecialDrive_DeviceHandle_Flags flags)
{
    int access = 0;

    if ((flags & DEVICE_FLAG_READ) && (flags & DEVICE_FLAG_WRITE))
        access = O_RDWR;
    else if (flags & DEVICE_FLAG_READ)
        access = O_RDONLY;
    else if (flags & DEVICE_FLAG_WRITE)
        access = O_WRONLY;
    else
    {
        errno = EINVAL;
        return -1;
    }

    int fd = open(path, access);
    if (fd < 0)
        perror("open");

    return fd;
}

bool LibSpecialDriveSeek(LibSpecialDrive_DeviceHandle device, int64_t offset)
{
    off_t result = lseek(device, (off_t)offset, SEEK_SET);
    if (result == (off_t)-1)
    {
        perror("lseek");
        return false;
    }
    return true;
}

int64_t LibSpecialDriveRead(LibSpecialDrive_DeviceHandle device, int64_t len, uint8_t *target)
{
    ssize_t bytesRead = read(device, target, (size_t)len);
    if (bytesRead < 0)
    {
        perror("read");
        return -1;
    }
    return (int64_t)bytesRead;
}

int64_t LibSpecialDriveWrite(LibSpecialDrive_DeviceHandle device, int64_t len, const uint8_t *source)
{
    ssize_t bytesWritten = write(device, source, (size_t)len);
    if (bytesWritten < 0)
    {
        perror("write");
        return -1;
    }
    return (int64_t)bytesWritten;
}

void LibSpecialDriveCloseDevice(LibSpecialDrive_DeviceHandle device)
{
    if (device >= 0)
        close(device);
}
#else
#define LIBSPECIALDRIVEMLINUX_C_EMPTY
void LibSpecialDriveLINUX_dummy(void) {}
#endif // __linux__
