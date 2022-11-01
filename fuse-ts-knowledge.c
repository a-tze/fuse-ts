#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fuse-ts.h"
#include "fuse-ts-knowledge.h"
#include "fuse-ts-opts.h"
#include "fuse-ts-debug.h"
#include "fuse-ts-kdenlive.h"
#include "fuse-ts-smoothsort.h"
#include "fuse-ts-shotcut.h"

int get_index_from_pathname(const char* path) {
	if (path == NULL) {
		debug_printf ("get_index_from_path(NULL)!!!\n", path);
		return -1;
	}
	if (strcmp (path, "/") == 0) {
		return INDEX_ROOTDIR;
	} else if (strcmp (path, rawName) == 0) {
		return INDEX_RAW;
	} else if (strcmp (path, "/intime") == 0) {
		return INDEX_INTIME;
	} else if (strcmp (path, "/outtime") == 0) {
		return INDEX_OUTTIME;
	} else if (strcmp (path, "/inframe") == 0) {
		return INDEX_INFRAME;
	} else if (strcmp (path, "/outframe") == 0) {
		return INDEX_OUTFRAME;
	} else if (strcmp (path, "/pid") == 0) {
		return INDEX_PID;
	} else if (strcmp (path, opts_path) == 0) {
		return INDEX_OPTS;
	} else if (strcmp (path, durationName) == 0) {
		return INDEX_DURATION;
	} else if (strcmp (path, kdenlive_path) == 0) {
		return INDEX_KDENLIVE;
	} else if (path == strstr(path, "/project.kdenlive.") && kdenlive_tmp_path) {
		return INDEX_KDENLIVE_TMP;
	} else if (strcmp (path, shotcut_path) == 0) {
		return INDEX_SHOTCUT;
	} else if (shotcut_tmp_path && (
		(strncmp (path, "/shotcut-", 9) == 0) ||
		(strncmp (path, "/project_shotcut.mlt.", 21) == 0)
	)) {
		return INDEX_SHOTCUT_TMP;
	} else if (strcmp (path, "/rebuild") == 0) {
		return INDEX_REBUILD;
	} else if (strcmp (path, "/filelist") == 0) {
		return INDEX_FILELIST;
	} else if (strcmp (path, "/log") == 0) {
		return INDEX_LOG;
	} else {
		debug_printf ("get_index_from_path: unknown path!\n");
	}
	return -1;
}


