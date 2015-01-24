#include "kstring.h"

size_t kstrlen(const char* str)
{
	size_t ret = 0u;
	while (0u != str[ret])
		ret++;
	return ret;
}

