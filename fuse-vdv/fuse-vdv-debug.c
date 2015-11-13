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
#include "fuse-vdv.h"
#include "fuse-vdv-debug.h"

#ifdef DEBUG

void print_parsed_opts(int argc_new) {
    printf("Used parameters:\n");
    printf("\tlastframe:\t%d\n", lastframe);
    printf("\tinframe:\t%d\n", inframe);
    printf("\toutframe:\t%d\n", outframe);
    printf("\tbase_dir:\t%s\n", base_dir);
    printf("\tfile_prefix:\t%s\n", prefix);
    printf("\tstart_time:\t%s\n", start_time);
    printf("\tintro_file:\t%s\n", intro_file);
    printf("\toutro_file:\t%s\n", outro_file);
    printf("\n\n\targuments left:\t%d\n\n", argc_new);
}

void print_file_chain_element(sourcefile_t * file){
	int sf, ef;
	sf = (int)((off_t) file->startpos / frame_size);
	ef = (int)((off_t) file->endpos / frame_size);
	printf("File: '%s'\tusing frame %d to %d of %d total frames (byte %" PRId64 " to %" PRId64 ")\n",
			file->filename, sf, ef, file->frames, file->startpos, file->endpos);
//	printf("\t\t\t(global position %" PRId64 " to %" PRId64 ")\n",
//			file->globalpos, file->globalpos + file->endpos - file->startpos);
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

	// always print errors to stdout
	if(logging != stderr)
	{
		va_list cargs;
		va_copy(cargs, args);
		vfprintf(stderr, text, cargs);
		va_end(cargs);
	}

	vfprintf(logging, text, args);
	va_end(args);
	fflush(logging);
}

