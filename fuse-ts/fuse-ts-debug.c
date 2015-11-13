#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <dirent.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "fuse-ts.h"
#include "fuse-ts-debug.h"

#ifdef DEBUG

void print_parsed_opts(int argc_new) {
    printf("Used parameters:\n");
    printf("\toutbyte:\t%" PRId64 "\n", outbyte);
    printf("\tnumfiles:\t%d\n", numfiles);
    printf("\ttotalframes:\t%d\n", totalframes);
    printf("\tinframe:\t%d\n", inframe);
    printf("\toutframe:\t%d\n", outframe);
    printf("\tbase_dir:\t%s\n", base_dir);
    printf("\tfile_prefix:\t%s\n", prefix);
    printf("\tstart_time:\t%s\n", start_time);
    printf("\tintro_file:\t%s\n", intro_file);
    printf("\toutro_file:\t%s\n", outro_file);
    printf("\twinpath:\t%s\n", winpath);
    printf("\tstripslashes:\t%d\n", winpath_stripslashes);
    printf("\n\n\targuments left:\t%d\n\n", argc_new);
}

void print_file_chain_element(sourcefile_t * file){
	printf("File: '%s'\tat offset %" PRId64 "\thaving %" PRId64 " bytes\n", file->filename, file->globalpos, file->filesize);
}


void print_file_chain(const char * name, sourcefile_t * list){
	if (list == NULL) {
		printf("Empty file chain!\n");
	}
	printf("Dumping file chain %s:\n", name);
	while (list != NULL) {
		print_file_chain_element(list);
		list = list->next;
	}
	printf("End of file chain\n");
}

void debug_printf(const char * text, ...) {
	va_list args;
	va_start(args, text);
	vfprintf(logging, text, args);
	va_end(args);
	fflush(logging);
}

#endif

void error_printf(const char * text, ...) {
	va_list args;
	va_start(args, text);
	vfprintf(logging, text, args);
	va_end(args);
	fflush(logging);
}

