#include <LibSpecialDrive.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void listPartition(LibSpecialDrive_BlockDevice *blk)
{
    if (!blk || blk->partitionCount <= 0)
        return;

    printf("\t%s\n", blk->type == PARTITION_TYPE_GPT ? "GPT" : "MBR");

    for (int i = 0; i < blk->partitionCount; i++)
    {
        LibSpecialDrive_Partition *part = &blk->partitions[i];
        printf("\tPartition %d\n\t\tMount Point: %s\n", i, (part->mountPoint ? part->mountPoint : "None"));

        if (blk->type == PARTITION_TYPE_GPT)
        {
            char *uuidStr = LibSpecialDriverGenUUIDString(part->partitionMeta.gpt.uniquePartitionGuid);
            printf("\t\tUUID: %s\n", uuidStr);
            free(uuidStr);
        }

        printf("\t\tVolume Path: %s\n\t\tFree Space: %"PRIu64" bytes\n", (part->path ? part->path : "None"), part->freeSpace);
    }
}

void listBlock(LibSpecialDrive *lb, bool listPart, bool hiddenBlock)
{
    if (!lb)
        return;

    if (lb->commonBlockDeviceCount > 0)
    {
        for (size_t i = 0; i < lb->commonBlockDeviceCount; i++)
        {
            LibSpecialDrive_BlockDevice *bd = &lb->commonBlockDevices[i];
            if (!hiddenBlock)
                printf("Common Device %zu: %s, Size: %lld bytes, Removable: %s\n",
                       i, bd->path, bd->size,
                       (bd->flags & BLOCK_FLAG_IS_REMOVABLE) ? "Yes" : "No");

            if (listPart)
                listPartition(bd);
        }
    }

    if (lb->specialBlockDeviceCount > 0)
    {
        for (size_t i = 0; i < lb->specialBlockDeviceCount; i++)
        {
            LibSpecialDrive_BlockDevice *bd = &lb->specialBlockDevices[i];
            LibSpecialDrive_Flag *flag = (LibSpecialDrive_Flag *)bd->signature->boot_code;
            char *uuidStr = LibSpecialDriverGenUUIDString(flag->uuid);

            if (!hiddenBlock)
                printf("Special Device %zu: %s, Size: %"PRIu64" bytes, Removable: %s\n\tSpecial UUID:%s\n",
                       i, bd->path, bd->size,
                       (bd->flags & BLOCK_FLAG_IS_REMOVABLE) ? "Yes" : "No", uuidStr);

            if (listPart)
                listPartition(bd);

            free(uuidStr);
        }
    }
}

void printHelp(const char *progName)
{
    printf("Uso: %s [opções]\n", progName);
    printf("Opções:\n");
    printf("  -a             Listar tudo");
    printf("  -b             Listar apenas blocos\n");
    printf("  -p             Listar apenas partições\n");
    printf("  -r             Recarrega os dispositivos\n");
    printf("  -m <id>        Marcar bloco comum com índice <id> como especial\n");
    printf("  -u <id>        Desmarcar bloco especial com índice <id>\n");
    printf("  -h             Mostrar esta ajuda\n");
}

int main(int argc, const char *argv[])
{
    if (argc == 1)
    {
        printHelp(argv[0]);
        return 0;
    }

    LibSpecialDrive *lb = LibSpecialDriverGet();

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-a") == 0)
        {
            listBlock(lb, true, false);
        }
        else if (strcmp(argv[i], "-b") == 0)
        {
            listBlock(lb, false, false);
        }
        else if (strcmp(argv[i], "-p") == 0)
        {
            listBlock(lb, true, true);
        }
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
        {
            int id = atoi(argv[++i]);
            printf("Marca: %s\n", LibSpecialDriveMark(lb, id) == 1 ? "Sucesso" : "Falha");
        }
        else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc)
        {
            int id = atoi(argv[++i]);
            printf("Desmarca: %s\n", LibSpecialDriveUnmark(lb, id) == 1 ? "Sucesso" : "Falha");
        }
        else if (strcmp(argv[i], "-h") == 0)
        {
            printHelp(argv[0]);
        }
        else if (strcmp(argv[i], "-r") == 0)
        {
            LibSpecialDriverReload(lb);
        }
        else
        {
            printf("Opção inválida: %s\n", argv[i]);
            printHelp(argv[0]);
            break;
        }
    }

    LibSpecialDriverDestroy(&lb);
    return 0;
}
