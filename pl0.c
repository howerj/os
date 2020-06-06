/** @file       pl0.c
 *  @brief      PL/0 driver
 *  @author     Richard Howe (2016)
 *  @license    MIT
 *  @email      howe.r.j.89@gmail.com 
 *
 * see https://en.wikipedia.org/wiki/PL/0 
 *
 * @todo Check that symbols and code to not overwrite each other */

#include "pl0.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/****************************** driver *************************************/

static code_t *process_file(FILE *input, FILE *output, int debug, int symbols)
{
	node_t *n;
	assert(input);
	assert(output);
	n = parse(input, debug);
	if(!n)
		return NULL;
	if(debug)
		print_node(output, n, 0, 0);
	code_t *c = code(n, MAX_CORE, 1);
	if(!c)
		return NULL;
	if(debug)
		dump(c, output, debug > 1);
	if(symbols)
		export(n, output);
	vm(c, stdin, output, debug);
	/*free_node(n);*/
	return c;
}

static FILE *fopen_or_die(const char *name, char *mode)
{
	FILE *file;
	assert(name);
	assert(mode);
	errno = 0;
	file = fopen(name, mode);
	if(!file) {
		fprintf(stderr, "could not open file \"%s\": %s\"\n", name, errno ? strerror(errno): "unknown");
		exit(EXIT_FAILURE);
	}
	return file;
}

static void help(void)
{
	const char *help = "\
PL/0 Compiler: A Toy Compiler\n\n\
\t-h print out a help message and quit\n\
\t-v increase verbosity levels\n\
\t-V print out version information and quit\n\
\t-S print out symbols defined and used\n\
\t-  Stop processing arguments\n\n\
Options must come before files to compile\n\n";
	fputs(help, stderr);
}

static void usage(const char *arg0)
{
	fprintf(stderr, "usage: %s [-h] [-v] [-V] [-] files\n", arg0);
}

int main(int argc, char **argv)
{
	int i, verbose = 0, symbols = 0;
	code_t *c;
	for(i = 1; i < argc && argv[i][0] == '-'; i++)
		switch(argv[i][1]) {
		case '\0': goto done; /* stop argument processing */
		case 'h':  usage(argv[0]);
			   help();
			   return -1;
		case 'v': if(verbose < INT_MAX) verbose++; break;
		case 'S': symbols = 1; break;
		case 'V': fprintf(stderr, "%s version: %d\n", argv[0], VERSION);
			  return -1;
		default:
			fprintf(stderr, "fatal: invalid argument '%s'\n", argv[i]);
			usage(argv[0]);
			return -1;
		}
done:
	if(i == argc) {
		if(verbose)
			fputs("reading from standard in\n", stderr);
		c = process_file(stdin, stdout, verbose, symbols);
		free_code(c);
	} else {
		for(; i < argc; i++) {
			FILE *in;
			if(verbose)
				fprintf(stderr, "reading from %s\n", argv[i]);
			in = fopen_or_die(argv[i], "rb");
			c = process_file(in, stdout, verbose, symbols);
			free_code(c);
			fclose(in);
		}
	}
	return 0;
}

