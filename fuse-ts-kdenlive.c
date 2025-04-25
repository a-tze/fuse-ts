#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <assert.h>
#include <mxml.h>
#include <pthread.h>
#include "fuse-ts.h"
#include "fuse-ts-tools.h"
#include "fuse-ts-debug.h"
#include "fuse-ts-xml.h"
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

bool format_frame_count_to_timestring (char *output, size_t output_size, int frame_count) {
	bool neg = false;
	if (frame_count < 0) {
		frame_count = -frame_count;
		neg = true;
	}
	int hours = frame_count / (3600 * frames_per_second);
	frame_count %= 3600 * frames_per_second;
	int minutes = frame_count / (60 * frames_per_second);
	frame_count %= 60 * frames_per_second;
	float seconds = (float) frame_count / frames_per_second;
	int len;
	if (neg)
		len = snprintf(output, output_size, "-%02d:%02d:%02.3f", hours, minutes, seconds);
	else
		len = snprintf(output, output_size, "%02d:%02d:%02.3f", hours, minutes, seconds);
	return len < output_size;
}

filebuffer_t* get_kdenlive_project_file_cache (const char *filename, int num_frames, int blanklen) {
	pthread_mutex_lock (&kl_cachemutex);
	if ((kl_project_file_cache != NULL) && (kl_project_file_cache_inframe == inframe) && (kl_project_file_cache_outframe == outframe) && (kl_project_file_cache_blanklen == blanklen)) {
		debug_printf ("%s: cache hit (%p)\n", __func__, kl_project_file_cache);
		pthread_mutex_unlock (&kl_cachemutex);
		return kl_project_file_cache;
	}
	int _outframe = (outframe < 0) ? totalframes : outframe;
	int _inframe = inframe;
	char *t = merge_strs (3, mountpoint, "/", filename);

	char blanklen_timestr[20];
	char intime_timestr[20];
	char outtime_timestr[20];
	char numframes_timestr[20];
	char numframes_m1_timestr[20];
	bool format_success = true;
	format_success = format_success && format_frame_count_to_timestring(blanklen_timestr, sizeof(blanklen_timestr), blanklen);
	format_success = format_success && format_frame_count_to_timestring(intime_timestr, sizeof(intime_timestr), _inframe);
	format_success = format_success && format_frame_count_to_timestring(outtime_timestr, sizeof(outtime_timestr), _outframe);
	format_success = format_success && format_frame_count_to_timestring(numframes_timestr, sizeof(numframes_timestr), num_frames);
	format_success = format_success && format_frame_count_to_timestring(numframes_m1_timestr, sizeof(numframes_m1_timestr), num_frames-1);
	if (!format_success) err(124, "%s: size fail when formatting time string\n", __func__);

	const size_t size = strlen(kl_template) * 2;
	if (kl_project_file_cache == NULL) kl_project_file_cache = filebuffer__new();
	char* temp = (char *) malloc (size);
	CHECK_OOM(temp);
	int len = snprintf (temp, size - 1, kl_template,
			_inframe,
			num_frames,
			num_frames - 1,
			outbyte,
			t,
			_outframe,
			blanklen,
			frames_per_second,
			width,
			height,
			blanklen_timestr,
			intime_timestr,
			outtime_timestr,
			numframes_timestr,
			numframes_m1_timestr);
	if (len >= size) err(124, "%s: size fail when generating project file\n", __func__);
	debug_printf ("%s: result has a size of: %d\n", __func__, len);
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
	debug_printf ("%s: result is: %d\n", __func__, filebuffer__contentsize(fb));
	return filebuffer__contentsize(fb);
}

void init_kdenlive_project_file () {
	pthread_mutex_lock (&kl_cachemutex);
	if (kl_project_file_cache != NULL) {
		debug_printf ("%s: freeing cache %p\n", __func__, kl_project_file_cache);
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
	debug_printf ("%s\n", __func__);
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

static int parse_time_string(const char *in_str)
{
	double sum = 0;
	const char *token_ptr = in_str;
	const char *sep_ptr;
	while ((sep_ptr = strchr(token_ptr, ':')))
	{
		double t = atof(token_ptr);
		if (sum < 0)
			sum = 60*sum - t;
		else
			sum = 60*sum + t;
		token_ptr = sep_ptr+1;
	}
	double t = atof(token_ptr);
	if (sum < 0)
		sum = 60*sum - t;
	else
		sum = 60*sum + t;
	return (int) (sum * frames_per_second);
}

int find_cutmarks_in_kdenlive_project_file (int *inframe, int *outframe, int *blanklen) {
	if (kl_writebuffer == NULL) {
		debug_printf ("find_cutmarks: file has not been written to.\n");
		return 100;
	}
/*
  in XPATH, I would look for 
    playlist[@id='playlist0']/entry[@producer='chain0']/@in
  and
    playlist[@id='playlist0']/entry[@producer='chain0']/@out
*/
	mxml_node_t *xmldoc;
	char* temp = filebuffer__read_all_to_cstring(kl_writebuffer);
	xmldoc = XMLLOAD(temp);
	free(temp);
	if (NULL == xmldoc) {
		debug_printf ("find_cutmarks: no valid XML!\n");
		return 1;
	}
	mxml_node_t *node, *subnode;
	node = mxmlFindElement (xmldoc, xmldoc, "playlist", "id", "playlist0", MXML_DESCEND_ALL);
	if (NULL == node) {
		debug_printf ("find_cutmarks: node with id 'playlist0' not found!\n");
		mxmlRelease (xmldoc);
		return 2;
	}

	int blank = 0;
	subnode = mxmlFindElement (node, node, "blank", NULL, NULL, MXML_DESCEND_ALL);
	if (NULL == subnode) {
		debug_printf ("find_cutmarks: node 'blank' not found - assuming 0!\n");
	} else {
		const char *strblank = mxmlElementGetAttr (subnode, "length");
		if (NULL != strblank) {
			blank = parse_time_string (strblank);
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

	node = mxmlFindElement (node, node, "entry", "producer", "chain0", MXML_DESCEND_ALL);
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
	int in = parse_time_string (strin);
	int out = parse_time_string (strout);
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
"<mlt LC_NUMERIC=\"C\" producer=\"main_bin\" version=\"7.22.0\" root=\"/tmp\">"
" <!-- %1$d => inframe,  %2$d => frames, %3$d => frames - 1  "
"  %4$" PRId64 " => filesize, %5$s => filename with path "
"  %6$d => outframe, %7$d => blanklen, "
"  %8$d => frames_per_second, %9$d => width, %10$d => height "
"  %11$s => blanklen_time, %12$s => intime "
"  %13$s => outtime, %14$s => total_time, %15$s => total_time - 1 frame --> "
// Omitting aspect ratio crashes KDEnlive, so living with hardcoded values.
// Cutting other formats should be possible anyway, KDEnlive will just scale
// it wrong if it is not 16:9.
" <profile width=\"%9$d\" frame_rate_den=\"1\" height=\"%10$d\" "
" display_aspect_num=\"16\" display_aspect_den=\"9\" frame_rate_num=\"%8$d\" "
" colorspace=\"709\" sample_aspect_den=\"1\" description=\"HD 1080i %8$d fps\" "
" progressive=\"0\" sample_aspect_num=\"1\"/> "
" <producer id=\"producer0\" in=\"00:00:00.000\" out=\"%14$s\">"
"  <property name=\"length\">2147483647</property>"  // maxint
"  <property name=\"eof\">continue</property>"
"  <property name=\"resource\">black</property>"
"  <property name=\"aspect_ratio\">1</property>"
"  <property name=\"mlt_service\">color</property>"
"  <property name=\"kdenlive:playlistid\">black_track</property>"
" </producer>"
" <chain id=\"chain0\" out=\"%15$s\">"
"  <property name=\"length\">%2$d</property>"
"  <property name=\"eof\">pause</property>"
"  <property name=\"resource\">%5$s</property>"
"  <property name=\"audio_index\">1</property>"
"  <property name=\"video_index\">0</property>"
"  <property name=\"kdenlive:id\">4</property>"
"  <property name=\"mute_on_pause\">0</property>"
" </chain>"
" <playlist id=\"playlist0\">"
"  <property name=\"kdenlive:audio_track\">1</property>"
"  <blank length=\"%11$s\"/>"
"  <entry producer=\"chain0\" in=\"%12$s\" out=\"%13$s\">"
"   <property name=\"kdenlive:id\">4</property>"
"  </entry>"
" </playlist>"
" <playlist id=\"playlist1\">"
"  <property name=\"kdenlive:audio_track\">1</property>"
" </playlist>"
" <tractor id=\"tractor0\" in=\"00:00:00.000\" out=\"%15$s\">"
"  <property name=\"kdenlive:audio_track\">1</property>"
"  <property name=\"kdenlive:trackheight\">59</property>"
"  <property name=\"kdenlive:timeline_active\">1</property>"
"  <property name=\"kdenlive:collapsed\">0</property>"
"  <track hide=\"video\" producer=\"playlist0\"/>"
"  <track hide=\"video\" producer=\"playlist1\"/>"
" </tractor>"
" <chain id=\"chain1\" out=\"%15$s\">"
"  <property name=\"length\">%2$d</property>"
"  <property name=\"eof\">pause</property>"
"  <property name=\"resource\">%5$s</property>"
"  <property name=\"audio_index\">1</property>"
"  <property name=\"video_index\">0</property>"
"  <property name=\"kdenlive:id\">4</property>"
"  <property name=\"mute_on_pause\">0</property>"
" </chain>"
" <playlist id=\"playlist2\">"
"  <blank length=\"%11$s\"/>"
"  <entry producer=\"chain1\" in=\"%12$s\" out=\"%13$s\">"
"   <property name=\"kdenlive:id\">4</property>"
"  </entry>"
" </playlist>"
" <playlist id=\"playlist3\"/>"
" <tractor id=\"tractor1\" in=\"00:00:00.000\" out=\"%15$s\">"
"  <property name=\"kdenlive:trackheight\">59</property>"
"  <property name=\"kdenlive:timeline_active\">1</property>"
"  <property name=\"kdenlive:collapsed\">0</property>"
"  <property name=\"kdenlive:track_name\">Cut Me -&gt;</property>"
"  <track hide=\"audio\" producer=\"playlist2\"/>"
"  <track hide=\"audio\" producer=\"playlist3\"/>"
" </tractor>"
" <tractor id=\"maintractor\" in=\"00:00:00.000\" out=\"%14$s\">"
"  <property name=\"kdenlive:uuid\">maintractor</property>"
"  <property name=\"kdenlive:clipname\">Sequence 1</property>"
"  <property name=\"kdenlive:sequenceproperties.hasAudio\">1</property>"
"  <property name=\"kdenlive:sequenceproperties.hasVideo\">1</property>"
"  <property name=\"kdenlive:sequenceproperties.activeTrack\">1</property>"
"  <property name=\"kdenlive:sequenceproperties.tracksCount\">2</property>"
"  <property name=\"kdenlive:sequenceproperties.documentuuid\">maintractor</property>"
"  <property name=\"kdenlive:duration\">%14$s</property>"
"  <property name=\"kdenlive:maxduration\">%2$d</property>"
"  <property name=\"kdenlive:producer_type\">17</property>"
"  <property name=\"kdenlive:id\">3</property>"
"  <property name=\"kdenlive:clip_type\">0</property>"
"  <property name=\"kdenlive:sequenceproperties.groups\">[ { \"children\": [ { \"data\": \"1:0\", \"leaf\": \"clip\", \"type\": \"Leaf\" }, { \"data\": \"0:0\", \"leaf\": \"clip\", \"type\": \"Leaf\" } ], \"type\": \"AVSplit\" } ] </property>"
"  <track producer=\"producer0\"/>"
"  <track producer=\"tractor0\"/>"
"  <track producer=\"tractor1\"/>"
" </tractor>"
" <chain id=\"chain2\" out=\"%15$s\">"
"  <property name=\"length\">%2$d</property>"
"  <property name=\"eof\">pause</property>"
"  <property name=\"resource\">%5$s</property>"
"  <property name=\"audio_index\">1</property>"
"  <property name=\"video_index\">0</property>"
"  <property name=\"kdenlive:id\">4</property>"
"  <property name=\"mute_on_pause\">0</property>"
"  <property name=\"kdenlive:clip_type\">0</property>"
" </chain>"
" <playlist id=\"main_bin\">"
"  <property name=\"kdenlive:docproperties.kdenliveversion\">23.08.5</property>"
"  <property name=\"kdenlive:docproperties.uuid\">maintractor</property>"
"  <property name=\"kdenlive:docproperties.version\">1.1</property>"
"  <property name=\"kdenlive:docproperties.opensequences\">maintractor</property>"
"  <property name=\"kdenlive:docproperties.activetimeline\">maintractor</property>"
"  <property name=\"xml_retain\">1</property>"
"  <entry producer=\"maintractor\" in=\"00:00:00.000\" out=\"00:00:00.000\"/>"
"  <entry producer=\"chain2\" in=\"00:00:00.000\" out=\"%15$s\"/>"
" </playlist>"
" <tractor id=\"tractor2\" in=\"00:00:00.000\" out=\"%14$s\">"
"  <property name=\"kdenlive:projectTractor\">1</property>"
"  <track producer=\"maintractor\" in=\"00:00:00.000\" out=\"%14$s\"/>"
" </tractor>"
"</mlt>"
;
