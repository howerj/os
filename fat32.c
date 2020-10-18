#include "fat32.h"
#include <assert.h>
#include <string.h>


#ifndef FAT32_LOGGING
#define FAT32_LOGGING (0u)
#endif

/* TODO: Implement FAT-12, FAT-16, and FAT-32 code */
/* TODO: Rename 'FAT32' -> 'FAT' */
enum {
	FAT32_TYPE_12,
	FAT32_TYPE_16,
	FAT32_TYPE_32,
};

struct fat32_file_t {
	unsigned long node;
};

typedef struct {
	uint8_t  jump[3];                        /* 0000h; JMP 0x80h */
	uint8_t  os_name[8];                     /* 0003h; OEM ID, name of formatting OS */
	/* BIOS Parameter Block */
	uint16_t bytes_per_sector;               /* 000Bh; usually 512 */
	uint8_t  sectors_per_cluster;            /* 000Dh; 1, 2, 4, 8, 16, 32, 64, or 128 sectors */
	uint16_t reserved_sectors;               /* 000Eh; reserved sectors, including boot sector */
	uint8_t  number_of_fat_copies;           /* 0010h; usually 2 */
	uint32_t unused0;                        /* 0011h; not used in FAT32 */
	uint8_t  media_descriptor;               /* 0015h; value is F8h, media descriptor */
	uint16_t unused1;                        /* 0016h; not used in FAT32 */
	uint16_t sectors_per_track;              /* 0018h; disk geometry used when formatting the partition */
	uint16_t heads;                          /* 001Ah; disk geometry used when formatting the partition */
	uint32_t start_sectors;                  /* 001Ch; */
	uint32_t sectors_in_partition;           /* 0020h; */
	uint32_t sectors_per_fat;                /* 0024h; */
	uint16_t fat_handling_flags;             /* 0028h; */
	uint16_t drive_version;                  /* 002Ah; high byte = Major, low byte = Minor */
	uint32_t root_directory_cluser_number;   /* 002Ch; */
	uint16_t file_system_information_sector; /* 0030h; */
	uint16_t backup_boot_sector;             /* 0032h; */
	uint8_t  reserved[12];                   /* 0034h; */
	/* Extended BIOS Parameter Block */
	uint8_t  logical_drive_number;           /* 0040h; */
	uint8_t  current_head;                   /* 0041h; */
	uint8_t  signature;                      /* 0042h; 28h or 29h */
	uint32_t id;                             /* 0043h; randomly generated serial number */
	uint8_t  volume_label[11];               /* 0047h; should be same as volume label in root directory */
	uint8_t  system_id[8];                   /* 0052h; system id should be 'FAT32' */
	/* Code */
	uint8_t  code[420];                      /* 005Ah; blaze it */
	/* Signature */
	uint16_t  executable_sector_signature;   /* 01FEh; AA55h */
} fat32_boot_sector_t;

typedef struct {
	uint8_t file_name[8];            /* 00h; file part of 8.3 file name */
	uint8_t file_extension[3];       /* 08h; extension part of 8.3 file name */
	uint8_t flag;                    /* 0Bh; see 'FAT32_DIR_FLG_*' enumerations */
	uint8_t unused[8];               /* 0Ch; unused, write as read */
	uint32_t starting_cluster;       /* 14h (high word), 1Ah (low word); starting cluster of file */
	uint16_t time;                   /* 16h; Time field in special format */
	uint16_t date;                   /* 18h; Date field in special format */
	uint32_t file_size;              /* 1Ch; File size in BYTES */
} fat32_directory_entry_t; /* 32 bytes */


typedef struct {
	void *file;
} fat_state_t;

/* Time Format:
 * - Bits 0-4; 2 Second intervals 0-29
 * - Bits 5-10; Minutes 0-59
 * - Bits 11-15; Hours 0-23
 * Date Format:
 * - Bits 0-4: Day of Month 1-31
 * - Bits 5-8: Month of Year 1-12
 * - Bits 9-15: Year from 1980 (range 1980 to 2107) */

enum {
	FAT32_DIR_FLG_READ_ONLY = 1u << 0, /* read only flag */
	FAT32_DIR_FLG_HIDDEN    = 1u << 1, /* hidden entry */
	FAT32_DIR_FLG_SYSTEM    = 1u << 2, /* entry is a system file, not file, directory  */
	FAT32_DIR_FLG_VOLUME    = 1u << 3, /* entry is volume name, not file or directory */
	FAT32_DIR_FLG_DIRECTORY = 1u << 4, /* entry points to another directory, not a file */
	FAT32_DIR_FLG_ARCHIVED  = 1u << 5, /* archived flag, this file has been backed up */
	FAT32_DIR_FLG_RESERVED0 = 1u << 6, /* reserved bit */
	FAT32_DIR_FLG_RESERVED1 = 1u << 7, /* reserved bit */
}; /* flag field in 'fat32_directory_entry_t' */

#if 0
#ifdef __GNUC__
static int fat32_log_fmt(fat32_t *h, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
static int fat32_log_line(fat32_t *h, const char *type, int die, int ret, const unsigned line, const char *fmt, ...) __attribute__ ((format (printf, 6, 7)));
#endif

static int fat32_log_fmt(fat32_t *h, const char *fmt, ...) {
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	const int r = h->os.logger(h->os.logfile, fmt, ap);
	va_end(ap);
	if (r < 0)
		(void)fat32_kill(h);
	return r;
}

static int fat32_log_line(fat32_t *h, const char *type, int die, int ret, const unsigned line, const char *fmt, ...) {
	assert(h);
	assert(fmt);
	if (h->os.flags & fat32_OPT_LOGGING_ON) {
		if (fat32_log_fmt(h, "%s:%u ", type, line) < 0)
			return fat32_ERROR;
		va_list ap;
		va_start(ap, fmt);
		if (h->os.logger(h->os.logfile, fmt, ap) < 0)
			(void)fat32_kill(h);
		va_end(ap);
		if (fat32_log_fmt(h, "\n") < 0)
			return fat32_ERROR;
	}
	if (die)
		return fat32_kill(h);
	return fat32_is_dead(h) ? fat32_ERROR : ret;
}

#if FAT32_LOGGING == 0
static inline int rcode(const int c) { return c; } /* suppresses warnings */
#define debug(H, ...) rcode(FAT32_OK)
#define info(H, ...)  rcode(FAT32_OK)
#define error(H, ...) rcode(FAT32_ERROR)
#define fatal(H, ...) httpc_kill((H))
#else
#define debug(H, ...) httpc_log_line((H), "debug", 0, FAT32_OK,    __LINE__, __VA_ARGS__)
#define info(H, ...)  httpc_log_line((H), "info",  0, FAT32_OK,    __LINE__, __VA_ARGS__)
#define error(H, ...) httpc_log_line((H), "error", 0, FAT32_ERROR, __LINE__, __VA_ARGS__)
#define fatal(H, ...) httpc_log_line((H), "fatal", 1, FAT32_ERROR, __LINE__, __VA_ARGS__)
#endif
#endif


static int sk(fat32_t *f, void *file, uint32_t loc) {
	assert(f);
	assert(f->seek);
	assert(file);
	/* TODO: if loc == f->loc seek, else nothing to do */
	/* TODO: if error set error flag */
	return f->seek(f, file, loc);
}

static int fat32_bytes_serdes(fat32_t *f, uint32_t loc, uint8_t *bytes, size_t length, int wr) {
	assert(f);
	assert(bytes);
	//if (sk(f, file, loc) < 0)
	//	return FAT32_ERROR;
	/* TODO: wr/rd */
	return FAT32_OK;
}

static int fat32_u8_serdes(fat32_t *f, uint32_t loc, uint8_t *u8, int wr) {
	assert(f);
	assert(u8);
	//if (sk(f, file, loc) < 0)
	//	return FAT32_ERROR;
	/* TODO: wr/rd */
	return FAT32_OK;
}

static int fat32_u16_serdes(fat32_t *f, uint32_t loc, uint16_t *u16, int wr) {
	assert(f);
	assert(u16);
	//if (sk(f, file, loc) < 0)
	//	return FAT32_ERROR;
	/* TODO: wr/rd */
	return 0;
}

static int fat32_u32_serdes(fat32_t *f, uint32_t loc, uint32_t *u32, int wr) {
	assert(f);
	assert(u32);
	//if (sk(f, file, loc) < 0)
	//	return FAT32_ERROR;
	/* TODO: wr/rd */
	return 0;
}

static int fat32_directory_entry_serdes(fat32_t *f, fat32_directory_entry_t *dir, int wr) {
	assert(f);
	assert(dir);
	uint16_t hi = 0, lo = 0;
	if (fat32_bytes_serdes(f, 0x00,  dir->file_name,      8, wr) != FAT32_OK) goto fail;
	if (fat32_bytes_serdes(f, 0x08,  dir->file_extension, 3, wr) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x0B, &dir->flag,              wr) != FAT32_OK) goto fail;
	if (fat32_bytes_serdes(f, 0x0C,  dir->unused,         8, wr) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x14,  &hi,                    wr) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x16, &dir->time,              wr) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x18, &dir->date,              wr) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x1A,  &lo,                    wr) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x1C, &dir->file_size,         wr) != FAT32_OK) goto fail;
	dir->starting_cluster = ((uint32_t)hi << 16) | (uint32_t)lo;
	return FAT32_OK;
fail:
	/* TODO: set error flag */
	//memset(dir, 0, sizeof *dir);
	return FAT32_ERROR;
}

static int fat32_boot_sector_serdes(fat32_t *f, fat32_boot_sector_t *bs, int wr) {
	assert(f);
	assert(bs);
	/* TODO: Default values for things */
	if (fat32_bytes_serdes(f, 0x00,  bs->jump,                 3, wr) != FAT32_OK) goto fail;
	if (fat32_bytes_serdes(f, 0x03,  bs->os_name,              8, wr) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x0B, &bs->bytes_per_sector,        wr) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x0D, &bs->sectors_per_cluster,     wr) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x0E, &bs->reserved_sectors,        wr) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x10, &bs->number_of_fat_copies,    wr) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x11, &bs->unused0,                 wr) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x15, &bs->media_descriptor,        wr) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x16, &bs->unused1,                 wr) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x18, &bs->sectors_per_track,       wr) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x1A, &bs->heads,                   wr) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x1C, &bs->start_sectors,           wr) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x20, &bs->sectors_in_partition,    wr) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x24, &bs->sectors_per_fat,         wr) != FAT32_OK) goto fail;

#if 0
	/* BIOS Parameter Block */
	uint16_t fat_handling_flags;             /* 0028h; */
	uint16_t drive_version;                  /* 002Ah; high byte = Major, low byte = Minor */
	uint32_t root_directory_cluser_number;   /* 002Ch; */
	uint16_t file_system_information_sector; /* 0030h; */
	uint16_t backup_boot_sector;             /* 0032h; */
	uint8_t  reserved[12];                   /* 0034h; */
	/* Extended BIOS Parameter Block */
	uint8_t  logical_drive_number;           /* 0040h; */
	uint8_t  current_head;                   /* 0041h; */
	uint8_t  signature;                      /* 0042h; 28h or 29h */
	uint32_t id;                             /* 0043h; randomly generated serial number */
	uint8_t  volume_label[11];               /* 0047h; should be same as volume label in root directory */
	uint8_t  system_id[8];                   /* 0052h; system id should be 'FAT32' */
	/* Code */
	uint8_t  code[420];                      /* 005Ah; blaze it */
	/* Signature */
	uint16_t  executable_sector_signature;   /* 01FEh; AA55h */
#endif

	return FAT32_OK;
fail:
	/* TODO: Set error */
	return FAT32_ERROR;
}

/* Add other parameters; sector size, etcetera? */
int fat32_format(fat32_t *f, void *path, size_t image_size) {
	assert(f);
	assert(path);
	/* open, populate structures, file empty space with either 0x00 or 0xFF, close */
	return 0;
}

int fat32_mount(fat32_t *f, void *path) {
	assert(f);
	assert(path);
	return 0;
}

int fat32_unmount(fat32_t *f, void *path) {
	assert(f);
	assert(path);
	return 0;
}

int fat32_fopen(fat32_t *f, const char *path, fat32_file_t **file) {
	assert(f);
	assert(path);
	assert(file);
	*file = NULL;
	return 0;
}

int fat32_fclose(fat32_t *f, fat32_file_t *file) {
	assert(f);
	assert(file);
	return 0;
}

int fat32_fread(fat32_t *f, fat32_file_t *file, size_t *cnt, uint8_t *bytes) {
	assert(f);
	assert(file);
	assert(cnt);
	assert(bytes);
	return 0;
}

int fat32_fwrite(fat32_t *f, fat32_file_t *file, size_t *cnt, const uint8_t *bytes) {
	assert(f);
	assert(file);
	assert(cnt);
	assert(bytes);
	return 0;
}

int fat32_fseek(fat32_t *f, fat32_file_t *file, size_t pos) {
	assert(f);
	assert(file);
	return 0;
}

int fat32_ftell(fat32_t *f, fat32_file_t *file, size_t *pos) {
	assert(f);
	assert(file);
	assert(pos);
	return 0;
}

int fat32_fstat(fat32_t *f, const char *path, fat32_stat_t *stat) {
	assert(f);
	assert(path);
	assert(stat);
	return 0;
}

int fat32_unlink(fat32_t *f, const char *path) {
	assert(f);
	assert(path);
	return 0;
}

int fat32_mkdir(fat32_t *f, const char *path) {
	assert(f);
	assert(path);
	return 0;
}

int fat32_tests(fat32_t *f, void *path) {
	assert(f);
	assert(path);
	return 0;
}

