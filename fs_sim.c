#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define DISK_FILE "virtual_disk.bin"
#define BLOCK_SIZE 1024
#define TOTAL_BLOCKS 1024  // 1 MB Disk
#define INODE_COUNT 64     // Maksimum dosya sayısı
#define INODE_SIZE 64      // Her inode 64 byte
#define DATA_START_BLOCK 5 // İlk 5 blok metadata için

// Derleyicinin struct boyutunu değiştirmemesi için "packed" kullanıyoruz.
#pragma pack(push, 1)
typedef struct {
    char is_used;          // 1 byte (0: boş, 1: dolu)
    char name[31];         // 31 byte (Dosya adı)
    int size;              // 4 byte (Dosya boyutu)
    int blocks[7];         // 28 byte (7 adet blok index pointer)
} Inode;
#pragma pack(pop)

FILE *disk;

// --- YARDIMCI FONKSİYONLAR ---

void format_disk() {
    printf("Sanal disk bulunamadı. Oluşturuluyor ve formatlanıyor...\n");
    FILE *f = fopen(DISK_FILE, "wb");
    if (!f) {
        printf("Hata: Disk dosyası oluşturulamadı!\n");
        exit(1);
    }

    // Diski sıfırlarla doldur
    char empty_block[BLOCK_SIZE] = {0};
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        fwrite(empty_block, 1, BLOCK_SIZE, f);
    }

    // Superblock (Boş blok haritası - Bitmap) ayarla
    // İlk 5 blok (0,1,2,3,4) metadata olarak işaretlenir.
    char bitmap[TOTAL_BLOCKS] = {0};
    for (int i = 0; i < DATA_START_BLOCK; i++) {
        bitmap[i] = 1; 
    }

    fseek(f, 0, SEEK_SET);
    fwrite(bitmap, 1, TOTAL_BLOCKS, f);
    fclose(f);
}

void read_bitmap(char *bitmap) {
    fseek(disk, 0, SEEK_SET);
    fread(bitmap, 1, TOTAL_BLOCKS, disk);
}

void write_bitmap(const char *bitmap) {
    fseek(disk, 0, SEEK_SET);
    fwrite(bitmap, 1, TOTAL_BLOCKS, disk);
    fflush(disk);
}

void read_inode(int index, Inode *inode) {
    long offset = (1 * BLOCK_SIZE) + (index * INODE_SIZE);
    fseek(disk, offset, SEEK_SET);
    fread(inode, sizeof(Inode), 1, disk);
}

void write_inode(int index, const Inode *inode) {
    long offset = (1 * BLOCK_SIZE) + (index * INODE_SIZE);
    fseek(disk, offset, SEEK_SET);
    fwrite(inode, sizeof(Inode), 1, disk);
    fflush(disk);
}

int find_free_blocks(int count, int *free_blocks) {
    char bitmap[TOTAL_BLOCKS];
    read_bitmap(bitmap);
    
    int found = 0;
    for (int i = DATA_START_BLOCK; i < TOTAL_BLOCKS; i++) {
        if (bitmap[i] == 0) {
            free_blocks[found++] = i;
            if (found == count) return 1; // Yeterli blok bulundu
        }
    }
    return 0; // Yeterli blok yok
}

// --- DOSYA SİSTEMİ İŞLEMLERİ ---

void create_file(const char *filename) {
    Inode inode;
    int free_index = -1;

    for (int i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &inode);
        if (inode.is_used && strcmp(inode.name, filename) == 0) {
            printf("Hata: '%s' adinda bir dosya zaten var.\n", filename);
            return;
        }
        if (!inode.is_used && free_index == -1) {
            free_index = i;
        }
    }

    if (free_index != -1) {
        memset(&inode, 0, sizeof(Inode));
        inode.is_used = 1;
        strncpy(inode.name, filename, 30);
        write_inode(free_index, &inode);
        printf("'%s' basariyla olusturuldu.\n", filename);
    } else {
        printf("Hata: Disk dolu (Maksimum dosya sinirina ulasildi).\n");
    }
}

void list_files() {
    Inode inode;
    int count = 0;
    printf("%-20s | %-15s | %s\n", "Dosya Adi", "Boyut (Byte)", "Kullanilan Bloklar");
    printf("-----------------------------------------------------------------\n");

    for (int i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &inode);
        if (inode.is_used) {
            printf("%-20s | %-15d | [", inode.name, inode.size);
            int first = 1;
            for (int b = 0; b < 7; b++) {
                if (inode.blocks[b] != 0) {
                    if (!first) printf(", ");
                    printf("%d", inode.blocks[b]);
                    first = 0;
                }
            }
            printf("]\n");
            count++;
        }
    }
    if (count == 0) printf("Dosya sistemi bos.\n");
}

void write_file(const char *filename, const char *text) {
    Inode inode;
    int size = strlen(text);
    int required_blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (size == 0) required_blocks = 1;

    if (required_blocks > 7) {
        printf("Hata: Dosya boyutu cok buyuk (Maks. 7 blok).\n");
        return;
    }

    for (int i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &inode);
        if (inode.is_used && strcmp(inode.name, filename) == 0) {
            if (inode.size > 0) {
                printf("Hata: '%s' zaten veri iceriyor. Uzerine yazma desteklenmiyor.\n", filename);
                return;
            }

            int free_blocks[7];
            if (!find_free_blocks(required_blocks, free_blocks)) {
                printf("Hata: Diskte yeterli bos alan yok.\n");
                return;
            }

            char bitmap[TOTAL_BLOCKS];
            read_bitmap(bitmap);

            int bytes_written = 0;
            for (int b = 0; b < required_blocks; b++) {
                int block_num = free_blocks[b];
                bitmap[block_num] = 1; // Bloğu dolu işaretle
                inode.blocks[b] = block_num;

                char chunk[BLOCK_SIZE] = {0};
                int to_write = (size - bytes_written) > BLOCK_SIZE ? BLOCK_SIZE : (size - bytes_written);
                memcpy(chunk, text + bytes_written, to_write);

                fseek(disk, block_num * BLOCK_SIZE, SEEK_SET);
                fwrite(chunk, 1, BLOCK_SIZE, disk);
                bytes_written += to_write;
            }

            write_bitmap(bitmap);
            inode.size = size;
            write_inode(i, &inode);
            printf("'%s' dosyasina %d byte yazildi.\n", filename, size);
            return;
        }
    }
    printf("Hata: '%s' bulunamadi.\n", filename);
}

void read_file(const char *filename) {
    Inode inode;
    for (int i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &inode);
        if (inode.is_used && strcmp(inode.name, filename) == 0) {
            printf("--- %s Icerigi ---\n", filename);
            int bytes_left = inode.size;
            
            for (int b = 0; b < 7 && inode.blocks[b] != 0; b++) {
                int block_num = inode.blocks[b];
                char buffer[BLOCK_SIZE + 1] = {0};
                int to_read = bytes_left > BLOCK_SIZE ? BLOCK_SIZE : bytes_left;
                
                fseek(disk, block_num * BLOCK_SIZE, SEEK_SET);
                fread(buffer, 1, to_read, disk);
                printf("%s", buffer);
                bytes_left -= to_read;
            }
            printf("\n-------------------------\n");
            return;
        }
    }
    printf("Hata: '%s' bulunamadi.\n", filename);
}

void delete_file(const char *filename) {
    Inode inode;
    for (int i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &inode);
        if (inode.is_used && strcmp(inode.name, filename) == 0) {
            char bitmap[TOTAL_BLOCKS];
            read_bitmap(bitmap);

            // Blokları serbest bırak
            for (int b = 0; b < 7; b++) {
                if (inode.blocks[b] != 0) {
                    bitmap[inode.blocks[b]] = 0;
                }
            }
            write_bitmap(bitmap);

            // Inode'u sıfırla
            memset(&inode, 0, sizeof(Inode));
            write_inode(i, &inode);

            printf("'%s' silindi.\n", filename);
            return;
        }
    }
    printf("Hata: '%s' bulunamadi.\n", filename);
}

void fs_status() {
    char bitmap[TOTAL_BLOCKS];
    read_bitmap(bitmap);

    int free_blocks = 0, used_blocks = 0;
    for (int i = DATA_START_BLOCK; i < TOTAL_BLOCKS; i++) {
        if (bitmap[i] == 0) free_blocks++;
        else used_blocks++;
    }

    int used_inodes = 0;
    Inode inode;
    for (int i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &inode);
        if (inode.is_used) used_inodes++;
    }

    printf("\n--- Dosya Sistemi Durumu ---\n");
    printf("Toplam Kapasite  : %d Bytes (%d Blok)\n", TOTAL_BLOCKS * BLOCK_SIZE, TOTAL_BLOCKS);
    printf("Veri Bloklari    : %d Blok\n", TOTAL_BLOCKS - DATA_START_BLOCK);
    printf("Bos Bloklar      : %d\n", free_blocks);
    printf("Dolu Bloklar     : %d\n", used_blocks);
    printf("Kullanilan Inode : %d / %d\n", used_inodes, INODE_COUNT);
    printf("----------------------------\n\n");
}

// --- CLI (KOMUT SATIRI ARAYÜZÜ) ---

int main() {
    FILE *test_disk = fopen(DISK_FILE, "rb");
    if (!test_disk) {
        format_disk();
    } else {
        fclose(test_disk);
    }

    disk = fopen(DISK_FILE, "r+b");
    if (!disk) {
        printf("Disk acilamadi!\n");
        return 1;
    }

    printf("Sanal Dosya Sistemi Baslatildi. Komutlar: create, ls, write, read, rm, status, exit\n");

    char input[1024];
    char cmd[16], arg1[32], arg2[1024];

    while (1) {
        printf("FS> ");
        if (!fgets(input, sizeof(input), stdin)) break;

        // Satır sonunu temizle
        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) continue;

        cmd[0] = arg1[0] = arg2[0] = '\0';
        int parsed = sscanf(input, "%15s %31s %[^\n]", cmd, arg1, arg2);

        if (strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "create") == 0 && parsed >= 2) {
            create_file(arg1);
        } else if (strcmp(cmd, "ls") == 0) {
            list_files();
        } else if (strcmp(cmd, "write") == 0 && parsed >= 3) {
            write_file(arg1, arg2);
        } else if (strcmp(cmd, "read") == 0 && parsed >= 2) {
            read_file(arg1);
        } else if (strcmp(cmd, "rm") == 0 && parsed >= 2) {
            delete_file(arg1);
        } else if (strcmp(cmd, "status") == 0) {
            fs_status();
        } else {
            printf("Hatali veya eksik komut. Kullanim:\n");
            printf("  create <dosya_adi>\n");
            printf("  ls\n");
            printf("  write <dosya_adi> <veri metni>\n");
            printf("  read <dosya_adi>\n");
            printf("  rm <dosya_adi>\n");
            printf("  status\n");
            printf("  exit\n");
        }
    }

    fclose(disk);
    return 0;
}