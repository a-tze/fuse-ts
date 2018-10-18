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
#include <time.h>
#include <pthread.h>
#include <linux/xattr.h>
#include "fuse-ts.h"
#include "fuse-ts-tools.h"
#include "fuse-ts-filelist.h"
#include "fuse-ts-filebuffer.h"
#include "fuse-ts-opts.h"
#include "fuse-ts-debug.h"
#include "fuse-ts-kdenlive.h"
#include "fuse-ts-smoothsort.h"
#include "fuse-ts-knowledge.h"
#include "fuse-ts-shotcut.h"

// "instance" variables:
FILE *logging = NULL;

char *base_dir = NULL;
char *start_time = NULL;
char *prefix = NULL;
int inframe = 0;
int outframe = 0;
int frames_per_second = 25;
int blanklen = 0;
char * intime = NULL;
char * outtime = NULL;
int numfiles = -1;
int totalframes = -1;
int width = 1920;
int height = 1280;
off_t outbyte = 0;
int slidemode = 0;
const char *rawName = "/uncut.ts";
const char *durationName = "/duration";

char *intro_file = NULL;
char *outro_file = NULL;
char *pid = NULL;
char *intime_str = NULL;
size_t intime_str_length = 0;
char *outtime_str = NULL;
size_t outtime_str_length = 0;
char *duration_str = NULL;
size_t duration_str_length = 0;
char *time_str = NULL;
size_t time_str_length = 0;
char *inframe_str = NULL;
char *outframe_str = NULL;
size_t inframe_str_length = 0;
size_t outframe_str_length = 0;

char *mountpoint = NULL;
time_t crtime = 0;

// used for storing glusters gfid:

char* gfid[17] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
size_t gfid_length[14];

// mutex for global operations (not on filehandles)
static pthread_mutex_t globalmutex = PTHREAD_MUTEX_INITIALIZER;

// used after initialization
sourcefile_t *sourcefiles = NULL;
int sourcefiles_c = 0;
off_t file_length = 0;
int palimmpalimm = 0;
volatile int filelock = 0;

sourcefile_t **filechains = NULL;
int filechains_size = 0;
fileposhint_t **filehints = NULL;
int filehints_size = 0;

filebuffer_t* filelist_filebuffer = NULL;

static int pid_nr = 0;

static int ts_getattr (const char *path, struct stat *stbuf) {
	debug_printf ("getattr called for '%s'\n", path);
	int res = 0;
	memset (stbuf, 0, sizeof (struct stat));
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) return -ENOENT;
	if ((entrynr == INDEX_KDENLIVE || entrynr == INDEX_SHOTCUT)
		&& totalframes < 0)
		return -ENOENT;

	stbuf->st_ino = (pid_nr << 16) | entrynr;
	stbuf->st_mode = S_IFREG | 0444;
	stbuf->st_nlink = 1;
	stbuf->st_mtime = crtime;
	stbuf->st_ctime = crtime;

	switch(entrynr) {
	case INDEX_ROOTDIR:
		stbuf->st_mode = S_IFDIR | 0777;
		stbuf->st_nlink = 2;
		break;
	case INDEX_RAW:
		stbuf->st_size = file_length;
		break;
	case INDEX_PID:
		stbuf->st_size = safe_strlen (pid);
		break;
	case INDEX_OPTS:
		stbuf->st_size = opts_length ();
		break;
	case INDEX_INTIME:
		stbuf->st_size = intime_str_length;
		break;
	case INDEX_OUTTIME:
		stbuf->st_size = outtime_str_length;
		break;
	case INDEX_INFRAME:
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_size = inframe_str_length;
		break;
	case INDEX_OUTFRAME:
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_size = outframe_str_length;
		break;
	case INDEX_DURATION:
		stbuf->st_size = duration_str_length;
		break;
	case INDEX_KDENLIVE:
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_size = 0;
		stbuf->st_size = get_kdenlive_project_file_size (rawName + 1, totalframes, blanklen);
		break;
	case INDEX_SHOTCUT:
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_size = 0;
		stbuf->st_size = get_shotcut_project_file_size (rawName + 1, totalframes, blanklen);
		break;
	case INDEX_REBUILD:
		stbuf->st_mode = S_IFREG | 0200;
		stbuf->st_size = 0;
		break;
	case INDEX_FILELIST:
		stbuf->st_size = filebuffer__contentsize(filelist_filebuffer);
		break;
	default:
		return -ENOENT;
	}
	return res;
}

static int ts_readdir (const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	(void) offset;
	(void) fi;

	debug_printf ("readdir called on '%s'\n", path);
	if (strcmp (path, "/") != 0)
		return -ENOENT;

	filler (buf, ".", NULL, 0);
	filler (buf, "..", NULL, 0);
	filler (buf, rawName + 1, NULL, 0);
	if (totalframes >= 0) {
		filler (buf, kdenlive_path + 1, NULL, 0);
		filler (buf, shotcut_path + 1, NULL, 0);
	}
	filler (buf, opts_path + 1, NULL, 0);
	filler (buf, "pid", NULL, 0);
	filler (buf, "intime", NULL, 0);
	filler (buf, "outtime", NULL, 0);
	filler (buf, "duration", NULL, 0);
	filler (buf, "inframe", NULL, 0);
	filler (buf, "outframe", NULL, 0);
	filler (buf, "rebuild", NULL, 0);
	filler (buf, "filelist", NULL, 0);

	return 0;
}

static int ts_open (const char *path, struct fuse_file_info *fi) {
	debug_printf ("open called on '%s'\n", path);
	fi->fh = 0;
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) return -ENOENT;
	switch(entrynr) {
	case INDEX_RAW:
		if (fi->flags & (O_WRONLY | O_RDWR))
			return -EACCES;
		check_signal ();
		pthread_mutex_lock (&globalmutex);
		fi->fh = insert_into_filechains_list (sourcefiles) + 1;
		fi->keep_cache = 1;
		pthread_mutex_unlock (&globalmutex);
		break;
	case INDEX_REBUILD:
		if ((fi->flags & O_WRONLY) != O_WRONLY)
			return -EACCES;
		palimmpalimm = 1;
		check_signal ();
		break;
	case INDEX_KDENLIVE:
		if (totalframes < 0)
			return -ENOENT;
		open_kdenlive_project_file (rawName + 1, totalframes, blanklen, ((fi->flags & O_TRUNC) > 0));
		return 0;
	case INDEX_SHOTCUT:
		if (totalframes < 0)
			return -ENOENT;
		open_shotcut_project_file (rawName + 1, totalframes, blanklen, ((fi->flags & O_TRUNC) > 0));
		return 0;
	case INDEX_PID:
	case INDEX_INTIME:
	case INDEX_OUTTIME:
	case INDEX_INFRAME:
	case INDEX_OUTFRAME:
	case INDEX_OPTS:
	case INDEX_DURATION:
	case INDEX_FILELIST:
	case INDEX_ROOTDIR:
		return 0;
	default:
		debug_printf ("Path not found: '%s' \n", path);
		return -ENOENT;
	}
	return 0;
}

static int ts_truncate (const char *path, off_t size) {
	debug_printf ("truncate called on '%s' with size %" PRId64 "\n", path, size);
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) return -ENOENT;
	size_t tmp;
	switch(entrynr) {
	case INDEX_KDENLIVE:
		return truncate_kdenlive_project_file(size);
	case INDEX_SHOTCUT:
		return truncate_shotcut_project_file(size);
	case INDEX_INFRAME:
		tmp = truncate_buffer(&inframe_str, inframe_str_length, size);
		if (tmp < 0 || tmp < size) {
			return -EIO;
		}
		inframe_str_length = tmp;
		return 0;
	case INDEX_OUTFRAME:
		tmp = truncate_buffer(&outframe_str, outframe_str_length, size);
		if (tmp < 0 || tmp < size) {
			return -EIO;
		}
		outframe_str_length = tmp;
		return 0;
	}
	return -EACCES;
}

int ts_data_do_read (sourcefile_t * file, char *buf, size_t size, off_t fileoffset) {
	if (file == NULL) {
		debug_printf ("Something went terribly wrong while reading: filepiece pointer NULL was given\n");
		return 0;
	}
	if (file->filename == NULL) {
		debug_printf ("Something went terribly wrong while reading: filepiece has NULL filename\n");
		return 0;
	}
	//debug_printf("trying to give out %d bytes at position %" PRId64 " of file '%s'\n", size, fileoffset, file->filename);
	if (file->fhandle == NULL) {
		file->fhandle = fopen (file->filename, "r");
		if (file->fhandle == NULL) {
			// the file is not accessible. what now? panic!
			debug_printf ("ERROR: can not open file '%s' for reading!\n", file->filename);
			fprintf (logging, "ERROR: can not open file '%s' for reading!\n", file->filename);
			return 0;
			//exit(121);
		}
	}
	assert (file->fhandle != NULL);
	FILE *f = file->fhandle;
	if (slidemode == 0) {
		fseek (f, fileoffset, SEEK_SET);
		size_t rsize = fread (buf, 1, size, f);
		size_t rsize_cum = rsize;
		debug_printf ("successfully read %d bytes of file '%s'\n", rsize, file->filename);
		while (rsize_cum < size) {
			debug_printf ("continuing read of '%s'\n", file->filename);
			rsize = fread (buf + rsize_cum, 1, file->filesize - rsize_cum, f);
			if (rsize <= 0) {
				debug_printf ("ERROR: file could not be read completely: '%s' !\n", file->filename);
				fprintf (logging, "ERROR: file could not be read completely: '%s' !\n", file->filename);
				return rsize_cum;
			}
			debug_printf ("successfully read %d bytes of file '%s'\n", rsize, file->filename);
			rsize_cum += rsize;
		} 
		return rsize_cum;
	} else {
		void * framebuf = malloc(file->filesize);
		if (framebuf == NULL) {
			debug_printf ("ERROR: cannot allocate memory\n");
			fprintf (logging, "ERROR: cannot allocate memory\n");
			return 0;
		}
		fseek (f, 0, SEEK_SET);
		size_t rsize = fread (framebuf, 1, file->filesize, f);
		size_t rsize_cum = rsize;
		debug_printf ("successfully read %d bytes of file '%s'\n", rsize, file->filename);
		while (rsize_cum < file->filesize) {
			debug_printf ("continuing read of '%s'\n", file->filename);
			rsize = fread (framebuf + rsize_cum, 1, file->filesize - rsize_cum, f);
			if (rsize <= 0) {
				debug_printf ("ERROR: file could not be read completely: '%s' !\n", file->filename);
				fprintf (logging, "ERROR: file could not be read completely: '%s' !\n", file->filename);
				free(framebuf);
				return 0;
			}
			debug_printf ("successfully read %d bytes of file '%s'\n", rsize, file->filename);
			rsize_cum += rsize;
		} 
		size_t remaining = size;
		off_t realoffset = fileoffset % file->filesize;
		while (remaining + realoffset >= file->filesize) {
			memcpy(buf, framebuf + realoffset, file->filesize - realoffset);
			remaining -= (file->filesize - realoffset);
			buf += (file->filesize - realoffset);
			realoffset = 0;
		}
		memcpy(buf, framebuf + realoffset, remaining);
		free(framebuf); 
		return size;
	}
}

static int ts_data_read (uint64_t filehandle, sourcefile_t * list, char *buf, size_t size, off_t offset, int depth) {

	debug_printf ("someone wants to read %d bytes at position %" PRId64 " (depth %d)\n", size, offset, depth);
	if ((list == NULL) || (offset < 0) || (size == 0) || (depth > 10)) {
		return 0;
	}
	if (depth == 0) {
		// no reading over the end of the virtual file...
		if (offset >= list->totalsize) {
			return 0;
		}
		if (offset + size > list->totalsize) {
			size = list->totalsize - offset;
		}
	}
	assert (list->globalpos <= offset);
	// get correct file
	sourcefile_t *t = list;
	t = get_sourcefile_for_position (t, offset);
	assert (t != NULL);

	off_t start = offset - t->globalpos;
	size_t readsize; // how many bytes are readable in the piece the offset points to
	if (slidemode == 0) {
		readsize = (size_t) ((off_t) t->filesize - start);
	} else {
		readsize = size;
	}
	if (readsize >= size) {
		return ts_data_do_read (t, buf, size, start);
	} else {
		return ts_data_do_read (t, buf, readsize, start) + ts_data_read (filehandle, t, buf + readsize, size - readsize, offset + readsize, ++depth);
	}
}

static int ts_read (const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	debug_printf ("someone wants to read %d bytes at position %" PRId64 " of '%s'\n", size, offset, path);
	if (fi->fh > 0) {
		if (filechains_size >= fi->fh) {
			return ts_data_read (fi->fh, filechains[fi->fh - 1], buf, size, offset, 0);
		} else {
			// There was no fopen before
			error_printf ("Error: unknown filehandle: %d \n", fi->fh);
			return 0;
		}
	}
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) return -ENOENT;
	size_t ret = 0;
	char *t = NULL;
	switch(entrynr) {
	case INDEX_PID:
		return string_read (pid, buf, size, offset);
	case INDEX_INTIME:
		return string_read_with_length (intime_str, buf, size, offset, intime_str_length);
	case INDEX_OUTTIME:
		return string_read_with_length (outtime_str, buf, size, offset, outtime_str_length);
	case INDEX_DURATION:
		return string_read_with_length (duration_str, buf, size, offset, duration_str_length);
	case INDEX_INFRAME:
		return string_read_with_length (inframe_str, buf, size, offset, inframe_str_length);
	case INDEX_OUTFRAME:
		return string_read_with_length (outframe_str, buf, size, offset, outframe_str_length);
	case INDEX_FILELIST:
		return filebuffer__read(filelist_filebuffer, offset, buf, size);
	case INDEX_OPTS:
		t = get_opts ();
		ret = string_read (t, buf, size, offset);
		free (t);
		return ret;
	case INDEX_KDENLIVE:
		if (totalframes < 0)
			return -ENOENT;
		return kdenlive_read (path, buf, size, offset, rawName, totalframes, blanklen);
	case INDEX_SHOTCUT:
		if (totalframes < 0)
			return -ENOENT;
		return shotcut_read (path, buf, size, offset, rawName, totalframes, blanklen);
	}
	debug_printf ("Path not found: '%s' \n", path);
	return -ENOENT;
}

int ts_write (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *info) {
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) return -ENOENT;
	switch(entrynr) {
	case INDEX_KDENLIVE:
		return write_kdenlive_project_file (buf, size, offset);
	case INDEX_SHOTCUT:
		return write_shotcut_project_file (buf, size, offset);
	case INDEX_INFRAME:
		return write_to_buffer (buf, size, offset, &inframe_str, &inframe_str_length);
	case INDEX_OUTFRAME:
		return write_to_buffer (buf, size, offset, &outframe_str, &outframe_str_length);
	case INDEX_REBUILD:
		return size;
	}

	return -EACCES;
}

int ts_release (const char *filename, struct fuse_file_info *info) {
	debug_printf ("release called on '%s'\n", filename);
	int entrynr = get_index_from_pathname(filename);
	if (entrynr < 0) return -ENOENT;
	switch(entrynr) {
	case INDEX_RAW:
		pthread_mutex_lock (&globalmutex);
		remove_from_filechains_list (info->fh - 1);
		pthread_mutex_unlock (&globalmutex);
		break;
	case INDEX_KDENLIVE:
		if (totalframes < 0)
			return -ENOENT;
		if (find_cutmarks_in_kdenlive_project_file (&inframe, &outframe, &blanklen) == 0) {
			pthread_mutex_lock (&globalmutex);
			rebuild_opts ();
			update_cutmarks_from_numbers();
			pthread_mutex_unlock (&globalmutex);
		}
		close_kdenlive_project_file ();
		break;
	case INDEX_SHOTCUT:
		if (totalframes < 0)
			return -ENOENT;
		if (find_cutmarks_in_shotcut_project_file (&inframe, &outframe, &blanklen) == 0) {
			pthread_mutex_lock (&globalmutex);
			rebuild_opts ();
			update_cutmarks_from_numbers();
			pthread_mutex_unlock (&globalmutex);
		}
		close_shotcut_project_file ();
		break;
	case INDEX_INFRAME:
	case INDEX_OUTFRAME:
		pthread_mutex_lock (&globalmutex);
		rebuild_opts ();
		update_cutmarks_from_strings();
		pthread_mutex_unlock (&globalmutex);
		break;
	}
	return 0;
}

void *ts_init (void) {
	pid_nr = getpid();
	pid = update_int_string (pid, pid_nr, NULL);
	return NULL;
}

int ts_utime (const char *path, struct utimbuf *b) {
	debug_printf ("utime called on '%s'\n", path);
	return 0;
}

int ts_getxattr (const char *path, const char *name, char *value, size_t vlen ) {
	debug_printf ("getxattr for '%s' called on '%s', buffer length is %d\n", name, path, vlen);
	if (strcmp (name, "trusted.gfid") != 0) {
//		return -126; //-ENOATTR;
		return -EOPNOTSUPP;
	}

	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0 || gfid[entrynr] == NULL) {
		debug_printf ("getxattr: attribute not present at present or unknown path\n");
		return -126; //-ENOATTR;
//		return 0;
	}
	if (gfid_length[entrynr] <= 0) {
		debug_printf ("getxattr: attribute has non-positive length\n");
		return -ENODATA;
//		return 0;
	}
	if (vlen == 0) {
		//asks for buffer size
		debug_printf ("getxattr: returning buffer size %d\n", gfid_length[entrynr]);
		return gfid_length[entrynr];
	}
	if (vlen < gfid_length[entrynr]) {
		//buffer too small
		debug_printf ("getxattr: caller's attribute buffer is too small\n");
		return -ERANGE;
	}
	debug_printf ("getxattr: returning '%s' with length %d\n", gfid[entrynr], gfid_length[entrynr]);
	memcpy(value, gfid[entrynr], gfid_length[entrynr]);
	return gfid_length[entrynr];
}

int ts_setxattr (const char * path, const char *name, const char *value, size_t vlen, int flags) {
	debug_printf ("setxattr for '%s' called on '%s', value is '%s', length is %d, flags is %0x\n", 
		name, path, value, vlen, flags);
	if (strcmp (name, "trusted.gfid") != 0) return 0;
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) {
		debug_printf ("setxattr: unknown path\n");
		return -ENOENT;
	}
	int exists = 0;
	if (entrynr >= 0 && gfid[entrynr] != NULL) {
		exists = 1;
	}
	// evaluate flags
	if (((flags | XATTR_CREATE) == XATTR_CREATE) && exists > 0) {
		// fail if attribute exists
		debug_printf ("setxattr: failing because attribute already exists and create flag given\n");
		return -EEXIST;
	}
	if (((flags | XATTR_REPLACE) == XATTR_REPLACE) && exists == 0) {
		// fail if attribute doesn't exist
		debug_printf ("setxattr: failing because attribute doesn't exist and replace flag given\n");
		return -126; //-ENOATTR;
	}

	// clear existing value
	if (exists > 0 && gfid_length[entrynr] > 0 && gfid[entrynr] != NULL) {
		debug_printf ("setxattr: clearing old attribute value @%p\n", gfid[entrynr]);
		free(gfid[entrynr]);
		gfid[entrynr] = NULL;
		gfid_length[entrynr] = 0;
	}
	// save new value
	if (vlen > 0) {
		//buffer too small
		debug_printf ("setxattr: storing attribute\n");
		char * temp = malloc(vlen + 1);
		if (temp == NULL) {
			// no memory left
			debug_printf ("setxattr: memory allocation failure\n");
			return -ENOSPC;
		}
		gfid[entrynr] = temp;
		memcpy(gfid[entrynr], value, vlen);
		gfid[entrynr][vlen] = 0;
	}
	gfid_length[entrynr] = vlen;
	debug_printf ("setxattr: returning success\n");
	return 0;
}

static struct fuse_operations ts_oper = {
	.init = ts_init,
	.getattr = ts_getattr,
	.getxattr = ts_getxattr,
	.setxattr = ts_setxattr,
	.readdir = ts_readdir,
	.open = ts_open,
	.read = ts_read,
	.write = ts_write,
	.release = ts_release,
	.truncate = ts_truncate,
	.utime = ts_utime,
};


/////////////////////////////////////////////

void check_signal (void) {
	debug_printf ("checking if signal has been set\n");
	if (palimmpalimm > 0) {
		debug_printf ("signal has been set, rebuilding file chain\n");

		// here should be locking against the lock in ts_open
		debug_printf ("no files opening right now\n");

		palimmpalimm = 0;
		sourcefile_t *newsources = init_sourcefiles ();
		int newsize = 0;
//		newsources = cut_and_merge (newsources, 0, lastframe, NULL, NULL, &newsize);
		if (sourcefiles->refcnt == 0) {
			purge_list (sourcefiles);
		} else {
			sourcefiles->refcnt--;
		}
		sourcefiles_c = newsize;
		sourcefiles = newsources;
		prepare_file_attributes (sourcefiles);
		rebuild_opts ();
		// end of lock
	}
}

void update_times_from_cutmarks() {
	char *intime = frames_to_seconds (inframe, frames_per_second);
	char *outtime = frames_to_seconds (outframe, frames_per_second);
	char *duration = frames_to_seconds (outframe - inframe, frames_per_second);
	intime_str = update_string_string (intime_str, intime, &intime_str_length);
	outtime_str = update_string_string (outtime_str, outtime, &outtime_str_length);
	duration_str = update_string_string (duration_str, duration, &duration_str_length);
	debug_printf("intime is %s, outtime is %s, duration is %s\n", intime_str, outtime_str, duration_str);
}

void update_cutmarks_from_numbers () {
	inframe_str = update_int_string (inframe_str, inframe, &inframe_str_length);
	outframe_str = update_int_string (outframe_str, outframe, &outframe_str_length);
	update_times_from_cutmarks();
}

void update_cutmarks_from_strings () {
	int t;
	if (inframe_str_length > 0) {
		t = atoi (inframe_str);
		if ((t < 0) || (t > 1080000 /* 12 hours */ )) {
			debug_printf ("Error: new inframe is too big (%d)!\n", t);
		} else {
			inframe = t;
		}
	}
	if (outframe_str_length > 0) {
		t = atoi (outframe_str);
		if ((t < 0) || (t > 1080000 /* 12 hours */ )) {
			debug_printf ("Error: new outframe is too big (%d)!\n", t);
		} else {
			outframe = t;
		}
	}
	update_times_from_cutmarks();
}

sourcefile_t *init_sourcefiles () {
	int found = 0;
	sourcefile_t *finallist = NULL;

	sourcefile_t *files = get_files_with_prefix (prefix, &found);
	if (found == 0) {
		error_printf ("Error: No files found!\n");
		exit (112);
	}
	files = smoothsort_list (files);
	found = list_count (files);
	if (found == 0) {
		error_printf ("Error: No files left in list!\n");
		exit (113);
	}
	sourcefile_t *iter = files;
	char *prefix_with_path = get_prefix_with_path ();
	int i = 0;
	int n = 0;
	for (; i < found; i++) {
		int comp = strcmp (prefix_with_path, iter->filename);
		if (comp <= 0) {
			n++;
			finallist = add_file_to_list (finallist, iter);
			if (0 != slidemode) {
				reorganize_slide_list(finallist);
			}
			if (outbyte > 0 && get_list_tail (finallist)->totalsize >= outbyte) {
				debug_printf("Stop searching for input files because outbyte bytes (%" PRId64 ") have been found.\n", outbyte);
				break;
			}
			if (numfiles > 0 && n >= numfiles) {
				debug_printf("Stop searching for input files because numfiles files (%d) have been found.\n", numfiles);
				break;
			}
		}
		iter = iter->next;
	}
	purge_list (files);
	iter = get_list_tail (finallist);
	if (iter == NULL) {
		error_printf ("I did not register any files!\n");
	} else {
		debug_printf ("I registered %d input files with a total of %" PRId64 " bytes.\n", list_count (finallist), iter->totalsize);
	}
	return finallist;
}

void prepare_file_attributes (sourcefile_t * list) {
	if (list == NULL) {
		return;
	}
	list->refcnt = 1;
	sourcefile_t *last = get_list_tail (list);
	if (last == NULL) {
		return;
	}
	file_length = (off_t) last->globalpos + last->filesize;
	list->totalsize = file_length;
	init_kdenlive_project_file ();
	init_shotcut_project_file ();
}

void create_filelist (sourcefile_t * list) {
	if (filelist_filebuffer == NULL) {
		filelist_filebuffer = filebuffer__new();
	}
	sourcefile_t *last = get_list_tail (list);
	if (last == NULL) {
		return;
	}
	sourcefile_t *i = list;
	const char* separator = "\n";
	while (i != NULL) {
		filebuffer__append(filelist_filebuffer, i->filename, safe_strlen(i->filename));
		filebuffer__append(filelist_filebuffer, separator, safe_strlen(separator));
		i = i->next;
	}
}

void handle_sigusr1 (int s) {
	if (s != SIGUSR1) {
		return;
	}
	palimmpalimm = 1;
	if (logging != NULL) {
		fflush (logging);
	}
}


int main (int argc, char *argv[]) {

	logging = stderr;

	if (argc < 3) {
		print_usage ();
		exit (100);
	}
	int argc_new = argc;
	char **argv_new = argv;
	parse_opts (&argc_new, &argv_new);
	time(&crtime);
	sourcefiles = init_sourcefiles ();
//	sourcefiles = cut_and_merge (sourcefiles, 0, lastframe, NULL, NULL, &sourcefiles_c);
	update_cutmarks_from_numbers ();
	prepare_file_attributes (sourcefiles);
	create_filelist(sourcefiles);

#ifdef DEBUG
	print_file_chain ("raw", sourcefiles);
#endif

	sig_t handler = handle_sigusr1;
	if (signal (SIGUSR1, handler) == SIG_ERR) {
		fprintf (logging, "Could not register signal handler, going on without!\n");
	}

#ifndef DEBUG
	logging = fopen ("/tmp/fuse-ts.log", "a");
#endif
	int ret = fuse_main (argc_new, argv_new, &ts_oper);
#ifdef DEBUG
	fclose (logging);
#endif
	return ret;
}
