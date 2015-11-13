#define DEBUG 1

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <dirent.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>

#include "config.h"
#include "fuse-vdv.h"
#include "fuse-vdv-tools.h"
#include "fuse-vdv-filelist.h"
#include "fuse-vdv-filebuffer.h"
#include "fuse-vdv-opts.h"
#include "fuse-vdv-debug.h"
#include "fuse-vdv-kdenlive.h"
#include "fuse-vdv-smoothsort.h"


// global variables of fuse-vdv.c :

FILE* logging = NULL;
char * base_dir = NULL;
char * start_time = NULL;
char * prefix = NULL;
int inframe = 0;
int outframe = -1;
int lastframe = -1;
int slidemode = 0;
const char *rawName = "/uncut.dv";
const char *cutName = "/cut.dv";
const char *cutCompleteName = "/cut-complete.dv";
char * intro_file = NULL;
char * outro_file = NULL;
char * pid = NULL;
char * inframe_str = NULL;
size_t inframe_str_length = 0;
char * outframe_str = NULL;
size_t outframe_str_length = 0;
char * mountpoint = NULL;
sourcefile_t * sourcefiles = NULL;
int sourcefiles_c = 0;
off_t file_length = 0;
int palimmpalimm = 0;
volatile int filelock = 0;
sourcefile_t ** filechains = NULL;
int filechains_size = 0;
fileposhint_t **filehints = NULL;
int filehints_size = 0;
sourcefile_t * sourcefiles_cut = NULL;
int sourcefiles_cut_c = 0;
off_t cut_file_length = 0;
sourcefile_t * sourcefiles_cutcomp = NULL;
int sourcefiles_cutcomp_c = 0;
off_t cutcomp_file_length = 0;
// static int cutmarks_tainted = 0;


// main test routine


int main(int argc, char *argv[]) {
	logging = stdout;

	printf("equalize 2010.12.30_20-15-23 : '%s'\n", equalize_date_string("2010.12.30_20-15-23"));
	printf("equalize 2010-12-30-20-15-23 : '%s'\n", equalize_date_string("2010.12.30_20-15-23"));
	printf("equalize 2010.12.30 : '%s'\n", equalize_date_string("2010.12.30"));
	printf("equalize NULL : '%s'\n", equalize_date_string((const char *)0));

	printf("date_compare 2010.12.30_20-15-23 and 2010.12.30_20-15-23 : '%d'\n", compare_date_strings("2010.12.30_20-15-23", "2010.12.30_20-15-23"));
	printf("date_compare 2010.12.30_20-15-23 and 2010-12-30-20-15-23 : '%d'\n", compare_date_strings("2010.12.30_20-15-23", "2010.12.30_20-15-23"));
	printf("date_compare 2010.12.31_20-15-23 and 2010-12-30-20-15-23 : '%d'\n", compare_date_strings("2010.12.31_20-15-23", "2010.12.30_20-15-23"));
	printf("date_compare 2010.12.30_20-15-23 and 2010-12-29-20-15-23 : '%d'\n", compare_date_strings("2010.12.30_20-15-23", "2010.12.29_20-15-23"));
	printf("date_compare 2010.12.30_20-15-23 and 2010-12-29-20-15-24 : '%d'\n", compare_date_strings("2010.12.30_20-15-23", "2010.12.29_20-15-24"));
	printf("date_compare 2010.12.30_20-15-24 and 2010-12-29-20-16-24 : '%d'\n", compare_date_strings("2010.12.30_20-15-24", "2010.12.29_20-16-24"));
	printf("date_compare 2010.12.30_20-15-24 and NULL : '%d'\n", compare_date_strings("2010.12.30_20-15-24", (const char*)0));
	printf("date_compare NULL and NULL : '%d'\n", compare_date_strings((const char*)0, (const char*)0));

	printf("date_to_stamp 2010.12.30_20-15-24 : '%08d'\n", datestring_to_timestamp("2010.12.30_20-15-24"));
	printf("date_to_stamp 2010.12.31_20-15-24 : '%08d'\n", datestring_to_timestamp("2010.12.31_20-15-24"));
	printf("date_to_stamp 2010.12.30_21-15-24 : '%08d'\n", datestring_to_timestamp("2010.12.30_21-15-24"));
	printf("date_to_stamp 2010.12.30_20-16-24 : '%08d'\n", datestring_to_timestamp("2010.12.30_20-16-24"));
	printf("date_to_stamp 2010.12.30_20-15-25 : '%08d'\n", datestring_to_timestamp("2010.12.30_20-15-25"));

	printf("get_datestring_from_filename NULL : '%s'\n", get_datestring_from_filename((const char*)0));
	printf("get_datestring_from_filename '2010.12.30_20-15-25' : '%s'\n", get_datestring_from_filename("2010.12.30_20-15-25"));
	printf("get_datestring_from_filename '/2010.12.30_20-15-25' : '%s'\n", get_datestring_from_filename("/2010.12.30_20-15-25"));
	printf("get_datestring_from_filename '/2010.12.30_20-15-25.dv' : '%s'\n", get_datestring_from_filename("/2010.12.30_20-15-25.dv"));
	printf("get_datestring_from_filename '/foo/bar/2010.12.30_20-15-25' : '%s'\n", get_datestring_from_filename("/foo/bar/2010.12.30_20-15-25"));
	printf("get_datestring_from_filename '/foo/bar/2010.12.30_20-15-25.dv' : '%s'\n", get_datestring_from_filename("/foo/bar/2010.12.30_20-15-25.dv"));
	printf("get_datestring_from_filename '/foo/bar/prefix-2010.12.30_20-15-25.dv' : '%s'\n", get_datestring_from_filename("/foo/bar/prefix-2010.12.30_20-15-25.dv"));

// Smoothsort Test:

	printf("starting sort tests\n");
	sourcefile_t * list = NULL;
	list = list_insert (list, new_file_entry_absolute_path("/abcdedf"));
	list = list_insert (list, new_file_entry_absolute_path("/1234"));
	list = list_insert (list, new_file_entry_absolute_path("/asdf7324"));
	list = list_insert (list, new_file_entry_absolute_path("/sadf7w9r."));
	list = list_insert (list, new_file_entry_absolute_path("/asdann342"));
	list = list_insert (list, new_file_entry_absolute_path("/aeb4320asdc"));
	list = list_insert (list, new_file_entry_absolute_path("/w43454a"));
	list = list_insert (list, new_file_entry_absolute_path("/8734adc"));
	list = list_insert (list, new_file_entry_absolute_path("/zasdc"));

	print_file_chain("unsortierte Liste", list);
	list = smoothsort_list(list);
	print_file_chain("sortierte Liste (smoothsort)", list);

// Filebuffer Test:

	filebuffer_t* fb = filebuffer__new();
	assert(fb != NULL);
	char * dummy = (char*) malloc(128);
	assert(dummy != NULL);
	memset(dummy, 0, 128);
	size_t read = filebuffer__read (fb, 0, dummy, 1);
	assert (read == 0);
	printf("output of empty read (%d bytes): '%s'\n", read, dummy);
	memset(dummy, 0, 128);

	size_t written = filebuffer__write(fb, "1234", 4, 0);
	printf("return of write: %d bytes\n", written);
	assert (written == 4);
	written = filebuffer__write(fb, "5678", 4, 4);
	printf("return of write: %d bytes\n", written);
	assert (written == 4);

	size_t contentsize = filebuffer__contentsize(fb);
	assert (contentsize == 8);
	
	read = filebuffer__read (fb, 2, dummy, 4);
	printf("return of read (4 bytes from offset 2): '%d'\n", read);
	assert (read == 4);
	printf("returned string: '%s'\n", dummy);
	assert (strncmp(dummy, "3456", 4) == 0);
	memset(dummy, 0, 128);

	filebuffer__truncate(fb, 4);
	contentsize = filebuffer__contentsize(fb);
	assert (contentsize == 4);

	read = filebuffer__read (fb, 2, dummy, 4);
	printf("return of read (4 bytes from offset 2): '%d'\n", read);
	assert (read == 2);
	printf("returned string: '%s'\n", dummy);
	assert (strncmp(dummy, "34", 2) == 0);
	memset(dummy, 0, 128);

	written = filebuffer__write(fb, "56", 2, 4);
	printf("return of write: %d bytes\n", written);
	assert (written == 2);
	contentsize = filebuffer__contentsize(fb);
	printf("contentsize: %d \n", contentsize);
	assert (contentsize == 6);

	return 0;
}

