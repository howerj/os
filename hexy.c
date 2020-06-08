#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *fopen_or_die(const char *file, const char *mode) {
	assert(file);
	assert(mode);
	errno  = 0;
	FILE *f = fopen(file, mode);
	if (!f) {
		(void)fprintf(stderr, "Could not open file '%s' in mode '%s': %s\n", file, mode, strerror(errno));
		exit(1);
	}
	return f;
}

int main(int argc, char **argv) {
	int r = 0;
	if (argc != 3) {
		(void)fprintf(stderr, "usage: %s in.hex out.bin\n", argv[0]);
		return 1;
	}
	FILE *in = fopen_or_die(argv[1], "rb"), *out = fopen_or_die(argv[2], "wb");

	uint64_t u = 0;
	while (fscanf(in, "%"SCNx64, &u) == 1) {
		errno = 0;
		if (1 != fwrite(&u, sizeof u, 1, out)) {
			(void)fprintf(stderr, "unable to write word: %s\n", strerror(errno));
			return 1;
		}
	}

	if (fclose(in) < 0)
		r = 1;

	if (fclose(out) < 0)
		r = 1;
	return r;
}

