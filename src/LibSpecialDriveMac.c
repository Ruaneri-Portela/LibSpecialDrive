#ifdef __APPLE__

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/disk.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <CoreFoundation/CoreFoundation.h>
#include <LibSpecialDrive.h>

void LibSpecialDriverDiretoryFreeSpaceLookup(LibSpecialDrive_Partition *part)
{
    if (!part && !part->mountPoint)
        return;
    struct statvfs stat;

    if (statvfs(part->mountPoint, &stat) != 0)
    {
        part->freeSpace = 0;
    }

    unsigned long block_size = stat.f_frsize;
    part->freeSpace = stat.f_bavail * block_size;
}

// Cria caminho para uma partição: ex. "/dev/disk2" + 1 => "/dev/disk2s1"
char *LibSpecialDriverPartitionPathLookup(const char *path, int partitionNumber)
{
    if (!path || partitionNumber < 0)
        return NULL;

    char partitionPath[PATH_MAX];
    snprintf(partitionPath, sizeof(partitionPath), "%ss%d", path, partitionNumber + 1);
    return strdup(partitionPath);
}

// Obtém o ponto de montagem da partição, se houver
void LibSpecialDrivePartitionGetPathMount(LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type)
{
    (void)type;
    if (!part || !part->path)
        return;

    struct statfs *mounts;
    int count = getmntinfo(&mounts, MNT_NOWAIT);
    if (count <= 0)
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

// Obtém tamanho do dispositivo em bytes e tamanho do setor lógico
bool LibSpecialDriveLookUpSizes(LibSpecialDrive_DeviceHandle device, LibSpecialDrive_BlockDevice *blk)
{
    if (!blk)
        return false;

    uint64_t blockCount = 0;

    if (ioctl(device, DKIOCGETBLOCKSIZE, &blk->lbaSize) < 0)
    {
        perror("ioctl(DKIOCGETBLOCKSIZE)");
        return false;
    }

    if (ioctl(device, DKIOCGETBLOCKCOUNT, &blockCount) < 0)
    {
        perror("ioctl(DKIOCGETBLOCKCOUNT)");
        return false;
    }

    blk->size = blk->lbaSize * blockCount;
    return true;
}

// Placeholder, assume que dispositivos são removíveis (implementação real é externa)
bool LibSpecialDriveLookUpIsRemovable(LibSpecialDrive_DeviceHandle device, LibSpecialDrive_BlockDevice *blk)
{
    (void)device;
    (void)blk;
    return true;
}

// Função interna para desmontar disco com diskutil
static bool LibSpecialDriverUmount(const char *path)
{
    if (!path)
        return false;

    char command[PATH_MAX + 64];
    snprintf(command, sizeof(command), "diskutil unmountDisk force %s", path);

    int ret = system(command);
    return (ret >= 0);
}

// Abre um dispositivo com as permissões especificadas
LibSpecialDrive_DeviceHandle LibSpecialDriveOpenDevice(const char *path, enum LibSpecialDrive_DeviceHandle_Flags flags)
{
    if (!path)
    {
        errno = EINVAL;
        return -1;
    }

    int access = 0;
    if ((flags & DEVICE_FLAG_READ) && (flags & DEVICE_FLAG_WRITE))
    {
        if (!LibSpecialDriverUmount(path))
            return -1;
        access = O_RDWR;
    }
    else if (flags & DEVICE_FLAG_READ)
    {
        access = O_RDONLY;
    }
    else if (flags & DEVICE_FLAG_WRITE)
    {
        access = O_WRONLY;
    }
    else
    {
        errno = EINVAL;
        return -1;
    }

    int fd = open(path, access);
    if (fd < 0 && !(flags & DEVICE_FLAG_SILENCE))
        perror("open");

    return fd;
}

// Move o ponteiro de leitura/escrita para o offset especificado
bool LibSpecialDriveSeek(LibSpecialDrive_DeviceHandle device, int64_t offset)
{
    if (lseek(device, (off_t)offset, SEEK_SET) == (off_t)-1)
    {
        perror("lseek");
        return false;
    }
    return true;
}

// Lê bytes do dispositivo para buffer
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

// Escreve bytes do buffer para o dispositivo
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

// Fecha o descritor de arquivo do dispositivo
void LibSpecialDriveCloseDevice(LibSpecialDrive_DeviceHandle device)
{
    if (device >= 0)
        close(device);
}

// Itera pelos dispositivos IOMedia e coleta os que são "whole" (inteiros)
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
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matchingDict, &iterator) != KERN_SUCCESS)
    {
        fprintf(stderr, "IOServiceGetMatchingServices failed\n");
        return NULL;
    }

    LibSpecialDrive *ctx = NULL;
    io_object_t media;

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

                CFBooleanRef removable = IORegistryEntryCreateCFProperty(media, CFSTR("Removable"), kCFAllocatorDefault, 0);
                if (removable && CFBooleanGetValue(removable))
                {
                    blk->flags |= BLOCK_FLAG_IS_REMOVABLE;
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
#else
#define LIBSPECIALDRIVEMAC_C_EMPTY
void LibSpecialDriveMAC_dummy(void) {}
#endif // __APPLE__