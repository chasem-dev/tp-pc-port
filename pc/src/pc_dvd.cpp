/* pc_dvd.cpp - DVD filesystem: read files from GCM/ISO disc image
 * Adapted from AC port's pc_disc.c for TP. Parses the GCM FST
 * and implements the Dolphin DVD API so JKRDvdRipper can load files. */
#include "pc_platform.h"
#include "pc_bswap.h"
#include <dolphin/dvd.h>
#include <dirent.h>

extern "C" {

/* ---- endian helpers ---- */
static u32 be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static u32 le32(const u8* p) {
    return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* ---- CISO support ---- */
#define CISO_HDR_SIZE 0x8000
#define CISO_MAGIC    0x4F534943

typedef struct {
    FILE* fp;
    int is_ciso;
    u32 block_size;
    int num_blocks;
    int* block_phys;
} DiscFile;

static DiscFile g_disc;
static int g_disc_open = 0;
static char g_disc_override_path[512];

static int disc_open(DiscFile* df, const char* path) {
    u8 hdr[CISO_HDR_SIZE];
    memset(df, 0, sizeof(*df));
    df->fp = fopen(path, "rb");
    if (!df->fp) return 0;

    if (fread(hdr, 1, CISO_HDR_SIZE, df->fp) == CISO_HDR_SIZE &&
        le32(hdr) == CISO_MAGIC) {
        df->block_size = le32(hdr + 4);
        if (df->block_size > 0) {
            int phys = 0;
            df->num_blocks = CISO_HDR_SIZE - 8;
            df->block_phys = (int*)malloc(df->num_blocks * sizeof(int));
            for (int i = 0; i < df->num_blocks; i++)
                df->block_phys[i] = hdr[8 + i] ? phys++ : -1;
            df->is_ciso = 1;
            return 1;
        }
    }
    df->is_ciso = 0;
    return 1;
}

static void disc_close(DiscFile* df) {
    if (df->fp) fclose(df->fp);
    if (df->block_phys) free(df->block_phys);
    memset(df, 0, sizeof(*df));
}

static int disc_read(DiscFile* df, u32 offset, void* dest, u32 size) {
    if (!df->fp) return 0;
    if (!df->is_ciso) {
        fseek(df->fp, (long)offset, SEEK_SET);
        return (u32)fread(dest, 1, size, df->fp) == size;
    }
    u8* out = (u8*)dest;
    while (size > 0) {
        u32 bi = offset / df->block_size;
        u32 bo = offset % df->block_size;
        u32 chunk = df->block_size - bo;
        if (chunk > size) chunk = size;
        if ((int)bi >= df->num_blocks || df->block_phys[bi] < 0) {
            memset(out, 0, chunk);
        } else {
            u32 phys = CISO_HDR_SIZE + (u32)df->block_phys[bi] * df->block_size + bo;
            fseek(df->fp, (long)phys, SEEK_SET);
            if ((u32)fread(out, 1, chunk, df->fp) != chunk) return 0;
        }
        out += chunk; offset += chunk; size -= chunk;
    }
    return 1;
}

/* ---- FST file table ---- */
#define MAX_FST_FILES 4096

typedef struct {
    char path[256];
    u32 disc_offset;
    u32 file_size;
} FSTFile;

static FSTFile g_fst_files[MAX_FST_FILES];
static int g_fst_file_count = 0;

static void build_fst_table(DiscFile* df) {
    u8 buf[12];
    g_fst_file_count = 0;

    disc_read(df, 0x424, buf, 4);
    u32 fst_off = be32(buf);

    disc_read(df, fst_off + 8, buf, 4);
    u32 num_ent = be32(buf);
    u32 str_tbl = fst_off + num_ent * 12;

    struct { u32 next_entry; char name[128]; } dir_stack[32];
    int stack_depth = 0;
    dir_stack[0].next_entry = num_ent;
    dir_stack[0].name[0] = '\0';
    stack_depth = 1;

    for (u32 i = 1; i < num_ent; i++) {
        while (stack_depth > 0 && i >= dir_stack[stack_depth - 1].next_entry)
            stack_depth--;

        disc_read(df, fst_off + i * 12, buf, 12);
        u32 noff = ((u32)buf[1] << 16) | ((u32)buf[2] << 8) | buf[3];
        char name[128];
        disc_read(df, str_tbl + noff, name, 127);
        name[127] = '\0';

        if (buf[0] == 1) {
            if (stack_depth < 32) {
                dir_stack[stack_depth].next_entry = be32(buf + 8);
                strncpy(dir_stack[stack_depth].name, name, 127);
                dir_stack[stack_depth].name[127] = '\0';
                stack_depth++;
            }
        } else {
            if (g_fst_file_count < MAX_FST_FILES) {
                char path[256] = "";
                for (int d = 1; d < stack_depth; d++) {
                    strncat(path, dir_stack[d].name, sizeof(path) - strlen(path) - 2);
                    strcat(path, "/");
                }
                strncat(path, name, sizeof(path) - strlen(path) - 1);
                strncpy(g_fst_files[g_fst_file_count].path, path, 255);
                g_fst_files[g_fst_file_count].path[255] = '\0';
                g_fst_files[g_fst_file_count].disc_offset = be32(buf + 4);
                g_fst_files[g_fst_file_count].file_size = be32(buf + 8);
                g_fst_file_count++;
            }
        }
    }
    fprintf(stderr, "[DVD] FST: %d files indexed\n", g_fst_file_count);
    /* Print first few paths for debugging */
    for (int i = 0; i < g_fst_file_count && i < 20; i++) {
        fprintf(stderr, "[DVD] FST[%d]: '%s'\n", i, g_fst_files[i].path);
    }
}

/* ---- disc image search ---- */
static int find_disc_image(char* out_path, int out_sz) {
    static const char* dirs[] = { ".", "rom", "orig", "build/rom", "../build/rom", NULL };
    for (int d = 0; dirs[d]; d++) {
        DIR* dp = opendir(dirs[d]);
        if (!dp) continue;
        struct dirent* ent;
        while ((ent = readdir(dp)) != NULL) {
            const char* n = ent->d_name;
            size_t len = strlen(n);
            if (len > 4 && (strcmp(n + len - 4, ".iso") == 0 ||
                            strcmp(n + len - 4, ".gcm") == 0 ||
                            strcmp(n + len - 5, ".ciso") == 0)) {
                if (strcmp(dirs[d], ".") == 0)
                    snprintf(out_path, out_sz, "%s", n);
                else
                    snprintf(out_path, out_sz, "%s/%s", dirs[d], n);
                closedir(dp);
                return 1;
            }
        }
        closedir(dp);
    }
    return 0;
}

void pc_disc_set_path(const char* path) {
    if (path == NULL || path[0] == '\0') {
        g_disc_override_path[0] = '\0';
        return;
    }

    strncpy(g_disc_override_path, path, sizeof(g_disc_override_path) - 1);
    g_disc_override_path[sizeof(g_disc_override_path) - 1] = '\0';
}

/* ---- DVD DiskID ---- */
typedef struct {
    char gameName[4];
    char company[2];
    u8 diskNumber;
    u8 gameVersion;
    u8 streaming;
    u8 streamBufSize;
    u8 padding[14];
    u32 dvdMagic;
    char gameTitle[64];
} DVDDiskID_t;

static DVDDiskID_t g_disk_id;

DVDDiskID* DVDGetCurrentDiskID(void) {
    return (DVDDiskID*)&g_disk_id;
}

/* ---- DVD File API ---- */
/* Use SDK DVDFileInfo from <dolphin/dvd.h> included at top. */

/* DVDDir and DVDDirEntry from <dolphin/dvd.h> */

/* Find file in FST, return index or -1 */
static int fst_find(const char* path) {
    if (path[0] == '/') path++;
    for (int i = 0; i < g_fst_file_count; i++) {
        if (strcmp(g_fst_files[i].path, path) == 0)
            return i;
    }
    /* Try case-insensitive */
    for (int i = 0; i < g_fst_file_count; i++) {
        if (strcasecmp(g_fst_files[i].path, path) == 0)
            return i;
    }
    return -1;
}

void DVDInit(void) {}

int DVDOpen(const char* fileName, DVDFileInfo* fileInfo) {
    int idx = fst_find(fileName);
    if (idx < 0) {
        if (g_pc_verbose) fprintf(stderr, "[DVD] Open FAIL: %s\n", fileName);
        return 0;
    }
    memset(fileInfo, 0, sizeof(DVDFileInfo));
    fileInfo->startAddr = g_fst_files[idx].disc_offset;
    fileInfo->length = g_fst_files[idx].file_size;
    if (g_pc_verbose) fprintf(stderr, "[DVD] Open: %s (%u bytes at 0x%X)\n",
                              fileName, fileInfo->length, fileInfo->startAddr);
    return 1;
}

int DVDClose(DVDFileInfo* fileInfo) {
    (void)fileInfo;
    return 1;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
    (void)prio;
    if (!g_disc_open || !fileInfo) return -1;
    u32 disc_off = fileInfo->startAddr + offset;
    if (disc_read(&g_disc, disc_off, addr, length))
        return (s32)length;
    return -1;
}

int DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset,
                         DVDCBCallback callback, s32 prio) {
    (void)block; (void)addr; (void)length; (void)offset; (void)callback; (void)prio;
    return 0;
}

s32 DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset,
                      DVDCallback callback, s32 prio) {
    s32 result = DVDReadPrio(fileInfo, addr, length, offset, prio);
    if (callback) callback(result, fileInfo);
    return result >= 0 ? 1 : -1;
}

int DVDOpenDir(const char* dirName, DVDDir* dir) { (void)dirName; (void)dir; return 0; }
int DVDReadDir(DVDDir* dir, DVDDirEntry* dirEntry) { (void)dir; (void)dirEntry; return 0; }
int DVDCloseDir(DVDDir* dir) { (void)dir; return 0; }

s32 DVDConvertPathToEntrynum(const char* pathName) {
    int idx = fst_find(pathName);
    if (idx < 0) {
        fprintf(stderr, "[DVD] ConvertPathToEntrynum FAIL: '%s'\n", pathName);
    }
    return idx >= 0 ? idx : -1;
}

int DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
    if (entrynum < 0 || entrynum >= g_fst_file_count) return 0;
    memset(fileInfo, 0, sizeof(DVDFileInfo));
    fileInfo->startAddr = g_fst_files[entrynum].disc_offset;
    fileInfo->length = g_fst_files[entrynum].file_size;
    return 1;
}

u32 DVDGetLength(DVDFileInfo* fileInfo) {
    return fileInfo ? fileInfo->length : 0;
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) { (void)block; return 0; }
s32 DVDCancel(volatile DVDCommandBlock* block) { (void)block; return 0; }
s32 DVDGetDriveStatus(void) { return 0; }

int DVDSetAutoFatalMessaging(BOOL enable) { (void)enable; return 1; }
int DVDCheckDisk(void) { return 0; }

int DVDGetCurrentDir(char* path, u32 maxlen) {
    if (path && maxlen > 1) { path[0] = '/'; path[1] = 0; }
    return 1;
}

int DVDChangeDir(const char* dirName) { (void)dirName; return 1; }

void DVDPause(void) {}
void DVDResume(void) {}

/* ---- Init/Shutdown ---- */
void pc_disc_init(void) {
    char path[512];
    if (g_disc_open) return;

    if (g_disc_override_path[0] != '\0') {
        strncpy(path, g_disc_override_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        if (!find_disc_image(path, sizeof(path))) {
            fprintf(stderr, "[DVD] No disc image found (searched ., rom/, orig/, build/rom/, ../build/rom/)\n");
            fprintf(stderr, "[DVD] Pass a disc path on the command line or place a .iso/.gcm/.ciso file in one of these directories\n");
            return;
        }
    }

    if (!disc_open(&g_disc, path)) {
        fprintf(stderr, "[DVD] Failed to open: %s\n", path);
        return;
    }

    /* Verify GCM magic */
    u8 buf[4];
    disc_read(&g_disc, 0x1C, buf, 4);
    if (be32(buf) != 0xC2339F3D) {
        fprintf(stderr, "[DVD] %s: not a valid GC disc image\n", path);
        disc_close(&g_disc);
        return;
    }

    /* Read disc ID */
    disc_read(&g_disc, 0, &g_disk_id, sizeof(g_disk_id));

    char id_str[7];
    memcpy(id_str, &g_disk_id, 6);
    id_str[6] = '\0';
    fprintf(stderr, "[DVD] Disc image: %s (%s, %s)\n",
            path, g_disc.is_ciso ? "CISO" : "ISO/GCM", id_str);

    /* Build FST */
    build_fst_table(&g_disc);
    g_disc_open = 1;

    fprintf(stderr, "[PC] Disc I/O initialized\n");
}

void pc_disc_shutdown(void) {
    if (g_disc_open) {
        disc_close(&g_disc);
        g_disc_open = 0;
    }
}

} /* extern "C" */
