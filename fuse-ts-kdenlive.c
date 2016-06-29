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
#include "fuse-ts-kdenlive.h"

char *kdenlive_path = "/project.kdenlive";
static const char *kl_template;

static filebuffer_t* kl_project_file_cache = NULL;
static int kl_project_file_cache_inframe = -1;
static int kl_project_file_cache_outframe = -1;
static int kl_project_file_cache_blanklen = -1;
static int kl_project_file_refcount = 0;
static pthread_mutex_t kl_cachemutex = PTHREAD_MUTEX_INITIALIZER;

static filebuffer_t* kl_writebuffer = NULL;

filebuffer_t* get_kdenlive_project_file_cache (const char *filename, int num_frames, int blanklen) {
	pthread_mutex_lock (&kl_cachemutex);
	if ((kl_project_file_cache != NULL) && (kl_project_file_cache_inframe == inframe) && (kl_project_file_cache_outframe == outframe) && (kl_project_file_cache_blanklen == blanklen)) {
		debug_printf ("%s: cache hit (%p)\n", __FUNCTION__, kl_project_file_cache);
		pthread_mutex_unlock (&kl_cachemutex);
		return kl_project_file_cache;
	}
	int _outframe = (outframe < 0) ? totalframes : outframe;
	int _inframe = inframe;
	char *t = merge_strs (3, mountpoint, "/", filename);

	const size_t size = strlen(kl_template) * 2;
	if (kl_project_file_cache == NULL) kl_project_file_cache = filebuffer__new();
	char* temp = (char *) malloc (size);
	CHECK_OOM(temp);
	int len = snprintf (temp, size - 1, kl_template, _inframe, num_frames, num_frames - 1, outbyte, t, _outframe, blanklen);
	if (len >= size) err(124, "%s: size fail when generating project file\n", __FUNCTION__);
	debug_printf ("%s: result has a size of: %d\n", __FUNCTION__, len);
	filebuffer__write(kl_project_file_cache, temp, len, 0);
	filebuffer__truncate(kl_project_file_cache, len);
	kl_project_file_cache_inframe = inframe;
	kl_project_file_cache_outframe = outframe;
	kl_project_file_cache_blanklen = blanklen;
	free (temp);
	free (t);
	pthread_mutex_unlock (&kl_cachemutex);
	return kl_project_file_cache;
}

size_t get_kdenlive_project_file_size (const char *filename, int num_frames, int blanklen) {
	filebuffer_t* fb = get_kdenlive_project_file_cache (filename, num_frames, blanklen);
	if (fb == NULL) return -EIO;
	debug_printf ("%s: result is: %d\n", __FUNCTION__, filebuffer__contentsize(fb));
	return filebuffer__contentsize(fb);
}

void init_kdenlive_project_file () {
	pthread_mutex_lock (&kl_cachemutex);
	if (kl_project_file_cache != NULL) {
		debug_printf ("%s: freeing cache %p\n", __FUNCTION__, kl_project_file_cache);
		filebuffer__destroy (kl_project_file_cache);
		kl_project_file_cache = NULL;
	}
	pthread_mutex_unlock (&kl_cachemutex);
}

size_t kdenlive_read (const char *path, char *buf, size_t size, off_t offset, const char *movie_path, int frames, int blanklen) {
	debug_printf ("reading from kdenlive project file at %" PRId64 " with a length of %d \n", offset, size);
	filebuffer_t* fb = get_kdenlive_project_file_cache (movie_path + 1, frames, blanklen);
	if (fb == NULL) return -EIO;
	return filebuffer__read(fb, offset, buf, size);
}

void open_kdenlive_project_file (const char *movie_path, int frames, int blanklen, int truncate) {
	debug_printf ("%s\n", __FUNCTION__);
	kl_project_file_refcount++;
	if (kl_project_file_refcount > 1) return;
	if (kl_writebuffer == NULL) {
		debug_printf("creating new writebuffer from project file\n");
		if (truncate) {
			kl_writebuffer = filebuffer__new();
		} else {
			kl_writebuffer = filebuffer__copy(get_kdenlive_project_file_cache(movie_path, frames, blanklen));
		}
	} else if (truncate) {
		filebuffer__truncate(kl_writebuffer, 0);
	}
}

int truncate_kdenlive_project_file(size_t size) {
	if (kl_writebuffer != NULL) {
		size_t l = filebuffer__truncate(kl_writebuffer, size);
		if (l < 0 || l < size) {
			return -EIO;
		}
	} else {
		kl_writebuffer = filebuffer__new();
	}
	return 0;
}

size_t write_kdenlive_project_file (const char *buffer, size_t size, off_t offset) {
	debug_printf ("writing to kdenlive project file at %" PRId64 " with a length of %d \n", offset, size);
	if (kl_writebuffer == NULL) {
		debug_printf ("writing to kdenlive project FAILED: not opened before!\n");
		return -EACCES;
	}
	return filebuffer__write(kl_writebuffer, buffer, size, offset);
}

void close_kdenlive_project_file () {
	debug_printf ("closing kdenlive project file.\n");
	kl_project_file_refcount--;
	if (kl_project_file_refcount > 0) return;
	if (kl_writebuffer != NULL) {
		filebuffer__destroy (kl_writebuffer);
		kl_writebuffer = NULL;
	}
}

int find_cutmarks_in_kdenlive_project_file (int *inframe, int *outframe, int *blanklen) {
	if (kl_writebuffer == NULL) {
		debug_printf ("find_cutmarks: file has not been written to.\n");
		return 100;
	}
/*
  in XPATH, I would look for 
    playlist[@id='playlist1']/entry[@producer='1']/@in
  and
    playlist[@id='playlist1']/entry[@producer='1']/@out
*/
	mxml_node_t *xmldoc;
	char* temp = filebuffer__read_all_to_cstring(kl_writebuffer);
	xmldoc = mxmlLoadString (NULL, temp, MXML_TEXT_CALLBACK);
	free(temp);
	if (NULL == xmldoc) {
		debug_printf ("find_cutmarks: no valid XML!\n");
		return 1;
	}
	mxml_node_t *node, *subnode;
	node = mxmlFindElement (xmldoc, xmldoc, "playlist", "id", "playlist1", MXML_DESCEND);
	if (NULL == node) {
		debug_printf ("find_cutmarks: node with id 'playlist1' not found!\n");
		mxmlRelease (xmldoc);
		return 2;
	}

	int blank = 0;
	subnode = mxmlFindElement (node, node, "blank", NULL, NULL, MXML_DESCEND);
	if (NULL == subnode) {
		debug_printf ("find_cutmarks: node 'blank' not found - assuming 0!\n");
	} else {
		const char *strblank = mxmlElementGetAttr (subnode, "length");
		if (NULL != strblank) {
			blank = atoi (strblank);
			if (blank < 0) {
				debug_printf ("find_cutmarks: node 'blank' contains negative value - assuming 0!\n");
				blank = 0;
			}
			if (blank > 45000) { //mehr als 30 min. Puffer sollte nicht noetig sein
				debug_printf ("find_cutmarks: node 'blank' contains high number - clipping to 45000!\n");
				blank = 45000;
			}
		}
	}

	node = mxmlFindElement (node, node, "entry", "producer", "1", MXML_DESCEND);
	if (NULL == node) {
		debug_printf ("find_cutmarks: node 'entry' in playlist not found!\n");
		mxmlRelease (xmldoc);
		return 3;
	}
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
	int in = atoi (strin);
	int out = atoi (strout);
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
//   %6$d => outframe, %7$d => blank before track

static const char *kl_template =
"<?xml version='1.0' encoding='utf-8'?>"
"<mlt title=\"Anonymous Submission\" root=\"/tmp\" version=\"0.8.8\">"
" <!-- %1$d => inframe,  %2$d => frames, %3$d => frames - 1  "
"  %4$" PRId64 " => filesize, %5$s => filename with path "
"  %6$d => outframe, %7$d => blanktime --> "
// Omitting aspect ratio crashes KDEnlive, so living with hardcoded values.
// Cutting other formats should be possible anyway, KDEnlive will just scale
// it wrong if it is not 16:9.
" <profile width=\"1920\" frame_rate_den=\"1\" height=\"1080\" "
" display_aspect_num=\"16\" display_aspect_den=\"9\" frame_rate_num=\"25\" "
" colorspace=\"709\" sample_aspect_den=\"1\" description=\"HD 1080i 25 fps\" "
" progressive=\"0\" sample_aspect_num=\"1\"/> "
" <producer in=\"0\" out=\"%3$d\" id=\"black\">"
"  <property name=\"mlt_type\">producer</property>"
"  <property name=\"aspect_ratio\">0</property>"
"  <property name=\"length\">%2$d</property>"
"  <property name=\"eof\">pause</property>"
"  <property name=\"resource\">black</property>"
"  <property name=\"mlt_service\">colour</property>"
" </producer>"
" <playlist id=\"black_track\">"
"  <entry in=\"0\" out=\"%3$d\" producer=\"black\"/>"
" </playlist>"
" <producer in=\"0\" out=\"%3$d\" id=\"1\">"
"  <property name=\"mlt_type\">producer</property>"
"  <property name=\"length\">%2$d</property>"
"  <property name=\"eof\">pause</property>"
"  <property name=\"resource\">%5$s</property>"
"  <property name=\"mlt_service\">avformat</property>"
"  <property name=\"source_fps\">25.000000</property>"
" </producer>"
" <playlist id=\"playlist1\">"
"  <blank length=\"%7$d\"/>"
"  <entry in=\"%1$d\" out=\"%6$d\" producer=\"1\"/>"
" </playlist>"
" <tractor title=\"Anonymous Submission\" global_feed=\"1\" in=\"0\" out=\"%3$d\" id=\"maintractor\">"
"  <track producer=\"black_track\"/>"
"  <track producer=\"playlist1\"/>"
" </tractor>"
" <kdenlivedoc kdenliveversion=\"0.9.10\" version=\"0.88\" projectfolder=\"/tmp/kdenlive\">"
"  <documentproperties zonein=\"0\" zoneout=\"100\" zoom=\"10\" verticalzoom=\"1\" position=\"0\"/>"
"  <tracksinfo>"
"   <trackinfo blind=\"0\" mute=\"0\" locked=\"0\" trackname=\"Cut me\"/>"
"  </tracksinfo>"
"  <kdenlive_producer id=\"1\" default_video=\"0\" fps=\"25.000000\" name=\"uncut.ts\" resource=\"%5$s\" duration=\"%2$d\" type=\"3\" />"
"  <markers/>"
"  <groups/>"
" </kdenlivedoc>"
"</mlt>"
;



