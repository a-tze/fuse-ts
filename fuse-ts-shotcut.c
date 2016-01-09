#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/types.h>
#include <assert.h>
#include <mxml.h>
#include <pthread.h>
#include "fuse-ts.h"
#include "fuse-ts-tools.h"
#include "fuse-ts-debug.h"
#include "fuse-ts-filebuffer.h"
#include "fuse-ts-shotcut.h"

const char *shotcut_path = "/project_shotcut.mlt";
const char *shotcut_path_win = "/project_shotcut_win.mlt";
static const char *sc_template;

static filebuffer_t* sc_project_file_cache = NULL;
static int sc_project_file_cache_frames = -1;
static char * sc_project_file_cache_filename = NULL;
static int sc_project_file_refcount = 0;
static pthread_mutex_t sc_cachemutex = PTHREAD_MUTEX_INITIALIZER;

static filebuffer_t *sc_writebuffer = NULL;
static const char* slash = "/";

filebuffer_t* get_shotcut_project_file_cache (const char *filename, int num_frames, int blanklen, const char* projectfile) {
	pthread_mutex_lock (&sc_cachemutex);
	char *t = merge_strs (3, mountpoint, slash, filename);
	if (0 == strcmp(projectfile, shotcut_path_win)) {
		const char * t2 = strchr(mountpoint, '/');
		int i = winpath_stripslashes;
		while (i > 1 && strlen(t2) > 1) {
			i--;
			t2 = strchr(t2 + 1, '/');
		}
		if (t2 == NULL) t2 = slash;
		free (t);
		t = merge_strs (4, winpath, t2, slash, filename);
	}
	debug_printf ("%s: filename: '%s' path: '%s' frames: %d\n", __FUNCTION__, filename, t, num_frames);

	if ((sc_project_file_cache != NULL) && (sc_project_file_cache_frames == num_frames) && (0 == strcmp(sc_project_file_cache_filename, t))) {
		debug_printf ("get_shotcut_project_file: filename: '%s' frames: %d --> cache hit: (%p)\n", filename, num_frames, sc_project_file_cache);
		pthread_mutex_unlock (&sc_cachemutex);
		free(t);
		return sc_project_file_cache;
	}

	int _outframe = (outframe < 0) ? totalframes : outframe;
	const size_t size = strlen(sc_template) * 2;
	if (sc_project_file_cache == NULL) sc_project_file_cache = filebuffer__new();
	char* temp = (char *) malloc (size);
	CHECK_OOM(temp);
	int len = snprintf (temp, size - 1, sc_template, inframe, num_frames, num_frames - 1, outbyte, t, _outframe, blanklen);
	if (len >= size) err(124, "%s: size fail when generating project file\n", __FUNCTION__);
	debug_printf ("get_shotcut_project_file: result has a size of: %d\n", len);
	filebuffer__write(sc_project_file_cache, temp, len, 0);
	filebuffer__truncate(sc_project_file_cache, len);
	sc_project_file_cache_frames = num_frames;
	if (sc_project_file_cache_filename != NULL) free(sc_project_file_cache_filename);
	sc_project_file_cache_filename = t;
	free (temp);

	pthread_mutex_unlock (&sc_cachemutex);
	return sc_project_file_cache;
}

size_t get_shotcut_project_file_size (const char *filename, int num_frames, int blanklen, const char* projectfile) {
	filebuffer_t* fb = get_shotcut_project_file_cache (filename, num_frames, blanklen, projectfile);
	return filebuffer__contentsize(fb);
}

void init_shotcut_project_file () {
	pthread_mutex_lock (&sc_cachemutex);
	if (sc_project_file_cache != NULL) {
		debug_printf ("init_shotcut_project_file: freeing cache pointer %p\n", sc_project_file_cache);
		filebuffer__destroy (sc_project_file_cache);
		sc_project_file_cache = NULL;
	}
	pthread_mutex_unlock (&sc_cachemutex);
}

size_t shotcut_read (const char *path, char *buf, size_t size, off_t offset, const char *movie_path, int frames, int blanklen, const char* projectfile) {
	debug_printf ("reading from shotcut project file at %" PRId64 " with a length of %d \n", offset, size);
	filebuffer_t* fb = get_shotcut_project_file_cache (movie_path + 1, frames, blanklen, projectfile);
	if (fb == NULL) return -EIO;
	return filebuffer__read (fb, offset, buf, size);
}

void open_shotcut_project_file (const char *movie_path, int frames, int blanklen, int truncate, const char* projectfile) {
	debug_printf ("opening shotcut project file, truncate:%d\n", truncate);
	sc_project_file_refcount++;
	if (sc_project_file_refcount > 1) return;
	if (sc_writebuffer == NULL) {
		debug_printf ("creating new writebuffer from project file\n");
		if (truncate) {
			sc_writebuffer = filebuffer__new();
		} else {
			sc_writebuffer = filebuffer__copy(get_shotcut_project_file_cache (movie_path, frames, blanklen, projectfile));
		}
	} else if (truncate) {
		filebuffer__truncate(sc_writebuffer, 0);
	}
}

void truncate_shotcut_project_file() {
	if (sc_writebuffer != NULL) filebuffer__truncate(sc_writebuffer, 0);
}

size_t write_shotcut_project_file (const char *buffer, size_t size, off_t offset) {
	debug_printf ("writing to shotcut project file at %" PRId64 " with a length of %d \n", offset, size);
	if (sc_writebuffer == NULL) {
		debug_printf ("writing to shotcut project FAILED: not opened before!\n");
		return -EACCES;
	}
	return filebuffer__write(sc_writebuffer, buffer, size, offset);
}

void close_shotcut_project_file () {
	debug_printf ("closing shotcut project file.\n");
	sc_project_file_refcount--;
	if (sc_project_file_refcount > 0) return;
	// TODO save projectfile and hand it out as a different file
	if (sc_writebuffer != NULL) {
		filebuffer__destroy(sc_writebuffer);
		sc_writebuffer = NULL;
	}
}

int find_cutmarks_in_shotcut_project_file (int *inframe, int *outframe, int *blanklen) {
	if (sc_writebuffer == NULL) {
		debug_printf ("find_cutmarks: file has not been written to.\n");
		return 100;
	}
/*
  in XPATH, I would look for 
    producer[@id='producer0']/@in
  and
    producer[@id='producer0']/@out
*/
	mxml_node_t *xmldoc;
	char* temp = filebuffer__read_all_to_cstring(sc_writebuffer);
	xmldoc = mxmlLoadString (NULL, temp, MXML_TEXT_CALLBACK);
	free(temp);
	if (NULL == xmldoc) {
		debug_printf ("find_cutmarks: no valid XML!\n");
		return 1;
	}
	mxml_node_t *node;
	node = mxmlFindElement (xmldoc, xmldoc, "producer", "id", "producer0", MXML_DESCEND);
	if (NULL == node) {
		debug_printf ("find_cutmarks: node with id 'producer0' not found!\n");
		mxmlRelease (xmldoc);
		return 2;
	}

	int blank = 0;
	const char *strin = mxmlElementGetAttr (node, "in");
	const char *strout = mxmlElementGetAttr (node, "out");

	if (NULL == strin) {
		debug_printf ("find_cutmarks: no valid inpoint found!\n");
		mxmlRelease (xmldoc);
		return 4;
	}
	if (NULL == strout) {
		debug_printf ("find_cutmarks: no valid outpoint found!\n");
		mxmlRelease (xmldoc);
		return 5;
	}

	debug_printf ("find_cutmarks: found attributes in='%s' out='%s'\n", strin, strout);

	int in, out = 0;
	if (index(strin, ':') != NULL) {
		static int h,m,s,ms = 0;
		if ((4 != sscanf(strin,"%d:%d:%d,%d",&h,&m,&s,&ms)) && (4 != sscanf(strin,"%d:%d:%d.%d",&h,&m,&s,&ms))) {
			debug_printf ("find_cutmarks: no valid inpoint found!\n");
			mxmlRelease (xmldoc);
			return 4;
		}
		in = (h*60*60*frames_per_second)+(m*60*frames_per_second)+(s*frames_per_second)+(ms/(1000/frames_per_second));
	} else {
		in = atoi (strin);
	}

	if (index(strout, ':') != NULL) {
		static int h,m,s,ms = 0;
		if ((4 != sscanf(strout,"%d:%d:%d,%d",&h,&m,&s,&ms)) && (4 != sscanf(strout,"%d:%d:%d.%d",&h,&m,&s,&ms))) {
			debug_printf ("find_cutmarks: no valid outpoint found!\n");
			mxmlRelease (xmldoc);
			return 5;
		}
		out = (h*60*60*frames_per_second)+(m*60*frames_per_second)+(s*frames_per_second)+(ms/(1000/frames_per_second));

	} else {
		out = atoi (strout);
	}
	
	mxmlRelease (xmldoc);
	if (0 > in) {
		debug_printf ("find_cutmarks: inpoint invalid!\n");
		return 6;
	}
	if (0 >= out) {
		debug_printf ("find_cutmarks: outpoint invalid!\n");
		return 7;
	}
	debug_printf ("find_cutmarks: blank is '%d'\n", blank);
	debug_printf ("find_cutmarks: in is '%d'\n", in);
	debug_printf ("find_cutmarks: out is '%d'\n", out);
	*blanklen = blank;
	*inframe = in;
	*outframe = out;
	return 0;
}


//   %1$d => inframe,  %2$d => frames, %3$d => frames - 1 
//   %4$(PRI64d) => filesize, %5$s => filename with path
//   %6$d => outframe

static const char *sc_template =
"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
"<mlt LC_NUMERIC=\"C\" version=\"0.9.9\" title=\"fuse-ts\" parent=\"producer0\" in=\"%1$d\" out=\"%6$d\">\n"
"  <profile description=\"automatic\" />\n"
"  <producer id=\"producer0\" title=\"fuse-ts\" in=\"%1$d\" out=\"%6$d\">\n"
"    <property name=\"mlt_type\">mlt_producer</property>\n"
"    <property name=\"length\">%2$d</property>\n"
"    <property name=\"eof\">pause</property>\n"
"    <property name=\"resource\">uncut.ts</property>\n"
"    <property name=\"seekable\">1</property>\n"
"    <property name=\"aspect_ratio\">1</property>\n"
"    <property name=\"audio_index\">1</property>\n"
"    <property name=\"video_index\">0</property>\n"
"    <property name=\"mute_on_pause\">0</property>\n"
"    <property name=\"mlt_service\">avformat-novalidate</property>\n"
"    <property name=\"ignore_points\">0</property>\n"
"    <property name=\"global_feed\">1</property>\n"
"  </producer>\n"
"  <!--\n"
"  %1$d => inframe,  %2$d => frames, %3$d => frames - 1\n"
"  %4$" PRId64 " => filesize, %5$s => filename with path\n"
"  %6$d => outframe %7$d => blanklen\n"
"  -->\n"
"</mlt>\n";

