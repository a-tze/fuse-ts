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
static const int frame_size = 144000;
static const int frames_per_second = 25;


// "instance" variables:
extern FILE* logging;

extern char * base_dir ;
extern char * start_time;
extern char * prefix;
extern int inframe;
extern int outframe;
extern int blanklen;
extern int lastframe;
extern char * intro_file;
extern char * outro_file;
extern int slidemode;

extern char * mountpoint;

// constants

extern const char *rawName;
extern const char *cutName;
extern const char *cutCompleteName;


typedef struct _sourcefile {
  char * filename;
  off_t globalpos; // excluding the bytes of this piece!
  off_t startpos;
  off_t endpos;
  int frames;
  int gframes; //including the frames of this piece
  FILE* fhandle;
  struct _sourcefile* prev; // NULL in the list head
  struct _sourcefile* next; // NULL in the list tail
  // these ones are meant for list heads:
  struct _sourcefile* tailhelper; // OBSOLETE!
  int refcnt;
  off_t totalsize;
  
  size_t avi_size;
  off_t avi_globalpos; // excluding the bytes of this piece!
  
  unsigned char *avi_movilist;
  unsigned char *avi_idx;
  int avi_idx_size;
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
extern void update_cut_filechains();
extern void update_cutmarks();
extern sourcefile_t * cut_and_merge(sourcefile_t * list, int inframe, int outframe, 
  char* intro_file, char * outro_file, int* piece_count);
extern void prepare_file_attributes(sourcefile_t * list);
extern int vdv_dv_do_read(sourcefile_t* file, char *buf, size_t size, off_t fileoffset);
#endif


