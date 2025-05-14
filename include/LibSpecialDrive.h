#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// GPT (GUID Partition Table)
#define SECTOR_SIZE 512
#define GPT_HEADER_OFFSET 512
#define GPT_SIGNATURE "EFI PART"
#define MAX_PARTITIONS 128
#define PARTITION_ENTRY_SIZE 128

typedef struct
{
    uint8_t partition_type_guid[16];
    uint8_t unique_partition_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t name[36]; // UTF-16LE
} __attribute__((packed)) LibSpeicalDrive_GPT_Partition_Entry;

typedef struct
{
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entries_lba;
    uint32_t num_partition_entries;
    uint32_t size_of_partition_entry;
    uint32_t partition_entries_crc32;
} __attribute__((packed)) LibSpeicalDrive_GPT_Header;

// MBR (Master Boot Record)
typedef struct
{
    uint8_t status;       // 0x00
    uint8_t chs_first[3]; // 0xFF 0xFF 0xFF
    uint8_t type;         // 0xEE (GPT protective)
    uint8_t chs_last[3];  // 0xFF 0xFF 0xFF
    uint32_t lba_start;   // 1
    uint32_t lba_count;   // 0xFFFFFFFF (ou real)
} __attribute__((packed)) MBR_Partition_Entry;

typedef struct
{
    uint8_t boot_code[440];
    uint8_t disk_signature[4]; // opcional
    uint8_t reserved[2];
    MBR_Partition_Entry partitions[4]; // só a primeira será usada
    uint16_t signature;                // 0xAA55
} __attribute__((packed)) ProtectiveMBR;

// LibSpecialDrive.h
enum LibSpeicalDrive_BlockFlags
{
    BLOCK_FLAG_IS_REMOVABLE = 1 << 0,
    BLOCK_FLAG_IS_READ_ONLY = 1 << 1
};

struct LibSpeicalDrive_Partition
{
    const char *path;
    const char *mountPoint;
    LibSpeicalDrive_GPT_Partition_Entry partitionMeta;
};

struct LibSpeicalDrive_BlockDevice
{
    const char *path;
    int8_t partitionCount;
    struct LibSpeicalDrive_Partition *partitions;
    int64_t size;
    int8_t flags;
    ProtectiveMBR *signature;
};

struct LibSpecialDrive
{
    struct LibSpeicalDrive_BlockDevice *commonBlockDevices;
    size_t commonBlockDeviceCount;
    struct LibSpeicalDrive_BlockDevice *specialBlockDevices;
    size_t specialBlockDeviceCount;
};

struct LibSpecialFlag
{
    char hex;
    char libspecialDriveName[22];
    uint8_t uuid[16];
    uint8_t version[4];
};

#define LIBSPECIAL_MAGIC_STRING "LIBSPECIALDRIVE_DEVICE"

// Universal

struct LibSpecialFlag *LibSpecialDriverIsSpecial(ProtectiveMBR *ptr);

uint8_t *LibSpecialDriverGenUUID(void);

char *LibSpecialDriverGenUUIDString(uint8_t *uuid);

bool LibSpecialDriverBlockAppend(struct LibSpecialDrive *driver, struct LibSpeicalDrive_BlockDevice **blockDevice);

void LibSpecialDriverDestroyPartition(struct LibSpeicalDrive_Partition *part);

void LibSpecialDriverDestroyBlock(struct LibSpeicalDrive_BlockDevice *blk);

void LibSpecialDriverMapperPartitions(LibSpeicalDrive_GPT_Header *header, uint8_t *partitionBuffer, struct LibSpeicalDrive_BlockDevice *blk);

bool LibSpecialDriverReload(struct LibSpecialDrive *ctx);

void LibSpecialDriverDestroy(struct LibSpecialDrive **ctx);

// System Dependent Functions

char *LibSpecialDriverPartitionPathLookup(const char *path, int partNumber);

void LibSpecialDrivePartitionGetPathMount(struct LibSpeicalDrive_Partition *part);

struct LibSpeicalDrive_Partition *LibSpecialDriverGetPartition(struct LibSpeicalDrive_BlockDevice *blk);

struct LibSpeicalDrive_BlockDevice *LibSpecialDriverGetBlock(const char *path);

struct LibSpecialDrive *LibSpecialDriverGet(void);

bool LibSpecialDriveMark(struct LibSpecialDrive *ctx, int blockNumber);

bool LibSpecialDriveUnmark(struct LibSpecialDrive *ctx, int blockNumber);