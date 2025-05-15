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
    uint8_t partitionTypeGuid[16];
    uint8_t uniquePartitionGuid[16];
    uint64_t startingLba;
    uint64_t endingLba;
    uint64_t attributes;
    uint16_t name[36]; // UTF-16LE
} __attribute__((packed)) LibSpeicalDrive_GPT_Partition_Entry;

typedef struct
{
    uint64_t signature;
    uint32_t revision;
    uint32_t headerSize;
    uint32_t crc32;
    uint32_t reserved;
    uint64_t currentLba;
    uint64_t backupLba;
    uint64_t firstUsableLba;
    uint64_t lastUsableLba;
    uint8_t diskGuid[16];
    uint64_t partitionEntriesLba;
    uint32_t numPartitionEntries;
    uint32_t sizeOfPartitionEntry;
    uint32_t partitionEntriesCrc32;
} __attribute__((packed)) LibSpeicalDrive_GPT_Header;

// MBR (Master Boot Record)
typedef struct
{
    uint8_t status;      // 0x00
    uint8_t chsFirst[3]; // 0xFF 0xFF 0xFF
    uint8_t type;        // 0xEE (GPT protective)
    uint8_t chsLast[3];  // 0xFF 0xFF 0xFF
    uint32_t lbaStart;   // 1
    uint32_t lbaCount;   // 0xFFFFFFFF (ou real)
} __attribute__((packed)) MBR_Partition_Entry;

typedef struct
{
    uint8_t boot_code[440];
    uint8_t diskSignature[4]; // opcional
    uint8_t reserved[2];
    MBR_Partition_Entry partitions[4]; // só a primeira será usada
    uint16_t signature;                // 0xAA55
} __attribute__((packed)) ProtectiveMBR;

// LibSpecialDrive.h
enum LibSpecialDrive_BlockFlags
{
    BLOCK_FLAG_IS_REMOVABLE = 1 << 0,
    BLOCK_FLAG_IS_READ_ONLY = 1 << 1
};

enum LibSpecialDrive_PartitionType
{
    PARTITION_TYPE_UNKNOWN = 0,
    PARTITION_TYPE_GPT = 1,
    PARTITION_TYPE_MBR = 2
};

union LibSpecialDrive_PartitionMeta
{
    LibSpeicalDrive_GPT_Partition_Entry gpt;
    MBR_Partition_Entry mbr;
};

struct LibSpecialDrive_Partition
{
    const char *path;
    const char *mountPoint;
    union LibSpecialDrive_PartitionMeta partitionMeta;
};

struct LibSpecialDrive_BlockDevice
{
    const char *path;
    int8_t partitionCount;
    enum LibSpecialDrive_PartitionType type;
    struct LibSpecialDrive_Partition *partitions;
    int64_t size;
    int8_t flags;
    ProtectiveMBR *signature;
};

struct LibSpecialDrive
{
    struct LibSpecialDrive_BlockDevice *commonBlockDevices;
    size_t commonBlockDeviceCount;
    struct LibSpecialDrive_BlockDevice *specialBlockDevices;
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

bool LibSpecialDriverBlockAppend(struct LibSpecialDrive *driver, struct LibSpecialDrive_BlockDevice **blockDevice);

void LibSpecialDriverDestroyPartition(struct LibSpecialDrive_Partition *part);

void LibSpecialDriverDestroyBlock(struct LibSpecialDrive_BlockDevice *blk);

void LibSpecialDriverMapperPartitionsMBR(struct LibSpecialDrive_BlockDevice *blk);

void LibSpecialDriverMapperPartitionsGPT(LibSpeicalDrive_GPT_Header *header, uint8_t *partitionBuffer, struct LibSpecialDrive_BlockDevice *blk);

bool LibSpecialDriverReload(struct LibSpecialDrive *ctx);

void LibSpecialDriverDestroy(struct LibSpecialDrive **ctx);

// System Dependent Functions

char *LibSpecialDriverPartitionPathLookup(const char *path, int partNumber);

void LibSpecialDrivePartitionGetPathMount(struct LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type);

struct LibSpecialDrive_Partition *LibSpecialDriverGetPartition(struct LibSpecialDrive_BlockDevice *blk);

struct LibSpecialDrive_BlockDevice *LibSpecialDriverGetBlock(const char *path);

struct LibSpecialDrive *LibSpecialDriverGet(void);

bool LibSpecialDriveMark(struct LibSpecialDrive *ctx, int blockNumber);

bool LibSpecialDriveUnmark(struct LibSpecialDrive *ctx, int blockNumber);