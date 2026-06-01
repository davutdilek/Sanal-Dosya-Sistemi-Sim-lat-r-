/*
 * Mini Dosya Sistemi Simulatoru
 *
 * Disk Duzeni:
 *   Blok 0      : Bitmap (bos/dolu blok haritasi, 1 byte/blok)
 *   Blok 1-4    : Inode tablosu (64 inode x 64 byte = 4096 byte = 4 blok)
 *   Blok 5-7    : Dizin tablosu (64 girdi x 48 byte = 3072 byte = 3 blok)
 *   Blok 8+     : Veri bloklari
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BLOCK_SIZE        1024
#define TOTAL_BLOCKS      1024
#define INODE_COUNT       64
#define INODE_SIZE        64
#define DIR_ENTRY_SIZE    48
#define DIR_COUNT         64
#define INODE_START_BLOCK 1
#define DIR_START_BLOCK   5
#define DATA_START_BLOCK  8
#define MAX_FILE_BYTES    (7 * BLOCK_SIZE)

#define DISK_FILE         "virtual_disk.bin"

/* ===== STRUCT'LAR ===== */

#pragma pack(push, 1)
typedef struct {
    char is_used;
    int  size;
    int  blocks[7];
    char reserved[31];
} Inode;

typedef struct {
    char is_used;
    char name[43];
    int  inode_index;
} DirEntry;
#pragma pack(pop)

FILE *disk;

/* ===== DUSUK SEVIYE I/O ===== */

static void read_bitmap(char *bm) {
    fseek(disk, 0, SEEK_SET);
    fread(bm, 1, TOTAL_BLOCKS, disk);
}

static void write_bitmap(const char *bm) {
    fseek(disk, 0, SEEK_SET);
    fwrite(bm, 1, TOTAL_BLOCKS, disk);
    fflush(disk);
}

static void read_inode(int idx, Inode *nd) {
    long off = (long)INODE_START_BLOCK * BLOCK_SIZE + (long)idx * INODE_SIZE;
    fseek(disk, off, SEEK_SET);
    fread(nd, sizeof(Inode), 1, disk);
}

static void write_inode(int idx, const Inode *nd) {
    long off = (long)INODE_START_BLOCK * BLOCK_SIZE + (long)idx * INODE_SIZE;
    fseek(disk, off, SEEK_SET);
    fwrite(nd, sizeof(Inode), 1, disk);
    fflush(disk);
}

static void read_dir_entry(int idx, DirEntry *de) {
    long off = (long)DIR_START_BLOCK * BLOCK_SIZE + (long)idx * DIR_ENTRY_SIZE;
    fseek(disk, off, SEEK_SET);
    fread(de, sizeof(DirEntry), 1, disk);
}

static void write_dir_entry(int idx, const DirEntry *de) {
    long off = (long)DIR_START_BLOCK * BLOCK_SIZE + (long)idx * DIR_ENTRY_SIZE;
    fseek(disk, off, SEEK_SET);
    fwrite(de, sizeof(DirEntry), 1, disk);
    fflush(disk);
}

/* ===== YARDIMCILAR ===== */

static void free_inode_blocks(Inode *nd, char *bm) {
    for (int b = 0; b < 7; b++) {
        if (nd->blocks[b]) { bm[nd->blocks[b]] = 0; nd->blocks[b] = 0; }
    }
    nd->size = 0;
}

static int find_free_blocks(int count, int *out, const char *bm) {
    int found = 0;
    for (int i = DATA_START_BLOCK; i < TOTAL_BLOCKS && found < count; i++)
        if (!bm[i]) out[found++] = i;
    return found == count;
}

static int find_file(const char *name, int *dir_idx, int *inode_idx) {
    DirEntry de;
    for (int i = 0; i < DIR_COUNT; i++) {
        read_dir_entry(i, &de);
        if (de.is_used && strcmp(de.name, name) == 0) {
            *dir_idx = i; *inode_idx = de.inode_index; return 1;
        }
    }
    return 0;
}

/* Dosya verisini tek bir tampona okur. Tampon calloc ile ayrilir,
   cagiran free etmeli. Boyut *out_size'a yazilir. */
static char *read_file_data(const Inode *nd, int *out_size) {
    int sz = nd->size;
    *out_size = sz;
    if (sz == 0) return calloc(1, 1);
    char *buf = calloc(sz + 1, 1);
    if (!buf) return NULL;
    int left = sz;
    for (int b = 0; b < 7 && nd->blocks[b] && left > 0; b++) {
        int to_read = left > BLOCK_SIZE ? BLOCK_SIZE : left;
        fseek(disk, (long)nd->blocks[b] * BLOCK_SIZE, SEEK_SET);
        fread(buf + (sz - left), 1, to_read, disk);
        left -= to_read;
    }
    return buf;
}

/* Veriyi inode'a bloklara parcalayarak yazar.
   Bm guncellenir ama yazilmaz — cagiran write_bitmap cagrili. */
static int write_file_data(Inode *nd, const char *data, int size, char *bm) {
    int nblk = size == 0 ? 1 : (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (nblk > 7) { printf("Hata: Dosya cok buyuk (maks 7 KB).\n"); return 0; }
    int blks[7];
    if (!find_free_blocks(nblk, blks, bm)) {
        printf("Hata: Diskte yeterli alan yok.\n"); return 0;
    }
    char chunk[BLOCK_SIZE];
    int written = 0;
    for (int b = 0; b < nblk; b++) {
        bm[blks[b]] = 1;
        nd->blocks[b] = blks[b];
        memset(chunk, 0, BLOCK_SIZE);
        int len = (size - written) > BLOCK_SIZE ? BLOCK_SIZE : (size - written);
        if (len > 0) memcpy(chunk, data + written, len);
        fseek(disk, (long)blks[b] * BLOCK_SIZE, SEEK_SET);
        fwrite(chunk, 1, BLOCK_SIZE, disk);
        written += len;
    }
    nd->size = size;
    return 1;
}

/* ===== FORMAT ===== */

static void format_disk(void) {
    printf("Sanal disk bulunamadi. Olusturuluyor...\n");
    FILE *f = fopen(DISK_FILE, "wb");
    if (!f) { perror("Disk olusturulamadi"); exit(1); }
    char empty[BLOCK_SIZE] = {0};
    for (int i = 0; i < TOTAL_BLOCKS; i++) fwrite(empty, 1, BLOCK_SIZE, f);
    char bm[TOTAL_BLOCKS] = {0};
    for (int i = 0; i < DATA_START_BLOCK; i++) bm[i] = 1;
    fseek(f, 0, SEEK_SET);
    fwrite(bm, 1, TOTAL_BLOCKS, f);
    fclose(f);
    printf("Format tamamlandi.\n");
}

/* ===== KOMUTLAR ===== */

void cmd_create(const char *name) {
    int di, ii;
    if (find_file(name, &di, &ii)) { printf("Hata: '%s' zaten var.\n", name); return; }
    DirEntry de; int free_di = -1;
    for (int i = 0; i < DIR_COUNT; i++) {
        read_dir_entry(i, &de);
        if (!de.is_used) { free_di = i; break; }
    }
    if (free_di < 0) { printf("Hata: Dizin dolu.\n"); return; }
    Inode nd; int free_ii = -1;
    for (int i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &nd);
        if (!nd.is_used) { free_ii = i; break; }
    }
    if (free_ii < 0) { printf("Hata: Inode tablosu dolu.\n"); return; }
    memset(&nd, 0, sizeof(Inode)); nd.is_used = 1;
    write_inode(free_ii, &nd);
    memset(&de, 0, sizeof(DirEntry)); de.is_used = 1;
    strncpy(de.name, name, 42); de.inode_index = free_ii;
    write_dir_entry(free_di, &de);
    printf("'%s' olusturuldu.\n", name);
}

void cmd_ls(void) {
    DirEntry de; Inode nd; int count = 0;
    printf("%-24s | %-10s | %s\n", "Dosya Adi", "Boyut (B)", "Bloklar");
    printf("----------------------------------------------------------\n");
    for (int i = 0; i < DIR_COUNT; i++) {
        read_dir_entry(i, &de);
        if (!de.is_used) continue;
        read_inode(de.inode_index, &nd);
        printf("%-24s | %-10d | [", de.name, nd.size);
        int first = 1;
        for (int b = 0; b < 7; b++) {
            if (nd.blocks[b]) { if (!first) printf(","); printf("%d", nd.blocks[b]); first = 0; }
        }
        printf("]\n"); count++;
    }
    if (!count) printf("Dosya sistemi bos.\n");
}

static void do_write(const char *name, const char *text, int append_mode) {
    int di, ii;
    if (!find_file(name, &di, &ii)) { printf("Hata: '%s' bulunamadi.\n", name); return; }
    Inode nd; read_inode(ii, &nd);
    char bm[TOTAL_BLOCKS]; read_bitmap(bm);

    int new_len = (int)strlen(text);
    int existing = (nd.size > 0 && nd.size <= MAX_FILE_BYTES) ? nd.size : 0;
    int total = append_mode ? existing + new_len : new_len;

    if (total > MAX_FILE_BYTES) { printf("Hata: Dosya cok buyuk (maks 7 KB).\n"); return; }

    char *combined = calloc(total + 1, 1);
    if (!combined) { printf("Hata: Bellek yetersiz.\n"); return; }

    if (append_mode && existing > 0) {
        int tmp; char *old = read_file_data(&nd, &tmp);
        if (old) { memcpy(combined, old, existing); free(old); }
    }
    memcpy(combined + (append_mode ? existing : 0), text, new_len);

    free_inode_blocks(&nd, bm);
    if (write_file_data(&nd, combined, total, bm)) {
        write_bitmap(bm); write_inode(ii, &nd);
        printf("'%s': %d byte yazildi (toplam %d byte).\n", name, new_len, total);
    } else {
        write_bitmap(bm); /* bloklari geri serbest birak */
    }
    free(combined);
}

void cmd_write(const char *name, const char *text)  { do_write(name, text, 0); }
void cmd_append(const char *name, const char *text) { do_write(name, text, 1); }

void cmd_read(const char *name) {
    int di, ii;
    if (!find_file(name, &di, &ii)) { printf("Hata: '%s' bulunamadi.\n", name); return; }
    Inode nd; read_inode(ii, &nd);
    if (nd.size == 0) { printf("'%s' bos.\n", name); return; }
    int sz; char *data = read_file_data(&nd, &sz);
    printf("--- %s ---\n%s\n----------\n", name, data ? data : "");
    free(data);
}

void cmd_rm(const char *name) {
    int di, ii;
    if (!find_file(name, &di, &ii)) { printf("Hata: '%s' bulunamadi.\n", name); return; }
    Inode nd; read_inode(ii, &nd);
    char bm[TOTAL_BLOCKS]; read_bitmap(bm);
    free_inode_blocks(&nd, bm); nd.is_used = 0;
    write_inode(ii, &nd); write_bitmap(bm);
    DirEntry de; memset(&de, 0, sizeof(DirEntry));
    write_dir_entry(di, &de);
    printf("'%s' silindi.\n", name);
}

void cmd_rename(const char *old_name, const char *new_name) {
    int di, ii, tdi, tii;
    if (!find_file(old_name, &di, &ii)) { printf("Hata: '%s' bulunamadi.\n", old_name); return; }
    if (find_file(new_name, &tdi, &tii)) { printf("Hata: '%s' zaten var.\n", new_name); return; }

    Inode nd; read_inode(ii, &nd);
    printf("[rename] inode=%d  bloklar=[", ii);
    for (int b = 0; b < 7; b++) if (nd.blocks[b]) printf("%d ", nd.blocks[b]);
    printf("] -> degismedi\n");
    printf("  Sadece dizin girdisi guncellendi: '%s' -> '%s'\n", old_name, new_name);

    DirEntry de; read_dir_entry(di, &de);
    memset(de.name, 0, sizeof(de.name)); strncpy(de.name, new_name, 42);
    write_dir_entry(di, &de);
}

void cmd_cp(const char *src, const char *dst) {
    int di, ii;
    if (!find_file(src, &di, &ii)) { printf("Hata: '%s' bulunamadi.\n", src); return; }
    int tdi, tii;
    if (find_file(dst, &tdi, &tii)) { printf("Hata: '%s' zaten var.\n", dst); return; }
    Inode nd; read_inode(ii, &nd);
    int sz; char *data = read_file_data(&nd, &sz);
    if (!data) { printf("Hata: Bellek yetersiz.\n"); return; }
    cmd_create(dst);
    if (sz > 0) do_write(dst, data, 0);
    free(data);
    printf("'%s' -> '%s' kopyalandi.\n", src, dst);
}

void cmd_mv(const char *src, const char *dst) {
    int tdi, tii;
    if (find_file(dst, &tdi, &tii)) { printf("Hata: '%s' zaten var.\n", dst); return; }

    int src_di, src_ii;
    if (!find_file(src, &src_di, &src_ii)) { printf("Hata: '%s' bulunamadi.\n", src); return; }
    Inode old_nd; read_inode(src_ii, &old_nd);
    printf("[mv] eski -> inode=%d  bloklar=[", src_ii);
    for (int b = 0; b < 7; b++) if (old_nd.blocks[b]) printf("%d ", old_nd.blocks[b]);
    printf("]\n");

    cmd_cp(src, dst);
    cmd_rm(src);

    int dst_di, dst_ii;
    find_file(dst, &dst_di, &dst_ii);
    Inode new_nd; read_inode(dst_ii, &new_nd);
    printf("[mv] yeni -> inode=%d  bloklar=[", dst_ii);
    for (int b = 0; b < 7; b++) if (new_nd.blocks[b]) printf("%d ", new_nd.blocks[b]);
    printf("]\n");
    printf("  Inode ve bloklar yeniden tahsis edildi: '%s' -> '%s'\n", src, dst);
}

/* concat "a" "b" "c" → a ve b icerigi birlestirilerek c'ye yazilir */
void cmd_concat(const char *a, const char *b, const char *c) {
    int di, ii;
    if (!find_file(a, &di, &ii)) { printf("Hata: '%s' bulunamadi.\n", a); return; }
    Inode na; read_inode(ii, &na);
    int sa; char *da = read_file_data(&na, &sa);

    if (!find_file(b, &di, &ii)) { printf("Hata: '%s' bulunamadi.\n", b); free(da); return; }
    Inode nb; read_inode(ii, &nb);
    int sb; char *db = read_file_data(&nb, &sb);

    if (sa + sb > MAX_FILE_BYTES) {
        printf("Hata: Birlesmis boyut maks 7 KB'yi asiyor.\n");
        free(da); free(db); return;
    }

    char *combined = calloc(sa + sb + 1, 1);
    if (!combined) { free(da); free(db); return; }
    memcpy(combined, da, sa);
    memcpy(combined + sa, db, sb);

    cmd_create(c);
    if (sa + sb > 0) do_write(c, combined, 0);
    free(da); free(db); free(combined);
    printf("'%s' + '%s' -> '%s' (%d byte)\n", a, b, c, sa + sb);
}

void cmd_stat(const char *name) {
    int di, ii;
    if (!find_file(name, &di, &ii)) { printf("Hata: '%s' bulunamadi.\n", name); return; }
    Inode nd; read_inode(ii, &nd);
    int blk_count = 0;
    for (int b = 0; b < 7; b++) if (nd.blocks[b]) blk_count++;
    printf("\n--- stat: %s ---\n", name);
    printf("  Dizin girdisi : %d\n", di);
    printf("  Inode         : %d\n", ii);
    printf("  Boyut         : %d byte\n", nd.size);
    printf("  Blok sayisi   : %d\n", blk_count);
    printf("  Bloklar       : ");
    for (int b = 0; b < 7; b++) if (nd.blocks[b]) printf("%d ", nd.blocks[b]);
    printf("\n-------------------\n");
}

void cmd_wc(const char *name) {
    int di, ii;
    if (!find_file(name, &di, &ii)) { printf("Hata: '%s' bulunamadi.\n", name); return; }
    Inode nd; read_inode(ii, &nd);
    int sz; char *data = read_file_data(&nd, &sz);
    if (!data) return;
    int lines = 0, words = 0, bytes = sz;
    int in_word = 0;
    for (int i = 0; i < sz; i++) {
        if (data[i] == '\n') lines++;
        if (isspace((unsigned char)data[i])) { in_word = 0; }
        else { if (!in_word) { words++; in_word = 1; } }
    }
    free(data);
    printf("%5d satir  %5d kelime  %5d byte   %s\n", lines, words, bytes, name);
}

void cmd_find(const char *keyword) {
    DirEntry de; Inode nd;
    int found = 0;
    for (int i = 0; i < DIR_COUNT; i++) {
        read_dir_entry(i, &de);
        if (!de.is_used) continue;
        read_inode(de.inode_index, &nd);
        int sz; char *data = read_file_data(&nd, &sz);
        if (!data) continue;
        if (strstr(data, keyword)) {
            printf("  %s\n", de.name); found++;
        }
        free(data);
    }
    if (!found) printf("'%s' hicbir dosyada bulunamadi.\n", keyword);
    else printf("%d dosyada bulundu.\n", found);
}

void cmd_dump(const char *name) {
    int di, ii;
    if (!find_file(name, &di, &ii)) { printf("Hata: '%s' bulunamadi.\n", name); return; }
    Inode nd; read_inode(ii, &nd);
    int sz; char *data = read_file_data(&nd, &sz);
    if (!data) return;
    printf("--- hex dump: %s (%d byte) ---\n", name, sz);
    for (int i = 0; i < sz; i += 16) {
        printf("%04x  ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < sz) printf("%02x ", (unsigned char)data[i + j]);
            else printf("   ");
            if (j == 7) printf(" ");
        }
        printf(" |");
        for (int j = 0; j < 16 && i + j < sz; j++)
            printf("%c", isprint((unsigned char)data[i + j]) ? data[i + j] : '.');
        printf("|\n");
    }
    printf("---\n");
    free(data);
}

void cmd_status(void) {
    char bm[TOTAL_BLOCKS]; read_bitmap(bm);
    int free_blk = 0, used_blk = 0;
    for (int i = DATA_START_BLOCK; i < TOTAL_BLOCKS; i++) {
        if (bm[i]) used_blk++; else free_blk++;
    }
    int used_inodes = 0, data_bytes = 0;
    Inode nd; DirEntry de;
    for (int i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &nd); if (nd.is_used) used_inodes++;
    }
    for (int i = 0; i < DIR_COUNT; i++) {
        read_dir_entry(i, &de);
        if (!de.is_used) continue;
        read_inode(de.inode_index, &nd); data_bytes += nd.size;
    }
    printf("\n=== Dosya Sistemi Durumu ===\n");
    printf("Disk dosyasi      : %s\n", DISK_FILE);
    printf("Toplam kapasite   : %d KB (%d blok)\n", TOTAL_BLOCKS, TOTAL_BLOCKS);
    printf("Blok boyutu       : %d byte\n", BLOCK_SIZE);
    printf("Veri bloklari     : %d blok\n", TOTAL_BLOCKS - DATA_START_BLOCK);
    printf("Bos bloklar       : %d (%d KB)\n", free_blk, free_blk);
    printf("Dolu bloklar      : %d (%d KB)\n", used_blk, used_blk);
    printf("Kullanilan inode  : %d / %d\n", used_inodes, INODE_COUNT);
    printf("Toplam veri       : %d byte\n", data_bytes);
    printf("============================\n\n");
}

void cmd_defrag(void) {
    /* Her dosyanin verisini oku, tum veri bloklarini serbest birak,
       sonra DATA_START_BLOCK'tan itibaren sirali yeniden tahsis et. */
    typedef struct { int di; int ii; char *data; int size; } FileSnap;
    FileSnap snaps[DIR_COUNT]; int nsnaps = 0;
    DirEntry de; Inode nd;
    for (int i = 0; i < DIR_COUNT; i++) {
        read_dir_entry(i, &de);
        if (!de.is_used) continue;
        read_inode(de.inode_index, &nd);
        int sz; char *data = read_file_data(&nd, &sz);
        snaps[nsnaps++] = (FileSnap){i, de.inode_index, data, sz};
    }
    if (nsnaps == 0) { printf("Defrag: Dosya yok, islem gerekmedi.\n"); return; }

    char bm[TOTAL_BLOCKS]; read_bitmap(bm);
    /* Tum veri bloklarini serbest birak */
    for (int i = 0; i < nsnaps; i++) {
        read_inode(snaps[i].ii, &nd);
        free_inode_blocks(&nd, bm);
        write_inode(snaps[i].ii, &nd);
    }
    /* Sirali yeniden yaz */
    for (int i = 0; i < nsnaps; i++) {
        read_inode(snaps[i].ii, &nd);
        if (snaps[i].size > 0)
            write_file_data(&nd, snaps[i].data, snaps[i].size, bm);
        else
            nd.size = 0;
        write_inode(snaps[i].ii, &nd);
        free(snaps[i].data);
    }
    write_bitmap(bm);
    printf("Defrag tamamlandi. %d dosya yeniden duzenlendi.\n", nsnaps);
}

void cmd_fsck(void) {
    /* Bitmap ile inode referanslari karsilastir */
    char bm[TOTAL_BLOCKS]; read_bitmap(bm);
    char ref[TOTAL_BLOCKS]; memset(ref, 0, TOTAL_BLOCKS);
    /* Metadata bloklari her zaman referansli */
    for (int i = 0; i < DATA_START_BLOCK; i++) ref[i] = 1;

    Inode nd;
    for (int i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &nd);
        if (!nd.is_used) continue;
        for (int b = 0; b < 7; b++)
            if (nd.blocks[b]) ref[nd.blocks[b]] = 1;
    }

    int orphan = 0, zombie = 0;
    for (int i = DATA_START_BLOCK; i < TOTAL_BLOCKS; i++) {
        if (bm[i] && !ref[i]) {
            printf("  [ORPHAN] Blok %d: bitmap'te dolu ama hicbir inode referans etmiyor.\n", i);
            orphan++;
        }
        if (!bm[i] && ref[i]) {
            printf("  [ZOMBIE] Blok %d: inode referans ediyor ama bitmap'te bos.\n", i);
            zombie++;
        }
    }

    /* Dizin-Inode tutarlilik kontrolu */
    DirEntry de;
    int dir_errors = 0;
    for (int i = 0; i < DIR_COUNT; i++) {
        read_dir_entry(i, &de);
        if (!de.is_used) continue;
        read_inode(de.inode_index, &nd);
        if (!nd.is_used) {
            printf("  [TUTARSIZ] Dizin girdisi '%s' gecersiz inode %d'ye isaret ediyor.\n",
                   de.name, de.inode_index);
            dir_errors++;
        }
    }

    if (!orphan && !zombie && !dir_errors)
        printf("fsck: Hata bulunamadi. Disk tutarli.\n");
    else
        printf("fsck: %d orphan, %d zombie blok, %d dizin hatasi bulundu.\n",
               orphan, zombie, dir_errors);
}

/* ===== ARGUMAN AYRISTIRICI (tirnak destekli) ===== */

/*
 * Bir sonraki token'i al. Tirnak icindeyse bosluklar dahil edilir.
 * Geri donus: 1 = token alindi, 0 = input bitti.
 */
static int next_token(const char **p, char *out, int maxlen) {
    while (**p == ' ') (*p)++;
    if (!**p) return 0;
    int i = 0;
    if (**p == '"') {
        (*p)++;
        while (**p && **p != '"' && i < maxlen - 1) out[i++] = *(*p)++;
        if (**p == '"') (*p)++;
    } else {
        while (**p && **p != ' ' && i < maxlen - 1) out[i++] = *(*p)++;
    }
    out[i] = '\0';
    return 1;
}

/* cmd + en fazla 3 arguman dondurur, token sayisi return */
static int parse_input(const char *input, char *cmd, char *a1, char *a2, char *a3) {
    const char *p = input;
    int n = 0;
    if (next_token(&p, cmd, 32)) n++;  else return 0;
    if (next_token(&p, a1,  64)) n++;  else return n;
    if (next_token(&p, a2,  8100)) n++;  else return n;
    if (next_token(&p, a3,  64)) n++;
    return n;
}

/* ===== YARDIM ===== */

static void print_help_all(void) {
    printf("Komutlar (detay icin: help \"<komut>\"):\n");
    printf("  create  \"ad\"                  Dosya olustur\n");
    printf("  ls                             Dosyalari listele\n");
    printf("  write   \"ad\" \"metin\"          Dosyaya yaz (uzerine yazar)\n");
    printf("  append  \"ad\" \"metin\"          Dosya sonuna ekle\n");
    printf("  read    \"ad\"                  Dosyayi oku\n");
    printf("  rm      \"ad\"                  Dosyayi sil\n");
    printf("  rename  \"eski\" \"yeni\"         Yeniden adlandir\n");
    printf("  cp      \"kaynak\" \"hedef\"      Kopyala\n");
    printf("  mv      \"kaynak\" \"hedef\"      Tasi\n");
    printf("  concat  \"a\" \"b\" \"c\"          a+b birlestir, c'ye yaz\n");
    printf("  stat    \"ad\"                  Dosya inode bilgisi\n");
    printf("  wc      \"ad\"                  Satir/kelime/byte say\n");
    printf("  find    \"kelime\"              Tum dosyalarda ara\n");
    printf("  dump    \"ad\"                  Hex dump\n");
    printf("  defrag                         Bloklari sikistir\n");
    printf("  fsck                           Disk tutarlilik kontrolu\n");
    printf("  status                         Disk durumu\n");
    printf("  help    [\"komut\"]             Bu yardim / komut detayi\n");
    printf("  exit                           Cikis\n");
}

static void print_help_cmd(const char *name) {
    if (!strcmp(name, "create")) {
        printf("\nKOMUT: create\n");
        printf("  Bos bir dosya olusturur. Veri icermez.\n");
        printf("  Disk uzerinde bir dizin girdisi ve inode tahsis edilir.\n\n");
        printf("  Kullanim:\n");
        printf("    create \"<dosya_adi>\"\n\n");
        printf("  Ornekler:\n");
        printf("    create \"notlar.txt\"      -> notlar.txt adinda bos dosya olusturur\n");
        printf("    create \"log 2024.txt\"    -> bosluk iceren isim, tirnak gereklidir\n\n");
        printf("  Hatalar:\n");
        printf("    Ayni isimde dosya varsa hata verir.\n");
        printf("    Maksimum 64 dosya olusturulabilir.\n\n");

    } else if (!strcmp(name, "ls")) {
        printf("\nKOMUT: ls\n");
        printf("  Diskteki tum dosyalari boyut ve blok bilgisiyle listeler.\n\n");
        printf("  Kullanim:\n");
        printf("    ls\n\n");
        printf("  Cikti sutunlari:\n");
        printf("    Dosya Adi | Boyut (byte) | Kullanilan veri bloklari\n\n");
        printf("  Ornek cikti:\n");
        printf("    notlar.txt               | 45         | [8,9]\n\n");

    } else if (!strcmp(name, "write")) {
        printf("\nKOMUT: write\n");
        printf("  Dosyaya veri yazar. Dosyada onceden veri varsa uzerine yazar.\n");
        printf("  Eski bloklar serbest birakilir, yeni bloklar tahsis edilir.\n\n");
        printf("  Kullanim:\n");
        printf("    write \"<dosya_adi>\" \"<metin>\"\n\n");
        printf("  Ornekler:\n");
        printf("    write \"notlar.txt\" \"Ilk satir\"          -> dosyaya yazar\n");
        printf("    write \"notlar.txt\" \"Yeni icerik\"        -> eskiyi silip yeniden yazar\n");
        printf("    write \"log.txt\" \"hata kodu 404\"         -> bosluklu metin, tirnak gerekli\n\n");
        printf("  Sinir: Maksimum 7168 byte (7 blok).\n\n");

    } else if (!strcmp(name, "append")) {
        printf("\nKOMUT: append\n");
        printf("  Mevcut dosyanin sonuna veri ekler. Eski icerik korunur.\n\n");
        printf("  Kullanim:\n");
        printf("    append \"<dosya_adi>\" \"<metin>\"\n\n");
        printf("  Senaryo:\n");
        printf("    create \"log.txt\"\n");
        printf("    write  \"log.txt\" \"[BASLA] sistem acildi\"\n");
        printf("    append \"log.txt\" \"[BILGI] kullanici giris yapti\"\n");
        printf("    append \"log.txt\" \"[HATA] baglanti kesildi\"\n");
        printf("    read   \"log.txt\"   -> uc satiri birlesmis gosterir\n\n");

    } else if (!strcmp(name, "read")) {
        printf("\nKOMUT: read\n");
        printf("  Dosya icerigini ekrana yazar.\n\n");
        printf("  Kullanim:\n");
        printf("    read \"<dosya_adi>\"\n\n");
        printf("  Ornek:\n");
        printf("    read \"notlar.txt\"\n\n");

    } else if (!strcmp(name, "rm")) {
        printf("\nKOMUT: rm\n");
        printf("  Dosyayi siler. Inode sifirlanir, bloklar bitmap'te serbest birakilir,\n");
        printf("  dizin girdisi kaldirilir.\n\n");
        printf("  Kullanim:\n");
        printf("    rm \"<dosya_adi>\"\n\n");
        printf("  Ornek:\n");
        printf("    rm \"gecici.txt\"\n\n");
        printf("  Not: Silinen veri kurtarilamaz (bloklar sifirlanmaz, sadece bos isaretle\n");
        printf("       bildirilir — gercek disk davranisiyla ayni).\n\n");

    } else if (!strcmp(name, "rename")) {
        printf("\nKOMUT: rename\n");
        printf("  Sadece dizin girdisindeki ismi degistirir.\n");
        printf("  Inode numarasi, veri bloklari ve icerik tamamen ayni kalir.\n");
        printf("  Disk uzerinde yalnizca birkaç byte degisir — cok hizli ve atomik.\n\n");
        printf("  Kullanim:\n");
        printf("    rename \"<eski_ad>\" \"<yeni_ad>\"\n\n");
        printf("  Ornek:\n");
        printf("    rename \"taslak.txt\" \"son_surum.txt\"\n\n");
        printf("  Cikti ornegi:\n");
        printf("    [rename] inode=0  bloklar=[8] -> degismedi\n");
        printf("      Sadece dizin girdisi guncellendi: 'taslak.txt' -> 'son_surum.txt'\n\n");
        printf("  mv ile farki:\n");
        printf("    rename -> inode ayni, bloklar ayni, sadece isim degisir\n");
        printf("    mv     -> yeni inode tahsis edilir, veri yeni bloklara kopyalanir\n\n");

    } else if (!strcmp(name, "cp")) {
        printf("\nKOMUT: cp\n");
        printf("  Bir dosyayi ayni disk icerisinde kopyalar.\n");
        printf("  Yeni inode ve bloklar tahsis edilir; kaynak degismez.\n\n");
        printf("  Kullanim:\n");
        printf("    cp \"<kaynak>\" \"<hedef>\"\n\n");
        printf("  Senaryo:\n");
        printf("    cp \"ayarlar.txt\" \"ayarlar_yedek.txt\"\n");
        printf("    write \"ayarlar.txt\" \"yeni deger\"\n");
        printf("    read  \"ayarlar_yedek.txt\"   -> eski icerik hala burada\n\n");

    } else if (!strcmp(name, "mv")) {
        printf("\nKOMUT: mv\n");
        printf("  Dosyayi yeni bir inode ve yeni bloklara tasir.\n");
        printf("  Arka planda cp + rm yapar:\n");
        printf("    1. Hedef icin yeni inode tahsis edilir\n");
        printf("    2. Veri yeni bloklara kopyalanir\n");
        printf("    3. Kaynak inode ve bloklari serbest birakilir\n\n");
        printf("  Kullanim:\n");
        printf("    mv \"<kaynak>\" \"<hedef>\"\n\n");
        printf("  Cikti ornegi:\n");
        printf("    [mv] eski -> inode=0  bloklar=[8]\n");
        printf("    [mv] yeni -> inode=1  bloklar=[9]\n");
        printf("      Inode ve bloklar yeniden tahsis edildi\n\n");
        printf("  rename ile farki:\n");
        printf("    rename -> inode ayni, bloklar ayni, sadece isim degisir\n");
        printf("    mv     -> yeni inode tahsis edilir, veri yeni bloklara kopyalanir\n\n");

    } else if (!strcmp(name, "concat")) {
        printf("\nKOMUT: concat\n");
        printf("  Iki dosyanin icerigini birlestirip ucuncu bir dosyaya yazar.\n");
        printf("  Kaynak dosyalar degismez.\n\n");
        printf("  Kullanim:\n");
        printf("    concat \"<a>\" \"<b>\" \"<c>\"\n\n");
        printf("  Senaryo:\n");
        printf("    create \"giris.txt\"   &&  write \"giris.txt\" \"Baslik: Rapor\"\n");
        printf("    create \"icerik.txt\"  &&  write \"icerik.txt\" \"Icerik buraya\"\n");
        printf("    concat \"giris.txt\" \"icerik.txt\" \"rapor.txt\"\n");
        printf("    read \"rapor.txt\"   -> iki dosyanin birlesmis hali\n\n");

    } else if (!strcmp(name, "stat")) {
        printf("\nKOMUT: stat\n");
        printf("  Dosyanin dusuk seviye metadata bilgisini gosterir:\n");
        printf("  dizin girdisi indeksi, inode numarasi, boyut, blok sayisi, blok listesi.\n\n");
        printf("  Kullanim:\n");
        printf("    stat \"<dosya_adi>\"\n\n");
        printf("  Ornek cikti:\n");
        printf("    Dizin girdisi : 0\n");
        printf("    Inode         : 0\n");
        printf("    Boyut         : 45 byte\n");
        printf("    Blok sayisi   : 1\n");
        printf("    Bloklar       : 8\n\n");

    } else if (!strcmp(name, "wc")) {
        printf("\nKOMUT: wc\n");
        printf("  Dosyadaki satir, kelime ve byte sayisini hesaplar.\n\n");
        printf("  Kullanim:\n");
        printf("    wc \"<dosya_adi>\"\n\n");
        printf("  Ornek:\n");
        printf("    wc \"notlar.txt\"  ->  3 satir  12 kelime  87 byte   notlar.txt\n\n");

    } else if (!strcmp(name, "find")) {
        printf("\nKOMUT: find\n");
        printf("  Tum dosyalarin iceriginde verilen kelimeyi arar.\n");
        printf("  Eslesen dosyalarin adini listeler.\n\n");
        printf("  Kullanim:\n");
        printf("    find \"<aranacak_metin>\"\n\n");
        printf("  Senaryo:\n");
        printf("    Birden fazla log dosyasi var; 'hata' kelimesini icerenler:\n");
        printf("    find \"hata\"   ->  log1.txt, log3.txt\n\n");

    } else if (!strcmp(name, "dump")) {
        printf("\nKOMUT: dump\n");
        printf("  Dosya icerigini hex + ASCII formatinda gosterir.\n");
        printf("  Dusuk seviye veri dogrulamasi ve hata ayiklama icin kullanilir.\n\n");
        printf("  Kullanim:\n");
        printf("    dump \"<dosya_adi>\"\n\n");
        printf("  Ornek cikti:\n");
        printf("    0000  4d 65 72 68 61 62 61 20  44 75 6e 79 61  |Merhaba Dunya|\n\n");

    } else if (!strcmp(name, "defrag")) {
        printf("\nKOMUT: defrag\n");
        printf("  Disk parcalanmasini giderir. Tum dosyalarin verisini okur,\n");
        printf("  veri bloklarini serbest birakir, DATA_START'tan itibaren sirali\n");
        printf("  yeniden tahsis eder.\n\n");
        printf("  Kullanim:\n");
        printf("    defrag\n\n");
        printf("  Senaryo:\n");
        printf("    Cok sayida dosya olusturup silince bloklar dagili hale gelir:\n");
        printf("    [8 dolu] [9 bos] [10 dolu] [11 bos] [12 dolu]\n");
        printf("    defrag sonrasi:\n");
        printf("    [8 dolu] [9 dolu] [10 dolu] [11 bos] [12 bos]\n\n");

    } else if (!strcmp(name, "fsck")) {
        printf("\nKOMUT: fsck\n");
        printf("  Disk tutarliligini kontrol eder. Uc turu hata arar:\n\n");
        printf("  [ORPHAN]  Bitmap'te dolu isaretli ama hicbir inode referans etmiyor.\n");
        printf("  [ZOMBIE]  Inode referans ediyor ama bitmap'te bos isaretli.\n");
        printf("  [TUTARSIZ] Dizin girdisi gecersiz bir inode'a isaret ediyor.\n\n");
        printf("  Kullanim:\n");
        printf("    fsck\n\n");
        printf("  Senaryo:\n");
        printf("    Programin bir onceki calismasinda cokme olmus olabilir.\n");
        printf("    fsck ile veri bütünlügü kontrol edilir.\n\n");

    } else if (!strcmp(name, "status")) {
        printf("\nKOMUT: status\n");
        printf("  Disk durumunu ozet olarak gosterir:\n");
        printf("  toplam kapasite, bos/dolu blok sayisi, kullanilan inode sayisi,\n");
        printf("  toplam veri miktari.\n\n");
        printf("  Kullanim:\n");
        printf("    status\n\n");

    } else {
        printf("Bilinmeyen komut: '%s'\n", name);
        printf("Tum komutlar icin sadece 'help' yazin.\n");
    }
}

/* ===== MAIN ===== */

int main(void) {
    {
        FILE *t = fopen(DISK_FILE, "rb");
        if (!t) format_disk(); else fclose(t);
    }
    disk = fopen(DISK_FILE, "r+b");
    if (!disk) { perror("Disk acilamadi"); return 1; }

    printf("Sanal Dosya Sistemi hazir. Komut listesi: help  |  Detay: help \"<komut>\"\n\n");

    char input[8192];
    char cmd[32], a1[64], a2[8100], a3[64];

    while (1) {
        printf("FS> "); fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;
        if (!input[0]) continue;

        cmd[0] = a1[0] = a2[0] = a3[0] = '\0';
        int n = parse_input(input, cmd, a1, a2, a3);

        if      (!strcmp(cmd, "exit"))                       break;
        else if (!strcmp(cmd, "help") && n >= 2)             print_help_cmd(a1);
        else if (!strcmp(cmd, "help"))                       print_help_all();
        else if (!strcmp(cmd, "ls"))                         cmd_ls();
        else if (!strcmp(cmd, "status"))                     cmd_status();
        else if (!strcmp(cmd, "defrag"))                     cmd_defrag();
        else if (!strcmp(cmd, "fsck"))                       cmd_fsck();
        else if (!strcmp(cmd, "create") && n >= 2)           cmd_create(a1);
        else if (!strcmp(cmd, "read")   && n >= 2)           cmd_read(a1);
        else if (!strcmp(cmd, "rm")     && n >= 2)           cmd_rm(a1);
        else if (!strcmp(cmd, "stat")   && n >= 2)           cmd_stat(a1);
        else if (!strcmp(cmd, "wc")     && n >= 2)           cmd_wc(a1);
        else if (!strcmp(cmd, "find")   && n >= 2)           cmd_find(a1);
        else if (!strcmp(cmd, "dump")   && n >= 2)           cmd_dump(a1);
        else if (!strcmp(cmd, "write")  && n >= 3)           cmd_write(a1, a2);
        else if (!strcmp(cmd, "append") && n >= 3)           cmd_append(a1, a2);
        else if (!strcmp(cmd, "rename") && n >= 3)           cmd_rename(a1, a2);
        else if (!strcmp(cmd, "cp")     && n >= 3)           cmd_cp(a1, a2);
        else if (!strcmp(cmd, "mv")     && n >= 3)           cmd_mv(a1, a2);
        else if (!strcmp(cmd, "concat") && n >= 4)           cmd_concat(a1, a2, a3);
        else { printf("Hatali komut. 'help' yazin.\n"); }
    }

    fclose(disk);
    printf("Cikis.\n");
    return 0;
}
