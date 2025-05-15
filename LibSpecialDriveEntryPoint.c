#include <LibSpecialDrive.h>
#include <stdio.h>
#include <stdlib.h>

void listPartition(LibSpecialDrive_BlockDevice *blk)
{
    if (!blk || blk->partitionCount <= 0)
        return;

    if (blk->type == PARTITION_TYPE_GPT)
    {
        printf("\tGPT\n");
    }
    else
    {
        printf("\tMBR\n");
    }

    for (int i = 0; i < blk->partitionCount; i++)
    {
        LibSpecialDrive_Partition *part = &blk->partitions[i];
        printf("\tPartition %d\n\t\tMount Point: %s\n", i, (part->mountPoint ? part->mountPoint : "None"));

        if (blk->type == PARTITION_TYPE_GPT)
        {
            char *uuidStr = LibSpecialDriverGenUUIDString(blk->partitions[i].partitionMeta.gpt.uniquePartitionGuid);
            printf("\t\tUUID: %s\n", uuidStr);
            free(uuidStr);
        }
        printf("\t\tVolume Path: %s\n", (part->path ? part->path : "None"));
    }
}

void list(LibSpecialDrive *lb)
{
    if (!lb)
    {
        return;
    }
    if (lb->commonBlockDeviceCount >= 0)
        for (size_t i = 0; i < lb->commonBlockDeviceCount; i++)
        {
            LibSpecialDrive_BlockDevice *bd = &lb->commonBlockDevices[i];
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
            LibSpecialDrive_BlockDevice *bd = &lb->specialBlockDevices[i];
            LibSpecialDrive_Flag *flag = (LibSpecialDrive_Flag *)bd->signature->boot_code;
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
    LibSpecialDrive *lb = LibSpecialDriverGet();
    // Exibir resultados
    list(lb);
    // printf("%d\n\n", LibSpecialDriveMark(lb, 2));
    // list(lb);
    // printf("%d\n\n", LibSpecialDriveUnmark(lb, 0));
    // list(lb);

    LibSpecialDriverDestroy(&lb);

    return 0;
}