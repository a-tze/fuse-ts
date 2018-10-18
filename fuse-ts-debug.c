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
#include "fuse-ts-tools.h"

void print_parsed_opts(int argc_new) {
    info_printf("Used parameters:\n");
    info_printf("\toutbyte:\t%" PRId64 "\n", outbyte);
    info_printf("\tnumfiles:\t%d\n", numfiles);
    info_printf("\ttotalframes:\t%d\n", totalframes);
    info_printf("\tinframe:\t%d\n", inframe);
    info_printf("\toutframe:\t%d\n", outframe);
    info_printf("\tbase_dir:\t%s\n", base_dir);
    info_printf("\tfile_prefix:\t%s\n", prefix);
    info_printf("\tstart_time:\t%s\n", start_time);
    info_printf("\tintro_file:\t%s\n", intro_file);
    info_printf("\toutro_file:\t%s\n", outro_file);
    info_printf("\tfps:\t%d\n", frames_per_second);
    info_printf("\twidth:\t%d\n", width);
    info_printf("\theight:\t%d\n", height);
    info_printf("\n\n\targuments left:\t%d\n\n", argc_new);
}

void print_file_chain_element(sourcefile_t * file){
	info_printf("File: '%s'\tat offset %" PRId64 "\thaving %" PRId64 " bytes\n", file->filename, file->globalpos, file->filesize);
}


void print_file_chain(const char * name, sourcefile_t * list){
	if (list == NULL) {
		info_printf("Empty file chain!\n");
	}
	info_printf("Dumping file chain %s:\n", name);
	while (list != NULL) {
		print_file_chain_element(list);
		list = list->next;
	}
	info_printf("End of file chain\n");
}

#ifdef DEBUG

void debug_printf(const char * text, ...) {
	va_list args;
	va_start(args, text);
	vfprintf(logging, text, args);
	va_end(args);
	fflush(logging);
}

#endif

static void internal_log_append(const char * text, va_list args) {
	char *buf = (char*) malloc(MAX_LOGBUFFER_SIZE);
	CHECK_OOM(buf);

	vsnprintf(buf, MAX_LOGBUFFER_SIZE, text, args);

	if (MAX_LOGBUFFER_SIZE - filebuffer__contentsize(log_filebuffer) < safe_strlen(buf)) {
		size_t copylength = MAX_LOGBUFFER_SIZE - safe_strlen(buf);
		size_t currentlogsize = filebuffer__contentsize(log_filebuffer);
		char *buf2 = (char*) malloc(copylength);
		CHECK_OOM(buf2);
		copylength = filebuffer__read(log_filebuffer, currentlogsize - copylength, buf2, copylength);
		filebuffer_t* logbuf = filebuffer__new();
		filebuffer__append(logbuf, buf2, copylength);
		filebuffer_t* old = log_filebuffer;
		log_filebuffer = logbuf;
		filebuffer__destroy(old);
	}
	filebuffer__append(log_filebuffer, buf, safe_strlen(buf));
}

void error_printf(const char * text, ...) {
	va_list args;
	va_start(args, text);
	vfprintf(logging, text, args);
	va_end(args);
	fflush(logging);

	// print again into log buffer
	char *buf = (char*) malloc(MAX_LOGBUFFER_SIZE);
	CHECK_OOM(buf);

	va_start(args, text);
	internal_log_append(text, args);
	va_end(args);
}

void info_printf(const char * text, ...) {
	va_list args;
#ifdef DEBUG
	va_start(args, text);
	vfprintf(logging, text, args);
	va_end(args);
	fflush(logging);
#endif
	// print again into log buffer
	char *buf = (char*) malloc(MAX_LOGBUFFER_SIZE);
	CHECK_OOM(buf);

	va_start(args, text);
	internal_log_append(text, args);
	va_end(args);
}

