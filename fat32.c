#include "fat32.h"
#include <assert.h>
#include <string.h>


#ifndef FAT32_LOGGING
#define FAT32_LOGGING (0u)
#endif

enum {
	FAT32_TYPE_12 = 12,
	FAT32_TYPE_16 = 16,
	FAT32_TYPE_32 = 32,
};

struct fat32_file_t {
	unsigned long pos;
};

typedef struct {
	int type;
	fat32_t os;
} fat32_state_t;

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
static int fat32_log_fmt(fat32_t *f, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
static int fat32_log_line(fat32_t *f, const char *type, int die, int ret, const unsigned line, const char *fmt, ...) __attribute__ ((format (printf, 6, 7)));
#endif

static int fat32_log_fmt(fat32_t *f, const char *fmt, ...) {
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	const int r = f->os.logger(f->os.logfile, fmt, ap);
	va_end(ap);
	if (r < 0)
		(void)fat32_kill(f);
	return r;
}

static int fat32_log_line(fat32_t *f, const char *type, int die, int ret, const unsigned line, const char *fmt, ...) {
	assert(f);
	assert(fmt);
	if (f->os.flags & fat32_OPT_LOGGING_ON) {
		if (fat32_log_fmt(f, "%s:%u ", type, line) < 0)
			return fat32_ERROR;
		va_list ap;
		va_start(ap, fmt);
		if (f->os.logger(f->os.logfile, fmt, ap) < 0)
			(void)fat32_kill(f);
		va_end(ap);
		if (fat32_log_fmt(f, "\n") < 0)
			return fat32_ERROR;
	}
	if (die)
		return fat32_kill(f);
	return fat32_is_dead(f) ? fat32_ERROR : ret;
}

#if FAT32_LOGGING == 0
static inline int rcode(const int c) { return c; } /* suppresses warnings */
#define debug(F, ...) rcode(FAT32_OK)
#define info(F, ...)  rcode(FAT32_OK)
#define error(F, ...) rcode(FAT32_ERROR)
#define fatal(F, ...) fat32_kill((F))
#else
#define debug(F, ...) fat32_log_line((F), "debug", 0, FAT32_OK,    __LINE__, __VA_ARGS__)
#define info(F, ...)  fat32_log_line((F), "info",  0, FAT32_OK,    __LINE__, __VA_ARGS__)
#define error(F, ...) fat32_log_line((F), "error", 0, FAT32_ERROR, __LINE__, __VA_ARGS__)
#define fatal(F, ...) fat32_log_line((F), "fatal", 1, FAT32_ERROR, __LINE__, __VA_ARGS__)
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

static int wr(fat32_t *f, void *file, size_t cnt, const uint8_t *bytes) {
	assert(f);
	assert(f->write);
	assert(file);
	assert(bytes);
	/* TODO: Set error flag */
	return f->write(f, file, cnt, bytes);
}

static int rd(fat32_t *f, void *file, size_t cnt, uint8_t *bytes) {
	assert(f);
	assert(f->read);
	assert(file);
	assert(bytes);
	size_t c = cnt;
	const int r = f->read(f, file, &c, bytes);
	/* TODO: Set error flag */
	if (r < 0 || c != cnt)
		return FAT32_ERROR;
	return FAT32_OK;
}

enum { SERDES_READ, SERDES_WRITE, SERDES_WRITE_DEFAULT, };

static int fat32_bytes_serdes(fat32_t *f, uint32_t loc, const uint8_t *init, uint8_t *bytes, size_t length, int write) {
	assert(f);
	assert(bytes);
	if (sk(f, f->file, loc) < 0)
		return FAT32_ERROR;
	if (write) {
		if (write == SERDES_WRITE_DEFAULT) {
			if (init)
				memcpy(bytes, init, length);
			else
				memset(bytes, 0, length);
		}
		return wr(f, f->file, length, bytes);
	}
	return rd(f, f->file, length, bytes);
}

static int fat32_u8_serdes(fat32_t *f, uint32_t loc, uint8_t init, uint8_t *u8, int write) {
	assert(f);
	assert(u8);
	uint8_t s = init;
	return fat32_bytes_serdes(f, loc, &s, u8, 1, write);
}

static int fat32_u16_serdes(fat32_t *f, uint32_t loc, uint16_t init, uint16_t *u16, int write) {
	assert(f);
	assert(u16);
	if (sk(f, f->file, loc) < 0)
		return FAT32_ERROR;
	if (write == SERDES_WRITE_DEFAULT)
		*u16 = init;
	uint8_t b[2] = { *u16 & 0xff, (*u16 >> 8) & 0xff, };
	if (write)
		return wr(f, f->file, sizeof (b), b);
	if (rd(f, f->file, sizeof (b), b) < 0)
		return FAT32_ERROR;
	const uint16_t u = ((uint16_t)b[0] << 0) | (((uint16_t)b[1]) << 8);
	*u16 = u;
	return FAT32_OK;
}

static int fat32_u32_serdes(fat32_t *f, uint32_t loc, uint32_t init, uint32_t *u32, int write) {
	assert(f);
	assert(u32);
	if (sk(f, f->file, loc) < 0)
		return FAT32_ERROR;
	if (write == SERDES_WRITE_DEFAULT)
		*u32 = init;
	uint8_t b[4] = { *u32 & 0xff, (*u32 >> 8) & 0xff, (*u32 >> 16) & 0xff, (*u32 >> 24) & 0xff, };
	if (write)
		return wr(f, f->file, sizeof (b), b);
	if (rd(f, f->file, sizeof (b), b) < 0)
		return FAT32_ERROR;
	const uint32_t u = (((uint32_t)b[0]) << 0) | (((uint32_t)b[1]) << 8) | (((uint32_t)b[2]) << 16) | (((uint32_t)b[3]) << 24);
	*u32 = u;
	return FAT32_OK;
}

static int fat32_directory_entry_serdes(fat32_t *f, fat32_directory_entry_t *dir, int write) {
	assert(f);
	assert(dir);
	uint8_t d8_1[8] = { 0, }, d3_2[3] = { 0, }, d8_3[8] = { 0, };
	uint16_t hi = 0, lo = 0;
	if (fat32_bytes_serdes(f, 0x00, d8_1,  dir->file_name,      8, write) != FAT32_OK) goto fail;
	if (fat32_bytes_serdes(f, 0x08, d3_2,  dir->file_extension, 3, write) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x0B,    0, &dir->flag,              write) != FAT32_OK) goto fail;
	if (fat32_bytes_serdes(f, 0x0C, d8_3,  dir->unused,         8, write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x14,    0, &hi,                     write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x16,    0, &dir->time,              write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x18,    0, &dir->date,              write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x1A,    0, &lo,                     write) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x1C,    0, &dir->file_size,         write) != FAT32_OK) goto fail;
	dir->starting_cluster = ((uint32_t)hi << 16) | (uint32_t)lo;
	return FAT32_OK;
fail:
	return FAT32_ERROR;
}

static int fat32_boot_sector_serdes(fat32_t *f, fat32_boot_sector_t *bs, int write) {
	assert(f);
	assert(bs);
	uint8_t djmp[3] = { 0, }, dos[8] = { 'H', 'O', 'W', 'E', 'R', 'J', }, dres[12] = { 0, };
       	uint8_t dvol[11] = { 'B', 'O', 'O', 'T', };
       	uint8_t dsid[8] = { 'F', 'A', 'T', '3', '2', ' ', };
	/* TODO: Default values for things, detect type {FAT-{12,16,32}},
	 * handle different types. */
	if (fat32_bytes_serdes(f, 0x000, djmp,   bs->jump,                              3, write) != FAT32_OK) goto fail;
	if (fat32_bytes_serdes(f, 0x003, dos,    bs->os_name,                           8, write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x00B, 512,   &bs->bytes_per_sector,                     write) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x00D, 0,     &bs->sectors_per_cluster,                  write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x00E, 0,     &bs->reserved_sectors,                     write) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x010, 2,     &bs->number_of_fat_copies,                 write) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x011, 0,     &bs->unused0,                              write) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x015, 0xF8,  &bs->media_descriptor,                     write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x016, 0,     &bs->unused1,                              write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x018, 0,     &bs->sectors_per_track,                    write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x01A, 0,     &bs->heads,                                write) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x01C, 0,     &bs->start_sectors,                        write) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x020, 0,     &bs->sectors_in_partition,                 write) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x024, 0,     &bs->sectors_per_fat,                      write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x028, 0,     &bs->fat_handling_flags,                   write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x02A, 0,     &bs->drive_version,                        write) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x02C, 0,     &bs->root_directory_cluser_number,         write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x030, 0,     &bs->file_system_information_sector,       write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x032, 0,     &bs->backup_boot_sector,                   write) != FAT32_OK) goto fail;
	if (fat32_bytes_serdes(f, 0x034, dres,    bs->reserved,                        12, write) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x040, 0,      &bs->logical_drive_number,                write) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x041, 0,      &bs->current_head,                        write) != FAT32_OK) goto fail;
	if (fat32_u8_serdes   (f, 0x042, 0x29,   &bs->signature,                           write) != FAT32_OK) goto fail;
	if (fat32_u32_serdes  (f, 0x043, 0,      &bs->id,                                  write) != FAT32_OK) goto fail;
	if (fat32_bytes_serdes(f, 0x047, dvol,    bs->volume_label,                    11, write) != FAT32_OK) goto fail;
	if (fat32_bytes_serdes(f, 0x052, dsid,    bs->system_id,                        8, write) != FAT32_OK) goto fail;
	if (fat32_bytes_serdes(f, 0x05A, NULL,    bs->code,                           420, write) != FAT32_OK) goto fail;
	if (fat32_u16_serdes  (f, 0x1FE, 0xAA55, &bs->executable_sector_signature,         write) != FAT32_OK) goto fail;
	return FAT32_OK;
fail:
	return FAT32_ERROR;
}

/* Add other parameters; sector size, etcetera? */
int fat32_format(fat32_t *f, void *path, int type, size_t image_size) {
	assert(f);
	assert(f->open);
	assert(f->close);
	assert(f->write);
	assert(path);
	if (type != FAT32_TYPE_32) /* TODO: Support other FAT types */
		return FAT32_ERROR;
	if (image_size == 0) /* TODO: Check minimum file size */
		return FAT32_ERROR;
	if (f->open(f, &f->file, path, 1) < 0)
		return FAT32_ERROR;
	fat32_boot_sector_t bs = { .id = 0, };
	/* TODO: populate structures, file empty space with either 0x00 or 0xFF, close */
	const int r = fat32_boot_sector_serdes(f, &bs, SERDES_WRITE_DEFAULT);
	if (f->close(f, f->file) < 0)
		return FAT32_ERROR;
	f->file = NULL;
	return r != FAT32_OK ? FAT32_ERROR : FAT32_OK;
}

int fat32_mount(fat32_t *f, void *path) {
	assert(f);
	assert(path);
	return FAT32_OK;
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

