#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "config.h"
#include "fuse-vdv.h"
#include "fuse-vdv-knowledge.h"
#include "fuse-vdv-opts.h"
#include "fuse-vdv-debug.h"
#include "fuse-vdv-kdenlive.h"
#include "fuse-vdv-smoothsort.h"
#include "fuse-vdv-wav-demux.h"
#include "fuse-vdv-shotcut.h"


int get_index_from_pathname(const char* path) {
	if (path == NULL) {
		debug_printf ("get_index_from_path(NULL)!\n", path);
		return -1;
	}
	debug_printf ("get_index_from_path('%s')\n", path);

	if (strcmp (path, "/") == 0) {
		return INDEX_ROOTDIR;
	} else if (strcmp (path, rawName) == 0) {
		return INDEX_RAW;
	} else if (strcmp (path, cutName) == 0) {
		return INDEX_CUT;
	} else if (strcmp (path, cutCompleteName) == 0) {
		return INDEX_CUTCOMPLETE;
	}
#ifdef CONFIG_WITHWAVDEMUX
	else if (strcmp (path, wav_filepath) == 0) {
		return INDEX_WAV;
	}
#endif
	else if (strcmp (path, "/pid") == 0) {
		return INDEX_PID;
	} else if (strcmp (path, opts_path) == 0) {
		return INDEX_OPTS;
	} else if (strcmp (path, "/inframe") == 0) {
		return INDEX_INFRAME;
	} else if (strcmp (path, "/outframe") == 0) {
		return INDEX_OUTFRAME;
	} else if (strcmp (path, kdenlive_path) == 0) {
		return INDEX_KDENLIVE;
	}else if (strcmp (path, shotcut_path) == 0) {
		return INDEX_SHOTCUT;
	}else if (strcmp (path, "/rebuild") == 0) {
		return INDEX_REBUILD;
	} else {
		debug_printf ("get_index_from_path: unknown path!\n");
	}
	return -1;
}


