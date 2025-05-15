#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =====================================================================================
// Constantes da GPT
// =====================================================================================
#define GPT_SIGNATURE "EFI PART"

// =====================================================================================
// Estruturas GPT (GUID Partition Table)
// =====================================================================================
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

// =====================================================================================
// Estruturas MBR (Master Boot Record)
// =====================================================================================
typedef struct
{
    uint8_t status;      // 0x00
    uint8_t chsFirst[3]; // 0xFF 0xFF 0xFF
    uint8_t type;        // 0xEE (GPT protective)
    uint8_t chsLast[3];  // 0xFF 0xFF 0xFF
    uint32_t lbaStart;   // 1
    uint32_t lbaCount;   // 0xFFFFFFFF (ou real)
} __attribute__((packed)) LibSpecialDrive_MBR_Partition_Entry;

typedef struct
{
    uint8_t boot_code[440];
    uint8_t diskSignature[4]; // opcional
    uint8_t reserved[2];
    LibSpecialDrive_MBR_Partition_Entry partitions[4]; // só a primeira será usada
    uint16_t signature;                                // 0xAA55
} __attribute__((packed)) LibSpecialDrive_Protective_MBR;

// =====================================================================================
// Enums de Flags e Tipos
// =====================================================================================
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

// =====================================================================================
// Estruturas Principais
// =====================================================================================
union LibSpecialDrive_PartitionMeta
{
    LibSpeicalDrive_GPT_Partition_Entry gpt;
    LibSpecialDrive_MBR_Partition_Entry mbr;
};

typedef struct
{
    const char *path;
    const char *mountPoint;
    int64_t *lbaSize;
    union LibSpecialDrive_PartitionMeta partitionMeta;
} LibSpecialDrive_Partition;

typedef struct
{
    enum LibSpecialDrive_PartitionType type;
    LibSpecialDrive_Partition *partitions;
    int8_t partitionCount;
    char *path;
    int64_t lbaSize;
    int64_t size;
    int8_t flags;
    LibSpecialDrive_Protective_MBR *signature;
} LibSpecialDrive_BlockDevice;

typedef struct
{
    LibSpecialDrive_BlockDevice *commonBlockDevices;
    size_t commonBlockDeviceCount;
    LibSpecialDrive_BlockDevice *specialBlockDevices;
    size_t specialBlockDeviceCount;
} LibSpecialDrive;

typedef struct
{
    char hex;
    char libspecialDriveName[22];
    uint8_t uuid[16];
    uint8_t version[4];
} LibSpecialDrive_Flag;

#define LIBSPECIAL_MAGIC_STRING "LIBSPECIALDRIVE_DEVICE"

// =====================================================================================
// Funções Universais da Biblioteca
// =====================================================================================
LibSpecialDrive_Flag *LibSpecialDriverIsSpecial(LibSpecialDrive_Protective_MBR *ptr);
uint8_t *LibSpecialDriverGenUUID(void);
char *LibSpecialDriverGenUUIDString(uint8_t *uuid);
bool LibSpecialDriverBlockAppend(LibSpecialDrive *driver, LibSpecialDrive_BlockDevice **blockDevice);
void LibSpecialDriverDestroyPartition(LibSpecialDrive_Partition *part);
void LibSpecialDriverDestroyBlock(LibSpecialDrive_BlockDevice *blk);
void LibSpecialDriverMapperPartitionsMBR(LibSpecialDrive_BlockDevice *blk);
void LibSpecialDriverMapperPartitionsGPT(LibSpeicalDrive_GPT_Header *header, uint8_t *partitionBuffer, LibSpecialDrive_BlockDevice *blk);
bool LibSpecialDriverReload(LibSpecialDrive *ctx);
void LibSpecialDriverDestroy(LibSpecialDrive **ctx);

// =====================================================================================
// Funções de Sistema Dependente
// =====================================================================================
char *LibSpecialDriverPartitionPathLookup(const char *path, int partNumber);
void LibSpecialDrivePartitionGetPathMount(LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type);
#ifndef _WIN32
LibSpecialDrive_Partition *LibSpecialDriverGetPartition(LibSpecialDrive_BlockDevice *blk, int fd);
#else
#include <windows.h>
LibSpecialDrive_Partition *LibSpecialDriverGetPartition(LibSpecialDrive_BlockDevice *blk, HANDLE hDevice);
#endif
LibSpecialDrive_BlockDevice *LibSpecialDriverGetBlock(const char *path);
LibSpecialDrive *LibSpecialDriverGet(void);
bool LibSpecialDriveMark(LibSpecialDrive *ctx, int blockNumber);
bool LibSpecialDriveUnmark(LibSpecialDrive *ctx, int blockNumber);