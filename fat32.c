#include "fat32.h"
#include <assert.h>

struct fat32_file_t {
	unsigned long node;
};

/* Add other parameters; sector size, etcetera? */
int fat32_format(fat32_t *f, void *path, size_t image_size) {
	assert(f);
	assert(path);
	/* open, populate structures, close */
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

int fat32_tests(fat32_t *f) {
	assert(f);
	return 0;
}

