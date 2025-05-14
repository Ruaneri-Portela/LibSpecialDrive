#include <LibSpecialDrive.h>
#include <stdio.h>
#include <stdlib.h>

void listPartition(struct LibSpeicalDrive_BlockDevice *blk)
{
    if (!blk || blk->partitionCount <= 0)
        return;

    for (int i = 0; i < blk->partitionCount; i++)
    {
        struct LibSpeicalDrive_Partition *part = &blk->partitions[i];
        char *uuidStr = LibSpecialDriverGenUUIDString(blk->partitions[i].partitionMeta.unique_partition_guid);
        printf("\tPartition %d\n\t\tMount Point: %s\n\t\tUUID: %s\n\t\tVolume Path: %.48s\n", i, (part->mountPoint ? part->mountPoint : "None"), uuidStr, (part->path ? part->path : "None"));
        free(uuidStr); // LibSpecialDriverGenUUIDString deve alocar memória para o UUID, então liberamos aqui
    }
}

void list(struct LibSpecialDrive *lb)
{
    if (!lb)
    {
        return;
    }
    if (lb->commonBlockDeviceCount >= 0)
        for (size_t i = 0; i < lb->commonBlockDeviceCount; i++)
        {
            struct LibSpeicalDrive_BlockDevice *bd = &lb->commonBlockDevices[i];
            printf(
                "Common Device %zu: %s, Size: %lld bytes, Removable: %s\n",
                i,
                bd->path,
                bd->size,
                (bd->flags & BLOCK_FLAG_IS_REMOVABLE) ? "Yes" : "No");
            listPartition(bd);
        }
    else
    {
        printf("No common block devices found.\n");
    }
    if (lb->specialBlockDeviceCount >= 0)
        for (size_t i = 0; i < lb->specialBlockDeviceCount; i++)
        {
            struct LibSpeicalDrive_BlockDevice *bd = &lb->specialBlockDevices[i];
            struct LibSpecialFlag *flag = (struct LibSpecialFlag *)bd->signature->boot_code;
            char *uuidStr = LibSpecialDriverGenUUIDString(flag->uuid);
            printf(
                "Special Device %zu: %s, Size: %lld bytes, Removable: %s\n\tSpecial UUID:%s\n",
                i,
                bd->path,
                bd->size,
                (bd->flags & BLOCK_FLAG_IS_REMOVABLE) ? "Yes" : "No",
                uuidStr);
            listPartition(bd);
            free(uuidStr); // LibSpecialDriverGenUUIDString deve alocar memória para o UUID, então liberamos aqui
        }
    else
    {
        printf("No special block devices found.\n");
    }
}

int main()
{
    struct LibSpecialDrive *lb = LibSpecialDriverGet();
    // Exibir resultados
    list(lb);
    printf("%d\n\n", LibSpecialDriveMark(lb, 0));
    list(lb);
    printf("%d\n\n", LibSpecialDriveUnmark(lb, 0));
    list(lb);

    LibSpecialDriverDestroy(&lb);

    return 0;
}