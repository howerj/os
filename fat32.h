/* Project: FAT-32 library
 * Author:  Richard James Howe
 * License: The Unlicense
 * Email:   howe.r.j.89@gmail.com */
#ifndef FAT32_H
#define FAT32_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifndef FAT32_API
#define FAT32_API /* Used to apply attributes to exported functions */
#endif

#ifndef ALLOCATOR_FN
#define ALLOCATOR_FN
typedef void *(*allocator_fn)(void *arena, void *ptr, size_t oldsz, size_t newsz);
#endif

#include <stdint.h>
#include <stddef.h>

struct fat32_stat_t;
typedef struct fat32_file_t fat32_file_t;

enum {
	FAT32_ERROR = -1,
	FAT32_OK    =  0,
};

typedef struct {
	int type;
	size_t size;
} fat32_stat_t;

struct fat32_t;
typedef struct fat32_t fat32_t;

struct fat32_t {
	allocator_fn allocator;
	int (*open)(fat32_t *f, void **file, void *path, int new_file);
	int (*close)(fat32_t *f, void *file);
	int (*read)(fat32_t *f, void *file, size_t *cnt, uint8_t *bytes);
	int (*write)(fat32_t *f, void *file, size_t cnt, const uint8_t *bytes);
	int (*seek)(fat32_t *f, void *file, size_t pos);
	int (*tell)(fat32_t *f, void *file, size_t *pos);
	int (*flush)(fat32_t *f, void *file);
	int (*logger)(void *logfile, const char *fmt, va_list ap);
	void *arena,    /* passed to 'allocator' */
	     *logfile,  /* passed to 'logger' */
	     *file,     /* file handle of image, returned by open callback */
	     *state;    /* internal usage only; do not bounce */
};

FAT32_API int fat32_format(fat32_t *f, void *path, size_t image_size);
FAT32_API int fat32_mount(fat32_t *f, void *path);
FAT32_API int fat32_unmount(fat32_t *f, void *path);
FAT32_API int fat32_fopen(fat32_t *f, const char *path, fat32_file_t **file);
FAT32_API int fat32_fclose(fat32_t *f, fat32_file_t *file);
FAT32_API int fat32_fread(fat32_t *f, fat32_file_t *file, size_t *cnt, uint8_t *bytes);
FAT32_API int fat32_fwrite(fat32_t *f, fat32_file_t *file, size_t *cnt, const uint8_t *bytes);
FAT32_API int fat32_fseek(fat32_t *f, fat32_file_t *file, size_t pos);
FAT32_API int fat32_ftell(fat32_t *f, fat32_file_t *file, size_t *pos);
FAT32_API int fat32_fstat(fat32_t *f, const char *path, fat32_stat_t *stat);
FAT32_API int fat32_unlink(fat32_t *f, const char *path);
FAT32_API int fat32_mkdir(fat32_t *f, const char *path);
FAT32_API int fat32_tests(fat32_t *f, void *path);

#ifdef __cplusplus
}
#endif
#endif
