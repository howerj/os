#include "fat32.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED(X) ((void)(X))

#ifdef _WIN32 /* Used to unfuck file mode for "Win"dows. Text mode is for losers. */
#include <windows.h>
#include <io.h>
#include <fcntl.h>
static void binary(FILE *f) { _setmode(_fileno(f), _O_BINARY); } /* only platform specific code... */
#else
static inline void binary(FILE *f) { UNUSED(f); }
#endif

static int fat32_close(fat32_t *f, void *file) {
	assert(f);
	if (!file)
		return FAT32_OK;
	return fclose(file) < 0 ? FAT32_ERROR : FAT32_OK;
}

static int fat32_open(fat32_t *f, void **file, void *path, int new_file) {
	assert(f);
	assert(file);
	assert(path);
	*file = NULL;
	FILE *nf = fopen(path, new_file ? "wb" : "ab");
	if (!nf)
		return FAT32_ERROR;
	if (fseek(nf, 0, SEEK_SET) < 0) {
		(void)fclose(nf);
		return FAT32_ERROR;
	}
	*file = nf;
	return FAT32_OK;
}

static int fat32_read(fat32_t *f, void *file, size_t *cnt, uint8_t *bytes) {
	assert(f);
	assert(file);
	assert(cnt);
	assert(bytes);
	const size_t sz = *cnt;
	*cnt = 0;
	const size_t r = fread(bytes, 1, sz, file);
	if (ferror(file))
		return FAT32_ERROR;
	if (sz != r) {
		if (feof(file)) {
			clearerr(file);
			*cnt = sz;
			return FAT32_OK;
		}
		return FAT32_ERROR;
	}
	*cnt = sz;
	return FAT32_OK;
}

static int fat32_write(fat32_t *f, void *file, const size_t cnt, const uint8_t *bytes) {
	assert(f);
	assert(file);
	assert(bytes);
	const size_t r = fwrite(bytes, 1, cnt, file);
	if (cnt != r)
		return FAT32_ERROR;
	return FAT32_OK;
}

static int fat32_seek(fat32_t *f, void *file, size_t pos) {
	assert(f);
	assert(file);
	return fseek(file, pos, SEEK_SET) < 0 ? FAT32_ERROR : FAT32_OK;
}

static int fat32_tell(fat32_t *f, void *file, size_t *pos) {
	assert(f);
	assert(file);
	assert(pos);
	*pos = 0;
	const long r = ftell(file);
	if (r < 0)
		return FAT32_ERROR;
	*pos = r;
	return FAT32_OK;
}

static int fat32_flush(fat32_t *f, void *file) {
	assert(f);
	assert(file);
	return fflush(file) < 0 ? FAT32_ERROR : FAT32_OK;
}

static void *fat32_allocator(void *arena, void *ptr, const size_t oldsz, const size_t newsz) {
	UNUSED(arena);
	if (newsz == 0) {
		free(ptr);
		return NULL;
	}
	if (newsz > oldsz)
		return realloc(ptr, newsz);
	return ptr;
}

static int fat32_logger(void *logger, const char *fmt, va_list ap) {
	assert(fmt);
	assert(logger);
	FILE *f = logger;
	va_list copy;
	va_copy(copy, ap);
	const int r = vfprintf(f, fmt, copy);
	va_end(copy);
	return r;
}

static int format(fat32_t *f, int argc, char **argv) {
	assert(f);
	assert(argv);
	assert(argc > 0);
	return 0;
}

static int help(const char *arg0) {
	assert(arg0);
	return 0;
}

int main(int argc, char **argv) {

	binary(stdin);
	binary(stdout);
	binary(stderr);

	if (argc < 2) {
		(void)help(argv[0]);
		return 1;
	}

	fat32_t f = {
		.allocator = fat32_allocator,
		.open      = fat32_open,
		.close     = fat32_close,
		.read      = fat32_read,
		.write     = fat32_write,
		.seek      = fat32_seek,
		.tell      = fat32_tell,
		.flush     = fat32_flush,
		.logger    = fat32_logger,
		.logfile   = stderr,
		.arena     = NULL,
	};

	/* TODO:
	 * ./fat32 format <image-name> <kilobytes> <other-params>
	 * ./fat32 info <image-name>
	 * ./fat32 add <image-name> <path>...
	 * ./fat32 del <image-name> <path>...
	 * ./fat32 ls  <image-name> <path>...
	 * ./fat32 stat <image-name> <path>...
	 * ./fat32 xxd <image-name> <path>...
	 * ./fat32 cat <image-name> <path>...
	 * ./fat32 mkdir <image-name> <path>...
	 * ./fat32 rename <image-name> <path>...  
	 * TODO:
	 * - Allow verbose modes
	 */

	if (!strcmp(argv[1], "format")) {
		return format(&f, argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "test")) {
	} else if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
		if (help(argv[0]) < 0)
			return 1;
		return 0;
	} else {
		(void)help(argv[0]);
		return 1;
	}


	return 0;
}

