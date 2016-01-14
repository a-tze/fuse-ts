#ifndef _FUSE_MAIN_H
#define _FUSE_MAIN_H

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

// non-volatile parameters
static const int frames_per_second = 25;
static const int frame_duration_ms = 40; // frame duration in milliseconds
static const int read_block_size = 65536;

// "instance" variables:
extern FILE* logging;

extern char * base_dir ;
extern char * start_time;
extern char * prefix;
extern int inframe;
extern int outframe;
extern int blanklen;
extern char * intime;
extern char * outtime;
extern int slidemode;
extern char * intro_file;
extern char * outro_file;
extern char * winpath;
extern int winpath_stripslashes;
extern off_t outbyte;
extern int numfiles;
extern int totalframes;

extern char * mountpoint;

// constants

extern const char *rawName;
extern const char *durationName;


typedef struct _sourcefile {
  char * filename;
  off_t globalpos; // excluding the bytes of this piece!
  int repeat;  // # of repetitions in slidemode
  FILE* fhandle;
  struct _sourcefile* prev; // NULL in the list head
  struct _sourcefile* next; // NULL in the list tail
  // these ones are meant for list heads:
  struct _sourcefile* tailhelper; 
  int refcnt;
  off_t filesize; // size of this piece in bytes
  off_t totalsize; // cumulation of bytes including this piece
} sourcefile_t;

typedef struct _fileposhint {
	off_t lastpos;
	sourcefile_t * lastpiece;
} fileposhint_t;

extern sourcefile_t * sourcefiles;
extern int sourcefiles_c;
extern sourcefile_t ** filechains;
extern int filechains_size;
extern fileposhint_t **filehints;
extern int filehints_size;

extern void check_signal(void);
extern sourcefile_t * init_sourcefiles();
extern void update_cutmarks_from_strings();
extern void update_cutmarks_from_numbers();
extern void prepare_file_attributes(sourcefile_t * list);
#endif


