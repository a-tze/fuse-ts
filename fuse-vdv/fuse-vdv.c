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
#include "config.h"
#include "fuse-vdv.h"
#include "fuse-vdv-tools.h"
#include "fuse-vdv-filelist.h"
#include "fuse-vdv-opts.h"
#include "fuse-vdv-debug.h"
#include "fuse-vdv-kdenlive.h"
#include "fuse-vdv-smoothsort.h"
#include "fuse-vdv-wav-demux.h"
#include "fuse-vdv-knowledge.h"
#include "fuse-vdv-shotcut.h"

// "instance" variables:
FILE *logging = NULL;

char *base_dir = NULL;
char *start_time = NULL;
char *prefix = NULL;
int inframe = 0;
int outframe = -1;
int lastframe = -1;
int blanklen = 0;
int slidemode = 0;
const char *rawName = "/uncut.dv";
const char *cutName = "/cut.dv";
const char *cutCompleteName = "/cut-complete.dv";

char *intro_file = NULL;
char *outro_file = NULL;
char *pid = NULL;
char *inframe_str = NULL;
size_t inframe_str_length = 0;
char *outframe_str = NULL;
size_t outframe_str_length = 0;

char *mountpoint = NULL;
time_t crtime = 0;

// used for storing glusters gfid:

char* gfid[14] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	NULL, NULL, NULL, NULL, NULL, NULL, NULL};
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

sourcefile_t *sourcefiles_cut = NULL;
int sourcefiles_cut_c = 0;
off_t cut_file_length = 0;

sourcefile_t *sourcefiles_cutcomp = NULL;
int sourcefiles_cutcomp_c = 0;
off_t cutcomp_file_length = 0;

static int cutmarks_tainted = 0;
static int pid_nr = 0;

static int vdv_getattr (const char *path, struct stat *stbuf) {
	debug_printf ("getattr called for '%s'\n", path);
	int res = 0;
	memset (stbuf, 0, sizeof (struct stat));
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) return -ENOENT;
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
	case INDEX_CUT:
		stbuf->st_size = cut_file_length;
		break;
	case INDEX_CUTCOMPLETE:
		stbuf->st_size = cutcomp_file_length;
		break;
	case INDEX_PID:
		stbuf->st_size = safe_strlen (pid);
		break;
	case INDEX_OPTS:
		stbuf->st_size = opts_length ();
		break;
	case INDEX_INFRAME:
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_size = inframe_str_length;
		break;
	case INDEX_OUTFRAME:
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_size = outframe_str_length;
		break;
	case INDEX_KDENLIVE:
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_size = get_kdenlive_project_file_size (rawName + 1, file_length / frame_size, blanklen);
		break;
	case INDEX_SHOTCUT:
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_size = get_shotcut_project_file_size (rawName + 1, file_length / frame_size, blanklen);
		break;
	case INDEX_REBUILD:
		stbuf->st_mode = S_IFREG | 0200;
		stbuf->st_size = 0;
		break;
#ifdef CONFIG_WITHWAVDEMUX
	case INDEX_WAV:
		stbuf->st_size = wav_filesize;
		break;
#endif
	default:
		return -ENOENT;
	}
	return res;
}

static int vdv_readdir (const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	(void) offset;
	(void) fi;

	debug_printf ("readdir called on '%s'\n", path);
	if (strcmp (path, "/") != 0)
		return -ENOENT;

	filler (buf, ".", NULL, 0);
	filler (buf, "..", NULL, 0);
	filler (buf, rawName + 1, NULL, 0);
	if ((inframe >= 0) && (outframe >= 0)) {
		filler (buf, cutName + 1, NULL, 0);
		filler (buf, cutCompleteName + 1, NULL, 0);
	}
	filler (buf, kdenlive_path + 1, NULL, 0);
	filler (buf, shotcut_path + 1, NULL, 0);
	filler (buf, opts_path + 1, NULL, 0);
	filler (buf, "pid", NULL, 0);
	filler (buf, "inframe", NULL, 0);
	filler (buf, "outframe", NULL, 0);
	filler (buf, "rebuild", NULL, 0);
#ifdef CONFIG_WITHWAVDEMUX
	filler (buf, wav_filepath + 1, NULL, 0);
#endif

	return 0;
}

static int vdv_open (const char *path, struct fuse_file_info *fi) {
	debug_printf ("open called on '%s'\n", path);
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) return -ENOENT;
	switch(entrynr) {
	case INDEX_RAW:
		if ((fi->flags & 3) != O_RDONLY)
			return -EACCES;
		check_signal ();
		pthread_mutex_lock (&globalmutex);
		fi->fh = insert_into_filechains_list (sourcefiles);
		insert_into_filehints_list (fi->fh, sourcefiles); //TODO check fi->fh
		pthread_mutex_unlock (&globalmutex);
		break;
	case INDEX_CUT:
		if ((fi->flags & 3) != O_RDONLY)
			return -EACCES;
		check_signal ();
		pthread_mutex_lock (&globalmutex);
		fi->fh = insert_into_filechains_list (sourcefiles_cut);
		insert_into_filehints_list (fi->fh, sourcefiles); //TODO check fi->fh
		pthread_mutex_unlock (&globalmutex);
		break;
	case INDEX_CUTCOMPLETE:
		if ((fi->flags & 3) != O_RDONLY)
			return -EACCES;
		check_signal ();
		pthread_mutex_lock (&globalmutex);
		fi->fh = insert_into_filechains_list (sourcefiles_cutcomp);
		insert_into_filehints_list (fi->fh, sourcefiles); //TODO check fi->fh
		pthread_mutex_unlock (&globalmutex);
		break;
#ifdef CONFIG_WITHWAVDEMUX
	case INDEX_WAV:
		if ((fi->flags & 3) != O_RDONLY)
			return -EACCES;
		pthread_mutex_lock (&globalmutex);
		fi->fh = insert_into_filechains_list (sourcefiles_cut);
		insert_into_filehints_list (fi->fh, sourcefiles); //TODO check fi->fh
		pthread_mutex_unlock (&globalmutex);
		break;
#endif
	case INDEX_REBUILD:
		if ((fi->flags & O_WRONLY) != O_WRONLY)
			return -EACCES;
		palimmpalimm = 1;
		check_signal ();
		return 0;
	case INDEX_KDENLIVE:
		if ((fi->flags & 3) != O_RDONLY)
			open_kdenlive_project_file (rawName, file_length / frame_size, blanklen, ((fi->flags & O_TRUNC) > 0));
		return 0;
	case INDEX_SHOTCUT:
		if ((fi->flags & 3) != O_RDONLY)
			open_shotcut_project_file (rawName, file_length / frame_size, blanklen, ((fi->flags & O_TRUNC) > 0));
		return 0;
	case INDEX_INFRAME:
	case INDEX_OUTFRAME:
	case INDEX_PID:
	case INDEX_OPTS:
	case INDEX_ROOTDIR:
		return 0;
	default:
		debug_printf ("Path not found: '%s' \n", path);
		return -ENOENT;
	}
	return 0;
}

static int vdv_truncate (const char *path, off_t size) {
	debug_printf ("truncate called on '%s' with size %" PRId64 "\n", path, size);
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) return -ENOENT;
	switch(entrynr) {
	case INDEX_SHOTCUT:
		truncate_shotcut_project_file();
		break;
	case INDEX_KDENLIVE:
		truncate_kdenlive_project_file();
		break;
	case INDEX_INFRAME:
		inframe_str_length = 0;
		break;
	case INDEX_OUTFRAME:
		outframe_str_length = 0;
		break;
	default:
		return -EACCES;
	}
	return 0;
}

int vdv_dv_do_read (sourcefile_t * file, char *buf, size_t size, off_t fileoffset) {
	if (file == NULL) {
		debug_printf ("Something went terribly wrong while reading: filepiece pointer NULL was given\n");
		return 0;
	}
	if (file->filename == NULL) {
		debug_printf ("Something went terribly wrong while reading: filepiece has NULL filename\n");
		return 0;
	}
//      debug_printf("trying to give out %d bytes at position %" PRId64 " of file '%s'\n", size, fileoffset, file->filename);
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
		size = fread (buf, 1, size, f);
		debug_printf ("successfully read %d bytes of file '%s'\n", size, file->filename);
		return size;
	} else {
		void * framebuf = malloc((size_t)frame_size);
		fseek (f, 0, SEEK_SET);
		size_t rsize = fread (framebuf, 1, frame_size, f);
		debug_printf ("successfully read %d bytes of frame-file '%s'\n", rsize, file->filename);
		if (rsize < frame_size) {
			debug_printf ("ERROR: file too short '%s' !\n", file->filename);
			fprintf (logging, "ERROR: file too short '%s' !\n", file->filename);
			free(framebuf);
			return 0;
		}
		size_t remaining = size;
		int realoffset = (int)(fileoffset % frame_size);
		while (remaining + realoffset >= frame_size) {
			memcpy(buf, framebuf + realoffset, frame_size - realoffset);
			remaining -= (frame_size - realoffset);
			buf += (frame_size - realoffset);
			realoffset = 0;
		}
		memcpy(buf, framebuf + realoffset, remaining);
		free(framebuf);
		return size;
	}
}

static int vdv_dv_read (uint64_t filehandle, sourcefile_t * list, char *buf, size_t size, off_t offset, int depth) {

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
#ifdef CONFIG_WITHSMALL
	if (filehints_size > filehandle && (filehints[filehandle])->lastpos <= offset) {
		t = (filehints[filehandle])->lastpiece;
	}
#endif
	t = get_sourcefile_for_position (t, offset);
	assert (t != NULL);

#ifdef CONFIG_WITHSMALL
	// performance: cache list position
	if (filehints_size > filehandle) {
		(filehints[filehandle])->lastpiece = t;
		(filehints[filehandle])->lastpos = offset;
	}
#endif
	off_t start = offset - t->globalpos;
	start += t->startpos;
	size_t readsize;
	if (slidemode == 0) {
		readsize = (size_t) ((off_t) t->endpos - start);
	} else {
		readsize = frame_size;
	}
	if (readsize >= size) {
		readsize = size;
		return vdv_dv_do_read (t, buf, readsize, start);
	} else {
		return vdv_dv_do_read (t, buf, readsize, start) + vdv_dv_read (filehandle, t, buf + readsize, size - readsize, offset + readsize, ++depth);
	}
}

static int vdv_read (const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	debug_printf ("someone wants to read %d bytes at position %" PRId64 " of '%s'\n", size, offset, path);
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) return -ENOENT;

	size_t ret = 0;
	char *t = NULL;
	switch(entrynr) {
	case INDEX_RAW:
	case INDEX_CUT:
	case INDEX_CUTCOMPLETE:
		if (filechains_size > fi->fh) {
			return vdv_dv_read (fi->fh, filechains[fi->fh], buf, size, offset, 0);
		} else {
			// There was no fopen before
			error_printf ("Error: unknown filehandle: %d \n", fi->fh);
			return 0;
		}
		break;
	case INDEX_PID:
		return string_read (pid, buf, size, offset);
	case INDEX_INFRAME:
		return string_read_with_length (inframe_str, buf, size, offset, inframe_str_length);
	case INDEX_OUTFRAME:
		return string_read_with_length (outframe_str, buf, size, offset, outframe_str_length);
	case INDEX_OPTS:
		t = get_opts ();
		ret = string_read (t, buf, size, offset);
		free (t);
		return ret;
	case INDEX_KDENLIVE:
		return kdenlive_read (path, buf, size, offset, rawName, file_length / frame_size, blanklen);
	case INDEX_SHOTCUT:
		return shotcut_read (path, buf, size, offset, rawName, file_length / frame_size, blanklen);
#ifdef CONFIG_WITHWAVDEMUX
	case INDEX_WAV:
		return vdv_wav_read (filechains[fi->fh], buf, size, offset, 0);
#endif
	}
	debug_printf ("Path not found: '%s' \n", path);
	return -ENOENT;
}

int vdv_write (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *info) {
	int entrynr = get_index_from_pathname(path);
	if (entrynr < 0) return -ENOENT;

	switch(entrynr) {
	case INDEX_KDENLIVE:
		return write_kdenlive_project_file (buf, size, offset);
	case INDEX_SHOTCUT:
		return write_shotcut_project_file (buf, size, offset);
	case INDEX_INFRAME:
		cutmarks_tainted = 1;
		return write_to_buffer (buf, size, offset, &inframe_str, &inframe_str_length);
	case INDEX_OUTFRAME:
		cutmarks_tainted = 1;
		return write_to_buffer (buf, size, offset, &outframe_str, &outframe_str_length);
	case INDEX_REBUILD:
		return size;
	}
	return -EACCES;
}

int vdv_release (const char *filename, struct fuse_file_info *info) {
	debug_printf ("release called on '%s'\n", filename);
	int entrynr = get_index_from_pathname(filename);
	if (entrynr < 0) return -ENOENT;

	switch(entrynr) {
	case INDEX_RAW:
		pthread_mutex_lock (&globalmutex);
		remove_from_filechains_list (info->fh);
		pthread_mutex_unlock (&globalmutex);
		break;
	case INDEX_KDENLIVE:
		if (find_cutmarks_in_kdenlive_project_file (&inframe, &outframe, &blanklen) == 0) {
			pthread_mutex_lock (&globalmutex);
			rebuild_opts ();
			update_cut_filechains ();
			pthread_mutex_unlock (&globalmutex);
		}
		close_kdenlive_project_file ();
		break;
	case INDEX_SHOTCUT:
		if (find_cutmarks_in_shotcut_project_file (&inframe, &outframe, &blanklen) == 0) {
			pthread_mutex_lock (&globalmutex);
			rebuild_opts ();
			update_cut_filechains ();
			pthread_mutex_unlock (&globalmutex);
		}
		close_shotcut_project_file ();
		break;
	case INDEX_INFRAME:
	case INDEX_OUTFRAME:
		if (cutmarks_tainted) {
			debug_printf ("cutmarks were changed, updating filechains of cut files\n");
			update_cutmarks ();
			update_cut_filechains ();
			rebuild_opts ();
			cutmarks_tainted = 0;
		}
		break;
	}
	return 0;
}

void *vdv_init (void) {
	pid_nr = getpid();
	pid = update_int_string (pid, pid_nr, NULL);
	return NULL;
}

int vdv_utime (const char *path, struct utimbuf *b) {
	debug_printf ("utimens called on '%s'\n", path);
	return 0;
}

int vdv_getxattr (const char *path, const char *name, char *value, size_t vlen ) {
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

int vdv_setxattr (const char * path, const char *name, const char *value, size_t vlen, int flags) {
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

static struct fuse_operations vdv_oper = {
	.init = vdv_init,
	.getattr = vdv_getattr,
	.getxattr = vdv_getxattr,
	.setxattr = vdv_setxattr,
	.readdir = vdv_readdir,
	.open = vdv_open,
	.read = vdv_read,
	.write = vdv_write,
	.release = vdv_release,
	.truncate = vdv_truncate,
	.utime = vdv_utime,
};


/////////////////////////////////////////////

void check_signal (void) {
	debug_printf ("checking if signal has been set\n");
	if (palimmpalimm > 0) {
		debug_printf ("signal has been set, rebuilding file chain\n");

		pthread_mutex_lock (&globalmutex);
		debug_printf ("no files opening right now\n");

		palimmpalimm = 0;
		sourcefile_t *newsources = init_sourcefiles ();
		int newsize = 0;
		newsources = cut_and_merge (newsources, 0, lastframe, NULL, NULL, &newsize);
		if (sourcefiles->refcnt == 0) {
			purge_list (sourcefiles);
		} else {
			sourcefiles->refcnt--;
		}
		sourcefiles_c = newsize;
		sourcefiles = newsources;
		prepare_file_attributes (sourcefiles);
		rebuild_opts ();
		pthread_mutex_unlock (&globalmutex);
	}
}

sourcefile_t *init_sourcefiles () {
	int found = 0;
	sourcefile_t *finallist = NULL;

	sourcefile_t *files = get_files_with_prefix (prefix, &found);
	if (found == 0) {
		error_printf ("Error: No files matching the prefix (%s) found!\n", prefix);
		exit (112);
	}
	files = smoothsort_list (files);
	found = list_count (files);
	if (found == 0) {
		error_printf ("Error: No files left in list after sorting!\n");
		exit (113);
	}
	sourcefile_t *iter = files;
	char *prefix_with_path = get_prefix_with_path ();
	int i = 0;
	for (; i < found; i++) {
		int comp = strcmp (prefix_with_path, iter->filename);
		if (comp <= 0) {
			finallist = add_file_to_list (finallist, iter, 0, -1);
			if (0 != slidemode) {
				reorganize_slide_list(finallist);
			}
			if (get_list_tail (finallist)->gframes >= lastframe) {
				break;
			}
		}
		iter = iter->next;
	}
	purge_list (files);
	iter = get_list_tail (finallist);
	if (iter == NULL) {
		error_printf ("Error: No files matching the specified prefix (%s) and the start-time (%s) found!\n"
			"\tIs the prefix complete, including any separation characters?\n"
			"\tIs the date/time-format exactly like that used in the filenames?\n", prefix, start_time);
		exit(114);
	} else {
		debug_printf ("Found %d input files with a total of %d frames.\n", list_count (finallist), iter->gframes);
	}
	return finallist;
}


sourcefile_t *cut_and_merge (sourcefile_t * list, int inframe, int outframe, char *intro_file, char *outro_file, int *piece_count) {
	if ((outframe > 0) && (outframe > inframe)) {
		list = cut_out (list, outframe);
	}
	if (inframe > 0) {
		list = cut_in (list, inframe);
	}
	if (outro_file != NULL) {
		if (file_exists (outro_file) > 0) {
			list = add_file_to_list (list, new_file_entry_absolute_path (outro_file), 0, -1);
		} else {
			fprintf (logging, "Warning: Outro File '%s' does not exist!\n", outro_file);
		}
	}
	if (intro_file != NULL) {
		if (file_exists (intro_file) > 0) {
			list = add_file_to_list_head (list, new_file_entry_absolute_path (intro_file), 0, -1);
		} else {
			fprintf (logging, "Warning: Intro File '%s' does not exist!\n", intro_file);
		}
	}
	reorganize_list (list);
	*piece_count = list_count (list);
	debug_printf("Now having a list with %d pieces\n", *piece_count);
	crtime = time(NULL);
	return list;
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
	file_length = (off_t) last->globalpos + last->endpos - last->startpos;
	list->totalsize = file_length;
	init_kdenlive_project_file (rawName + 1);
	init_shotcut_project_file (rawName + 1);
}

void update_cutmarks () {
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
}


void update_cut_filechains () {
	sourcefiles_cut = dupe_file_list (sourcefiles);
	if (sourcefiles_cut == NULL) {
		error_printf ("Empty cutlist!\n");
		return;
	}
	sourcefiles_cut = cut_and_merge (sourcefiles_cut, inframe, outframe, NULL, NULL, &sourcefiles_cut_c);
	sourcefiles_cut->refcnt = 1;
	sourcefile_t *last = get_list_tail (sourcefiles_cut);
	cut_file_length = (off_t) last->globalpos + last->endpos - last->startpos;
	sourcefiles_cut->totalsize = cut_file_length;
#ifdef CONFIG_WITHWAVDEMUX
	update_wav_filesize (cut_file_length);
#endif
	// complete version:
	sourcefiles_cutcomp = dupe_file_list (sourcefiles);
	sourcefiles_cutcomp = cut_and_merge (sourcefiles_cutcomp, inframe, outframe, intro_file, outro_file, &sourcefiles_cutcomp_c);
	sourcefiles_cutcomp->refcnt = 1;
	last = get_list_tail (sourcefiles_cutcomp);
	cutcomp_file_length = (off_t) last->globalpos + last->endpos - last->startpos;
	sourcefiles_cutcomp->totalsize = cutcomp_file_length;
	init_kdenlive_project_file ();
	init_shotcut_project_file ();
	inframe_str = update_int_string (inframe_str, inframe, &inframe_str_length);
	outframe_str = update_int_string (outframe_str, outframe, &outframe_str_length);
	print_file_chain ("cut", sourcefiles_cut);
	print_file_chain ("cutComplete", sourcefiles_cutcomp);
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
	sourcefiles = init_sourcefiles ();
	sourcefiles = cut_and_merge (sourcefiles, 0, lastframe, NULL, NULL, &sourcefiles_c);
	prepare_file_attributes (sourcefiles);

	if ((inframe >= 0) && (outframe >= 0) && (outframe >= inframe)) {
		update_cut_filechains ();
	}
#ifdef DEBUG
	print_file_chain ("raw", sourcefiles);
	print_file_chain ("cut", sourcefiles_cut);
	print_file_chain ("cutComplete", sourcefiles_cutcomp);
#endif

	sig_t handler = handle_sigusr1;
	if (signal (SIGUSR1, handler) == SIG_ERR) {
		fprintf (logging, "Could not register signal handler, going on without!\n");
	}

#ifndef DEBUG
	logging = fopen ("/tmp/fuse-vdv.log", "a");
#endif
	int ret = fuse_main (argc_new, argv_new, &vdv_oper);
#ifndef DEBUG
	fclose (logging);
#endif
	return ret;
}
