#include <LibSpecialDrive.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Função Auxiliar de Formatação de Tamanho ---

static void formatBytes(uint64_t bytes, char *buffer, size_t bufSize) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  double size = (double)bytes;
  size_t unitIdx = 0;

  while (size >= 1024.0 && unitIdx < 4) {
    size /= 1024.0;
    unitIdx++;
  }

  snprintf(buffer, bufSize, "%.2f %s (%" PRIu64 " bytes)", size, units[unitIdx],
           bytes);
}

// --- Listagem de Partições ---

void listPartition(LibSpecialDrive_BlockDevice *blk) {
  if (!blk || blk->partitionCount <= 0 || !blk->partitions)
    return;

  printf("\tTabela de Partições: %s\n",
         (blk->type == PARTITION_TYPE_GPT) ? "GPT" : "MBR");

  for (int i = 0; i < blk->partitionCount; i++) {
    LibSpecialDrive_Partition *part = &blk->partitions[i];
    const char *mountPoint = (part->mountPoint && part->mountPoint[0] != '\0')
                                 ? part->mountPoint
                                 : "N/A";
    const char *volumePath =
        (part->path && part->path[0] != '\0') ? part->path : "N/A";

    char freeSpaceFormatted[64];
    formatBytes(part->freeSpace, freeSpaceFormatted,
                sizeof(freeSpaceFormatted));

    printf("\tPartição %d:\n", i);
    printf("\t\tPonto de Montagem: %s\n", mountPoint);

    if (blk->type == PARTITION_TYPE_GPT) {
      char *uuidStr = LibSpecialDriveGenUUIDString(
          part->partitionMeta.gpt.uniquePartitionGuid);
      printf("\t\tUUID da Partição  : %s\n", uuidStr ? uuidStr : "N/A");
      if (uuidStr) {
        LibSpecialDriveFree(uuidStr);
      }
    }

    printf("\t\tCaminho do Volume : %s\n", volumePath);
    printf("\t\tEspaço Livre      : %s\n", freeSpaceFormatted);
  }
}

// --- Listagem de Dispositivos de Bloco ---

void listBlock(LibSpecialDrive *lb, bool listPart, bool hiddenBlock) {
  if (!lb)
    return;

  if (lb->commonBlockDevices && lb->commonBlockDeviceCount > 0) {
    for (size_t i = 0; i < lb->commonBlockDeviceCount; i++) {
      LibSpecialDrive_BlockDevice *bd = &lb->commonBlockDevices[i];

      if (!hiddenBlock) {
        char sizeFormatted[64];
        formatBytes(bd->size, sizeFormatted, sizeof(sizeFormatted));
        const char *removableStr =
            (bd->flags & BLOCK_FLAG_IS_REMOVABLE) ? "Sim" : "Não";
        const char *pathStr = (bd->path) ? bd->path : "N/A";

        printf("Dispositivo Comum %zu: %s | Tamanho: %s | Removível: %s\n", i,
               pathStr, sizeFormatted, removableStr);
      }

      if (listPart) {
        listPartition(bd);
      }
    }
  }

  if (lb->specialBlockDevices && lb->specialBlockDeviceCount > 0) {
    for (size_t i = 0; i < lb->specialBlockDeviceCount; i++) {
      LibSpecialDrive_BlockDevice *bd = &lb->specialBlockDevices[i];

      if (!hiddenBlock) {
        char sizeFormatted[64];
        formatBytes(bd->size, sizeFormatted, sizeof(sizeFormatted));
        const char *removableStr =
            (bd->flags & BLOCK_FLAG_IS_REMOVABLE) ? "Sim" : "Não";
        const char *pathStr = (bd->path) ? bd->path : "N/A";

        char *uuidStr = NULL;
        if (bd->signature) {
          LibSpecialDrive_Flag *flag =
              (LibSpecialDrive_Flag *)bd->signature->boot_code;
          uuidStr = LibSpecialDriveGenUUIDString(flag->uuid);
        }

        printf("Dispositivo Especial %zu: %s | Tamanho: %s | Removível: %s\n",
               i, pathStr, sizeFormatted, removableStr);
        printf("\tUUID Especial: %s\n", uuidStr ? uuidStr : "N/A");

        if (uuidStr) {
          LibSpecialDriveFree(uuidStr);
        }
      }

      if (listPart) {
        listPartition(bd);
      }
    }
  }
}

// --- Exibição de Ajuda ---

void printHelp(const char *progName) {
  printf("Uso: %s [opções]\n\n", progName);
  printf("Opções:\n");
  printf("  -a             Listar blocos e partições\n");
  printf("  -b             Listar apenas os dispositivos de bloco\n");
  printf("  -p             Listar apenas as partições\n");
  printf("  -r             Recarregar os dispositivos\n");
  printf("  -m <id>        Marcar bloco comum com índice <id> como especial\n");
  printf("  -u <id>        Desmarcar bloco especial com índice <id>\n");
  printf("  -h             Mostrar esta ajuda\n");
}

// --- Conversão Segura de Argumento Numérico ---

static bool parseIndex(const char *str, int *outVal) {
  if (!str || !outVal)
    return false;

  char *endPtr = NULL;
  long val = strtol(str, &endPtr, 10);

  if (endPtr == str || *endPtr != '\0' || val < 0) {
    return false;
  }

  *outVal = (int)val;
  return true;
}

// --- Ponto de Entrada Principal ---

int main(int argc, const char *argv[]) {
  if (argc <= 1) {
    printHelp(argv[0]);
    return 0;
  }

  LibSpecialDrive *lb = LibSpecialDriveGet();
  if (!lb) {
    fprintf(stderr, "Erro ao inicializar a biblioteca LibSpecialDrive.\n");
    return 1;
  }

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-a") == 0) {
      listBlock(lb, true, false);
    } else if (strcmp(argv[i], "-b") == 0) {
      listBlock(lb, false, false);
    } else if (strcmp(argv[i], "-p") == 0) {
      listBlock(lb, true, true);
    } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
      int id = 0;
      if (parseIndex(argv[++i], &id)) {
        bool ok = LibSpecialDriveMark(lb, id);
        printf("Operação Marcação (ID %d): %s\n", id, ok ? "Sucesso" : "Falha");
      } else {
        fprintf(stderr, "Índice de dispositivo inválido fornecido para '-m'\n");
      }
    } else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
      int id = 0;
      if (parseIndex(argv[++i], &id)) {
        bool ok = LibSpecialDriveUnmark(lb, id);
        printf("Operação Desmarcação (ID %d): %s\n", id,
               ok ? "Sucesso" : "Falha");
      } else {
        fprintf(stderr, "Índice de dispositivo inválido fornecido para '-u'\n");
      }
    } else if (strcmp(argv[i], "-r") == 0) {
      bool ok = LibSpecialDriveReload(lb);
      printf("Recarregamento de dispositivos: %s\n", ok ? "Sucesso" : "Falha");
    } else if (strcmp(argv[i], "-h") == 0) {
      printHelp(argv[0]);
    } else {
      fprintf(stderr, "Opção não reconhecida: %s\n\n", argv[i]);
      printHelp(argv[0]);
      break;
    }
  }

  LibSpecialDriveDestroy(&lb);
  return 0;
}