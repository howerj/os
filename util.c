/** @file       util.c
 *  @brief      PL/0 generic utilities 
 *  @author     Richard Howe (2016)
 *  @license    MIT
 *  @email      howe.r.j.89@gmail.com */
#include "pl0.h"
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

void *allocate(size_t sz)
{
	errno = 0;
	void *r = calloc(sz, 1);
	if(!r) {
		fprintf(stderr, "allocation failed: %s", errno ? strerror(errno) : "unknown reason");
		exit(EXIT_FAILURE);
	}
	return r;
}

char *duplicate(const char *str)
{
	char *r;
	assert(str);
	errno = 0;
	r = malloc(strlen(str)+1);
	if(!r) {
		fprintf(stderr, "duplicate failed: %s", errno ? strerror(errno) : "unknown reason");
		exit(EXIT_FAILURE);
	}
	strcpy(r, str);
	return r;
}

error_t *new_error(void)
{
	return allocate(sizeof(error_t));
}

void indent(FILE *output, char c, unsigned i)
{
	assert(output);
	while(i--)
		fputc(c, output);
}

void ethrow(error_t *e)
{
	if(e && e->jmp_buf_valid) {
		e->jmp_buf_valid = 0;
		e->error = 1;
		longjmp(e->j, 1);
	}
	exit(EXIT_FAILURE);
}


