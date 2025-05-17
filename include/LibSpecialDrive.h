#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(_MSC_VER)
#define strdup _strdup
#define PACKED_BEGIN __pragma(pack(push, 1))
#define PACKED_END __pragma(pack(pop))
#define PACKED
#define EXPORT __declspec(dllexport)
#else
#define PACKED_BEGIN
#define PACKED_END
#define PACKED __attribute__((packed))
#define EXPORT
#endif

// =====================================================================================
// Constantes da GPT
// =====================================================================================
#define GPT_SIGNATURE "EFI PART"

// =====================================================================================
// Estruturas GPT (GUID Partition Table)
// =====================================================================================

PACKED_BEGIN

typedef struct PACKED
{
    uint8_t partitionTypeGuid[16];
    uint8_t uniquePartitionGuid[16];
    uint64_t startingLba;
    uint64_t endingLba;
    uint64_t attributes;
    uint16_t name[36]; // UTF-16LE
} LibSpecialDrive_GPT_Partition_Entry;

typedef struct PACKED
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
} LibSpecialDrive_GPT_Header;

// =====================================================================================
// Estruturas MBR (Master Boot Record)
// =====================================================================================

typedef struct PACKED
{
    uint8_t status;
    uint8_t firstCHS[3];
    uint8_t partitionType;
    uint8_t lastCHS[3];
    uint32_t firstLBA;
    uint32_t sectors;
} LibSpecialDrive_MBR_Partition_Entry;

typedef struct PACKED
{
    uint8_t boot_code[440];
    uint8_t diskSignature[4];
    uint8_t reserved[2];
    LibSpecialDrive_MBR_Partition_Entry partitions[4];
    uint16_t signature;
} LibSpecialDrive_Protective_MBR;

PACKED_END

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
    LibSpecialDrive_GPT_Partition_Entry gpt;
    LibSpecialDrive_MBR_Partition_Entry mbr;
};

typedef struct
{
    char *path;
    char *mountPoint;
    uint32_t *lbaSize;
    uint64_t freeSpace;
    union LibSpecialDrive_PartitionMeta partitionMeta;
} LibSpecialDrive_Partition;

typedef struct
{
    enum LibSpecialDrive_PartitionType type;
    LibSpecialDrive_Partition *partitions;
    uint32_t lbaSize;
    uint64_t size;
    int8_t partitionCount;
    int8_t flags;
    char *path;
    LibSpecialDrive_Protective_MBR *signature;
} LibSpecialDrive_BlockDevice;

typedef struct
{
    LibSpecialDrive_BlockDevice *commonBlockDevices;
    size_t commonBlockDeviceCount;
    LibSpecialDrive_BlockDevice *specialBlockDevices;
    size_t specialBlockDeviceCount;
} LibSpecialDrive;

typedef struct PACKED
{
    uint8_t hex;
    char libspecialDriveName[22];
    uint8_t uuid[16];
    uint8_t version[4];
} LibSpecialDrive_Flag;

#ifndef _WIN32
typedef int LibSpecialDrive_DeviceHandle;
#define DEVICE_INVALID -1
#else
#include <windows.h>
typedef HANDLE LibSpecialDrive_DeviceHandle;
#define DEVICE_INVALID INVALID_HANDLE_VALUE
#endif

enum LibSpecialDrive_DeviceHandle_Flags
{
    DEVICE_FLAG_READ = 1 << 0,
    DEVICE_FLAG_WRITE = 1 << 1,
    DEVICE_FLAG_SILENCE = 1 << 3
};

#define LIBSPECIAL_MAGIC_STRING "LIBSPECIALDRIVE_DEVICE"

#define LIBSPECIAL_FLAG {0xFF, LIBSPECIAL_MAGIC_STRING, {0}, {0, 0, 0, 1}}

// =====================================================================================
// Funções Universais da Biblioteca
// =====================================================================================
/// Internas
LibSpecialDrive_Flag *LibSpecialDriverIsSpecial(LibSpecialDrive_Protective_MBR *ptr);
void LibSpecialDriverGenUUID(uint8_t *uuid);
bool LibSpecialDriverBlockAppend(LibSpecialDrive *driver, LibSpecialDrive_BlockDevice **blockDevice);
void LibSpecialDriverDestroyPartition(LibSpecialDrive_Partition *part);
void LibSpecialDriverDestroyBlock(LibSpecialDrive_BlockDevice *blk);
void LibSpecialDriverMapperPartitionsMBR(LibSpecialDrive_BlockDevice *blk);
void LibSpecialDriverMapperPartitionsGPT(LibSpecialDrive_GPT_Header *header, uint8_t *partitionBuffer, LibSpecialDrive_BlockDevice *blk);
LibSpecialDrive_Partition *LibSpecialDriverGetPartition(LibSpecialDrive_BlockDevice *blk, LibSpecialDrive_DeviceHandle device);
LibSpecialDrive_BlockDevice *LibSpecialDriverGetBlock(const char *path);

/// Externas
EXPORT char *LibSpecialDriverGenUUIDString(uint8_t *uuid);
EXPORT bool LibSpecialDriverReload(LibSpecialDrive *ctx);
EXPORT void LibSpecialDriverDestroy(LibSpecialDrive **ctx);
EXPORT bool LibSpecialDriveMark(LibSpecialDrive *ctx, int blockNumber);
EXPORT bool LibSpecialDriveUnmark(LibSpecialDrive *ctx, int blockNumber);
EXPORT LibSpecialDrive *LibSpecialDriverGet(void);

// =====================================================================================
// Funções de Sistema Dependente
// =====================================================================================
void LibSpecialDriverDiretoryFreeSpaceLookup(LibSpecialDrive_Partition *part);
char *LibSpecialDriverPartitionPathLookup(const char *path, int partNumber);
void LibSpecialDrivePartitionGetPathMount(LibSpecialDrive_Partition *part, enum LibSpecialDrive_PartitionType type);
bool LibSpecialDriveLookUpSizes(LibSpecialDrive_DeviceHandle device, LibSpecialDrive_BlockDevice *blk);
bool LibSpecialDriveLookUpIsRemovable(LibSpecialDrive_DeviceHandle device, LibSpecialDrive_BlockDevice *blk);
LibSpecialDrive_DeviceHandle LibSpecialDriveOpenDevice(const char *path, enum LibSpecialDrive_DeviceHandle_Flags flags);
bool LibSpecialDriveSeek(LibSpecialDrive_DeviceHandle device, int64_t padding);
int64_t LibSpecialDriveRead(LibSpecialDrive_DeviceHandle device, int64_t len, uint8_t *target);
int64_t LibSpecialDriveWrite(LibSpecialDrive_DeviceHandle device, int64_t len, const uint8_t *soruce);
void LibSpecialDriveCloseDevice(LibSpecialDrive_DeviceHandle device);
