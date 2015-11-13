#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/types.h>
#include <assert.h>
#include <mxml.h>
#include <pthread.h>
#include "fuse-vdv.h"
#include "fuse-vdv-tools.h"
#include "fuse-vdv-debug.h"
#include "fuse-vdv-filebuffer.h"
#include "fuse-vdv-kdenlive.h"

char *kdenlive_path = "/project.kdenlive";
static const char *kl_template;

static filebuffer_t* kl_project_file_cache = NULL;
static int kl_project_file_cache_frames = -1;
static int kl_project_file_refcount = 0;
static pthread_mutex_t kl_cachemutex = PTHREAD_MUTEX_INITIALIZER;

static filebuffer_t* kl_writebuffer = NULL;


filebuffer_t* get_kdenlive_project_file_cache (const char *filename, int num_frames, int blanklen) {
	pthread_mutex_lock (&kl_cachemutex);
	if ((kl_project_file_cache != NULL) && (kl_project_file_cache_frames == num_frames)) {
		debug_printf ("%s: cache hit (%p)\n", __FUNCTION__, kl_project_file_cache);
		pthread_mutex_unlock (&kl_cachemutex);
		return kl_project_file_cache;
	}
	int _outframe = (outframe < 0) ? lastframe : outframe;
	char *t = merge_strs (3, mountpoint, "/", filename);

	const size_t size = strlen(kl_template) * 2;
	if (kl_project_file_cache == NULL) kl_project_file_cache = filebuffer__new();
	char* temp = (char *) malloc (size);
	CHECK_OOM(temp);
	int len = snprintf (temp, size - 1, kl_template, inframe, num_frames, num_frames - 1, (off_t) (num_frames * frame_size), t, _outframe, blanklen);
	if (len >= size) err(124, "%s: size fail when generating project file\n", __FUNCTION__);
	debug_printf ("%s: result has a size of: %d\n", __FUNCTION__, len);
	filebuffer__write(kl_project_file_cache, temp, len, 0);
	filebuffer__truncate(kl_project_file_cache, len);

	kl_project_file_cache_frames = num_frames;
	free (temp);
	free (t);
	pthread_mutex_unlock (&kl_cachemutex);
	return kl_project_file_cache;
}

size_t get_kdenlive_project_file_size (const char *filename, int num_frames, int blanklen) {
	filebuffer_t* fb = get_kdenlive_project_file_cache (filename, num_frames, blanklen);
	if (fb == NULL) return -EIO;
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

void truncate_kdenlive_project_file() {
	if (kl_writebuffer != NULL) filebuffer__truncate(kl_writebuffer, 0);
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
    playlist[@id='playlist2']/entry/@in
  and
    playlist[@id='playlist2']/entry/@out
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
	node = mxmlFindElement (xmldoc, xmldoc, "playlist", "id", "playlist2", MXML_DESCEND);
	if (NULL == node) {
		debug_printf ("find_cutmarks: node with id 'playlist2' not found!\n");
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
	"<?xml version='1.0' encoding='utf-8'?>\n"
	"<mlt title=\"Anonymous Submission\" version=\"0.8.0\" root=\"/tmp\" LC_NUMERIC=\"C\">\n"
	" <profile width=\"720\" display_aspect_den=\"3\" colorspace=\"601\" frame_rate_den=\"1\""
	"  description=\"DV/DVD PAL\" height=\"576\" display_aspect_num=\"4\" frame_rate_num=\"25\" "
	"  progressive=\"0\" sample_aspect_num=\"16\" sample_aspect_den=\"15\"/>\n"
	" <producer in=\"0\" out=\"500\" id=\"black\">\n"
	"  <property name=\"mlt_type\">producer</property>\n"
	"  <property name=\"length\">15000</property>\n"
	"  <property name=\"eof\">pause</property>\n"
	"  <property name=\"resource\">/tmp/black</property>\n"
	"  <property name=\"aspect_ratio\">0</property>\n"
	"  <property name=\"mlt_service\">colour</property>\n"
	" </producer>\n"
	" <playlist id=\"black_track\">\n"
	"  <entry in=\"0\" out=\"7279\" producer=\"black\"/>\n"
	" </playlist>\n"
	" <playlist id=\"playlist1\"/>\n"
	" <producer in=\"0\" out=\"%3$d\" id=\"1\">\n"
	"  <property name=\"mlt_type\">producer</property>\n"
	"  <property name=\"length\">%2$d</property>\n"
	"  <property name=\"eof\">pause</property>\n"
	"  <property name=\"resource\">%5$s</property>\n"
	"  <property name=\"meta.media.nb_streams\">2</property>\n"
	"  <property name=\"meta.media.0.stream.type\">video</property>\n"
	"  <property name=\"meta.media.0.stream.frame_rate\">25.000000</property>\n"
	"  <property name=\"meta.media.0.stream.sample_aspect_ratio\">1.422222</property>\n"
	"  <property name=\"meta.media.0.codec.width\">720</property>\n"
	"  <property name=\"meta.media.0.codec.height\">576</property>\n"
	"  <property name=\"meta.media.0.codec.frame_rate\">25.000000</property>\n"
	"  <property name=\"meta.media.0.codec.pix_fmt\">yuv420p</property>\n"
	"  <property name=\"meta.media.0.codec.sample_aspect_ratio\">1.422222</property>\n"
	"  <property name=\"meta.media.0.codec.colorspace\">601</property>\n"
	"  <property name=\"meta.media.0.codec.name\">dvvideo</property>\n"
	"  <property name=\"meta.media.0.codec.long_name\">DV (Digital Video)</property>\n"
	"  <property name=\"meta.media.0.codec.bit_rate\">28800000</property>\n"
	"  <property name=\"meta.media.1.stream.type\">audio</property>\n"
	"  <property name=\"meta.media.1.codec.sample_fmt\">s16</property>\n"
	"  <property name=\"meta.media.1.codec.sample_rate\">48000</property>\n"
	"  <property name=\"meta.media.1.codec.channels\">2</property>\n"
	"  <property name=\"meta.media.1.codec.name\">pcm_s16le</property>\n"
	"  <property name=\"meta.media.1.codec.long_name\">PCM signed 16-bit little-endian</property>\n"
	"  <property name=\"meta.media.1.codec.bit_rate\">1536000</property>\n"
	"  <property name=\"meta.media.1.codec.profile\">-99</property>\n"
	"  <property name=\"meta.media.1.codec.level\">-99</property>\n"
	"  <property name=\"seekable\">1</property>\n"
	"  <property name=\"meta.media.sample_aspect_num\">64</property>\n"
	"  <property name=\"meta.media.sample_aspect_den\">45</property>\n"
	"  <property name=\"aspect_ratio\">1.422222</property>\n"
	"  <property name=\"audio_index\">1</property>\n"
	"  <property name=\"video_index\">0</property>\n"
	"  <property name=\"mlt_service\">avformat</property>\n"
	"  <property name=\"meta.media.0.codec.profile\">-99</property>\n"
	"  <property name=\"meta.media.0.codec.level\">-99</property>\n"
	"  <property name=\"meta.attr.title.markup\"/>\n"
	"  <property name=\"meta.attr.author.markup\"/>\n"
	"  <property name=\"meta.attr.copyright.markup\"/>\n"
	"  <property name=\"meta.attr.comment.markup\"/>\n"
	"  <property name=\"meta.attr.album.markup\"/>\n"
	"  <property name=\"av_bypass\">0</property>\n"
	"  <property name=\"source_fps\">25.000000</property>\n"
	"  <property name=\"top_field_first\">0</property>\n"
	"  <property name=\"meta.media.frame_rate_num\">25</property>\n"
	"  <property name=\"meta.media.frame_rate_den\">1</property>\n"
	"  <property name=\"meta.media.colorspace\">601</property>\n"
	"  <property name=\"meta.media.width\">720</property>\n"
	"  <property name=\"meta.media.height\">576</property>\n"
	"  <property name=\"meta.media.top_field_first\">0</property>\n"
	"  <property name=\"meta.media.progressive\">0</property>\n"
	" </producer>\n"
	" <playlist id=\"playlist2\">\n"
	"  <blank length=\"%7$d\"/>\n"
	"  <entry in=\"%1$d\" out=\"%6$d\" producer=\"1\"/>\n"
	" </playlist>\n"
	" <tractor title=\"Anonymous Submission\" global_feed=\"1\" in=\"0\" out=\"%3$d\" id=\"maintractor\">\n"
	"  <property name=\"meta.volume\">1</property>\n"
	"  <track producer=\"black_track\"/>\n"
	"  <track hide=\"video\" producer=\"playlist1\"/>\n"
	"  <track producer=\"playlist2\"/>\n"
	"  <transition in=\"0\" out=\"0\" id=\"transition0\">\n"
	"   <property name=\"a_track\">1</property>\n"
	"   <property name=\"b_track\">2</property>\n"
	"   <property name=\"mlt_type\">transition</property>\n"
	"   <property name=\"mlt_service\">mix</property>\n"
	"   <property name=\"always_active\">1</property>\n"
	"   <property name=\"combine\">1</property>\n"
	"   <property name=\"internal_added\">237</property>\n"
	"  </transition>\n"
	" </tractor>\n"
	" <kdenlivedoc profile=\"dv_pal\" kdenliveversion=\"0.9.2\" version=\"0.88\" projectfolder=\"/tmp/kdenlive\">\n"
	"  <customeffects/>\n"
	"  <documentproperties enableproxy=\"0\" generateproxy=\"0\" zonein=\"0\" zoneout=\"100\" zoom=\"11\""
	"   verticalzoom=\"1\" position=\"2075\"/>\n"
	"  <documentmetadata/><documentnotes/>\n"
	"  <profileinfo width=\"720\" display_aspect_den=\"3\" frame_rate_den=\"1\" description=\"DV/DVD PAL\""
	"   height=\"576\" frame_rate_num=\"25\" display_aspect_num=\"4\" progressive=\"0\" sample_aspect_num=\"16\""
	"   sample_aspect_den=\"15\"/>\n"
	"  <tracksinfo>\n"
	"   <trackinfo blind=\"1\" mute=\"0\" locked=\"0\" trackname=\"\" type=\"audio\"/>\n"
	"   <trackinfo blind=\"0\" mute=\"0\" locked=\"0\" trackname=\"\"/>\n"
	"  </tracksinfo>\n"
	"  <kdenlive_producer audio_max=\"1\" id=\"1\" default_video=\"0\" fps=\"25.000000\" name=\"cut.avi\" \n"
	"  videocodec=\"DV (Digital Video)\" resource=\"%5$s\" default_audio=\"1\" \n"
	"  audiocodec=\"PCM signed 16-bit little-endian\" duration=\"%2$d\" \n"
	"  aspect_ratio=\"1.422222\" channels=\"2\" \n"
	"  frequency=\"48000\" video_max=\"0\" type=\"3\" frame_size=\"720x576\" \n"
	"  file_size=\"%4$" PRId64 "\"/>\n" 
	"  <markers/>\n"
	"  <groups/>\n"
	" </kdenlivedoc>\n"
	"</mlt>\n";
