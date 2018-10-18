#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <dirent.h>
#include <assert.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "fuse-ts.h"
#include "fuse-ts-tools.h"
#include "fuse-ts-filelist.h"
#include "fuse-ts-debug.h"


sourcefile_t *dupe_file_entry (sourcefile_t * source) {
	if (source == NULL) {
		return NULL;
	}
	sourcefile_t *ret = (sourcefile_t *) malloc (sizeof (sourcefile_t));
	memcpy (ret, source, sizeof (sourcefile_t));
	ret->filename = dupe_str (source->filename);
	ret->prev = NULL;
	ret->next = NULL;
	return ret;
}

sourcefile_t *dupe_file_list (sourcefile_t * source) {
	if (source == NULL) {
		return NULL;
	}
	sourcefile_t *ret = NULL;
	while (source != NULL) {
		ret = list_insert (ret, dupe_file_entry (source));
		source = source->next;
	}
	return ret;
}

sourcefile_t *new_file_entry_absolute_path (const char *filename) {
	sourcefile_t *ret = malloc (sizeof (sourcefile_t));
	ret->filename = dupe_str (filename);
	ret->next = NULL;
	ret->prev = NULL;
	ret->globalpos = 0;
//	ret->startpos = 0;
//	ret->endpos = 0;
//	ret->frames = 0;
	ret->filesize = 0;
	ret->tailhelper = NULL;
	ret->fhandle = NULL;
	ret->refcnt = 0;
	return ret;
}

sourcefile_t *new_file (const char *filename) {
	const char *merge[] = { base_dir, "/", filename };
	char *fn = merge_str (merge, 3);
	sourcefile_t *ret = new_file_entry_absolute_path (fn);
	ret->filesize = get_filesize (fn);
	free (fn);
	return (ret);
}

void destroy_file_entry (sourcefile_t * node) {
	if (node == NULL) {
		return;
	}
	if (node->filename != NULL) {
		free (node->filename);
	}
	free (node);
}

void purge_list (sourcefile_t * root) {
	if (root == NULL) {
		return;
	}
	sourcefile_t *t;
	while (root->next != NULL) {
		t = root;
		root = root->next;
		destroy_file_entry (t);
	}
	destroy_file_entry (root);
}

sourcefile_t *list_insert (sourcefile_t * list, sourcefile_t * node) {
	if (list == NULL) {
		list = node;
		node->prev = NULL;
		node->next = NULL;
		node->tailhelper = node;
	} else {
		sourcefile_t *t = list;
		if (t->tailhelper != NULL) {
			t = t->tailhelper;
		}
		while (t->next != NULL) {
			t = t->next;
		}
		t->next = node;
		node->prev = t;
		node->next = NULL;
		list->tailhelper = node;
	}
	return list;
}

int list_count (sourcefile_t * root) {
	if (root == NULL) {
		return 0;
	}
	int c = 1;
	sourcefile_t *t = root;
	while (root->next != NULL) {
		root = root->next;
		c++;
	}
	t->tailhelper = root;
	return c;
}

sourcefile_t **list_to_array (sourcefile_t * root, int *num) {
	if (root == NULL) {
		error_printf ("Error: empty list encountered!\n");
		exit (110);
	}
	int c = list_count (root);
	sourcefile_t **r = malloc (c * sizeof (sourcefile_t *));
	int i;
	for (i = 0; i < c; i++) {
		r[i] = root;
		root = root->next;
	}
	*num = c;
	return r;
}

int sort_partition (sourcefile_t ** files, int left, int right, int pivotIndex) {
	sourcefile_t *pivotValue = files[pivotIndex];
	files[pivotIndex] = files[right];
	files[right] = pivotValue;
	int storeIndex = left;
	int i = left;
	sourcefile_t *t = NULL;
	for (; i < right; i++) {
		if (strcmp (files[i]->filename, pivotValue->filename) <= 0) {
			t = files[i];
			files[i] = files[storeIndex];
			files[storeIndex] = t;
			storeIndex++;
		}
	}
	t = files[storeIndex];
	files[storeIndex] = files[right];
	files[right] = t;
	return storeIndex;
}

void sort_array (sourcefile_t ** files, int left, int right) {
	if (right > left) {
		int pi = left;
		int pin = sort_partition (files, left, right, pi);
		sort_array (files, left, pin - 1);
		sort_array (files, pin + 1, right);
	}
}

sourcefile_t *sort_list (sourcefile_t * files) {
	int num = 0;
	sourcefile_t **t = list_to_array (files, &num);
	if (num <= 0) {
		return files;
	}
	sort_array (t, 0, num - 1);

	// liste neu verketten
	(t[0])->prev = NULL;
	(t[num - 1])->next = NULL;
	int c = 0;
	for (; c < num - 1; c++) {
		(t[c])->next = t[c + 1];
		(t[c + 1])->prev = t[c];
	}
	(t[0])->tailhelper = t[num - 1];
	files = t[0];
	free (t);
	return files;
}

sourcefile_t *get_list_tail (sourcefile_t * list) {
	if (list == NULL) {
		return NULL;
	}
	sourcefile_t *r = list;
	if (list->tailhelper != NULL) {
		r = list->tailhelper;
	}
	while (r->next != NULL) {
		r = r->next;
	}
	list->tailhelper = r;
	return r;
}

void reorganize_list (sourcefile_t * root) {
	if (slidemode != 0) {
		reorganize_slide_list (root);
		return;
	}
	debug_printf ("reorg_list  ");
	if (root == NULL) {
		return;
	}
	sourcefile_t *t = root;
	sourcefile_t *th = root;
	off_t gpos = 0;
//	int gframes = 0;
	while (t != NULL) {
		t->globalpos = gpos;
		gpos += (long long) t->filesize;
//		gpos -= (long long) t->startpos;
//		gframes += t->frames;
//		t->gframes = gframes;
		th = t;
		t = t->next;
	}
	root->tailhelper = th;
	debug_printf ("reorg_list end\n");
}

void reorganize_slide_list (sourcefile_t * root) {
	debug_printf ("reorg_list slidemode  ");
	if (root == NULL) {
		return;
	}
	sourcefile_t *t = root;
	sourcefile_t *th = root;
	off_t gpos = 0;
	while (t != NULL) {
		if (t->next != NULL) {
			char *filename = get_datestring_from_filename (t->filename);
			int ts1 = datestring_to_timestamp (filename);
			free (filename);
			filename = get_datestring_from_filename (t->next->filename);
			int ts2 = datestring_to_timestamp (filename);
			free (filename);
			t->repeat = (ts2 - ts1) * frames_per_second;
		} else {
			t->repeat = 250;
		}
		t->filesize = get_filesize(t->filename);
		t->globalpos = gpos;
		gpos += (long long) t->filesize * t->repeat;
		th = t;
		t = t->next;
	}
	root->tailhelper = th;
	debug_printf ("reorg_list end\n");
}

sourcefile_t *drop_list_tail (sourcefile_t * list) {
	if (list == NULL) {
		return NULL;
	}
	sourcefile_t *t = get_list_tail (list);
	if (t == NULL) {
		error_printf ("Strange things happen!\n");
		exit (114);
	}
	sourcefile_t *p = NULL;
	if (t->prev != NULL) {
		p = t->prev;
		p->next = NULL;
		list->tailhelper = p;
	}
	destroy_file_entry (t);
	if (t == list) {
		return NULL;
	}
	return list;
}

sourcefile_t *drop_list_head (sourcefile_t * list) {
	if (list == NULL) {
		return NULL;
	}
	sourcefile_t *t = list->next;
	if (t != NULL) {
		t->tailhelper = list->tailhelper;
		t->prev = NULL;
	}
	destroy_file_entry (list);
	return t;
}

sourcefile_t *add_file_to_list (sourcefile_t * list, sourcefile_t * entry) {
	if (entry == NULL) {
		error_printf ("Error: null pointer!\n");
		exit (111);
	}

	sourcefile_t *t = dupe_file_entry (entry);
	list = list_insert (list, t);
	off_t filesize = get_filesize (t->filename);
	off_t gpos = 0;

	if (t->prev != NULL) {
		gpos = t->prev->totalsize;
	}
	t->globalpos = gpos;
	t->filesize = filesize;
	t->totalsize = gpos + filesize;
	debug_printf ("adding sourcefile '%s' ( %" PRId64 " bytes) with virtual offset %" PRId64 "\n", t->filename, filesize, gpos);
	return list;
}

sourcefile_t *add_file_to_list_head (sourcefile_t * list, sourcefile_t * entry) {
	sourcefile_t *ret = NULL;
	ret = add_file_to_list (ret, entry);
	ret->next = list;
	list->prev = ret;
	reorganize_list (ret);
	return ret;
}


sourcefile_t *get_sourcefile_for_position (sourcefile_t * list, off_t pos) {
	if (list == NULL) {
		return NULL;
	}
	sourcefile_t *t = list;
        // get beginning of correct file
        while (t != NULL) {
                if (t->globalpos >= pos) {
                        if (t->globalpos > pos) {
                                t = t->prev;
                        }
                        break;
                }
                t = t->next;
        }
        // special treatment if last file must be read
        if (t == NULL) {
                t = get_list_tail (list);
        }
        // now the piece is in t
        assert (t != NULL);
	return t;
}


off_t get_filesize (char *name) {
	struct stat st;
	memset (&st, 0, sizeof (stat));
	stat (name, &st);
	off_t size = st.st_size;
	return size;
}

sourcefile_t *get_files_with_prefix (const char *prefix, int *num) {
	DIR *hdir;
	struct dirent *entry;
	sourcefile_t *list = NULL;
	size_t l = safe_strlen (prefix);
	hdir = opendir (base_dir);
	do {
		entry = readdir (hdir);
		if (entry) {
			if (0 == l || strncmp (entry->d_name, prefix, l) == 0) {
				list = list_insert (list, new_file (entry->d_name));
				debug_printf ("found file: '%s'\n", entry->d_name);
			}
		}
	} while (entry);
	closedir (hdir);
	*num = list_count (list);
	debug_printf ("found %d files.\n", *num);
	return list;
}

char *get_prefix_with_path () {
	const char *merge[] = { base_dir, "/", prefix, start_time };
	char *ret = merge_str (merge, 4);
	return ret;
}

int file_exists (const char *filename) {
	debug_printf ("checking existence of '%s'\n", filename);
	if ((filename == NULL) || (filename[0] == 0)) {
		return 0;
	}
	char *fn = NULL;
	if (filename[0] != '/') {
		const char *merge[] = { base_dir, "/", filename };
		char *fn = merge_str (merge, 3);
		filename = fn;
	}
	int ret = 0;
	FILE *f = fopen (filename, "r");
	if (f) {
		ret = 1;
		fclose (f);
	}
	if (fn != NULL) {
		free (fn);
	}
	return ret;
}

void close_file_handles (sourcefile_t * files) {
	if (files == NULL) {
		return;
	}
	while (files->next != NULL) {
		if (files->fhandle != NULL) {
			fclose (files->fhandle);
		}
		files->fhandle = NULL;
		files = files->next;
	}
	if (files->fhandle != NULL) {
		fclose (files->fhandle);
	}
	files->fhandle = NULL;
}

sourcefile_t **filechains_grow (sourcefile_t ** oldchain, int oldsize, int newsize) {
	int i;
	debug_printf ("growing filechains array to new size %d.\n", newsize);
	sourcefile_t **ret = (sourcefile_t **) malloc (newsize * sizeof (sourcefile_t *));
	for (i = 0; i < oldsize; i++) {
		ret[i] = oldchain[i];
	}
	for (i = oldsize; i < newsize; i++) {
		ret[i] = NULL;
	}
	return ret;
}

fileposhint_t **filehints_grow (fileposhint_t ** old, int oldsize, int newsize) {
	int i;
	debug_printf ("growing filehints array to new size %d.\n", newsize);
	fileposhint_t **ret = (fileposhint_t **) malloc (newsize * sizeof (fileposhint_t*));
	for (i = 0; i < oldsize; i++) {
		ret[i] = old[i];
	}
	for (i = oldsize; i < newsize; i++) {
		ret[i] = NULL;
	}
	return ret;
}

uint64_t insert_into_filechains_list (sourcefile_t * files) {
	int i;
	int c;

	if (filechains_size == 0) {
		//first call, create array
		debug_printf ("Creating filechains array with initial size of 5\n");
		filechains = (sourcefile_t **) malloc (5 * sizeof (sourcefile_t *));
		for (i = 0; i < 5; i++) {
			filechains[i] = NULL;
		}
		filechains_size = 5;
	}
	// dummy test
	if (files == NULL) {
		debug_printf ("NULL filepiece should be inserted to list\n");
		return 0;
	}
	debug_printf ("inserting filepiece with refcnt %d into array\n", files->refcnt);
	files->refcnt++;
	// first run, try to find dupe
	// count free slots in c
	c = 0;
	for (i = 0; i < filechains_size; i++) {
		if (filechains[i] == files) {
			debug_printf ("Entry %d of filechains was identical, assigning it. refcnt is now: %d.\n", i, filechains[i]->refcnt);
			return i;
		}
		if (filechains[i] == NULL) {
			c++;
		}
	}
	// no free slots? enlarge it
	if (c == 0) {
		int newsize = filechains_size * 2;
		sourcefile_t **newfiles = NULL;
		newfiles = filechains_grow (filechains, filechains_size, newsize);
		filechains = newfiles;
		filechains_size = newsize;
	}
	// second run, find first free slot
	for (i = 0; i < filechains_size; i++) {
		if (filechains[i] == NULL) {
			debug_printf ("Entry %d of filechains was empty, assigning chain to it.\n", i);
			filechains[i] = files;
			return i;
		}
	}
	return 0;
}

void remove_from_filechains_list (uint64_t handle) {
	sourcefile_t *chain = NULL;
	int i = (int) handle;

	debug_printf ("Releasing file handle %d from array\n", handle);

	if (handle >= filechains_size) {
		error_printf ("Extremely wrong file handle given!\n");
		return;
	}

	chain = filechains[i];
	if (chain == NULL) {
		error_printf ("WARNING: stale file chain released!\n");
		return;
	}

	debug_printf ("Refcnt was %d\n", chain->refcnt);
	chain->refcnt--;
	if (chain->refcnt == 0) {
		filechains[i] = NULL;
		if (handle >= filehints_size) {
			error_printf ("Extremely wrong file handle given!\n");
		} else {
			if (filehints[handle] != NULL) {
				free(filehints[handle]);
				filehints[handle] = NULL;
			}
		}
		close_file_handles (chain);
		purge_list (chain);
	}
}

