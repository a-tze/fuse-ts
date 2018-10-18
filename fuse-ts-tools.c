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
#include <time.h>
#include "fuse-ts.h"
#include "fuse-ts-tools.h"
#include "fuse-ts-debug.h"

// function from STL

char *strptime(const char *buf, const char *format, struct tm *tm);

char *dupe_str (const char *source) {
	size_t L = safe_strlen (source);
	if (L > max_str_len) {
		error_printf ("Exception: string that is too long - aborting\n");
		exit (1000);
	}
	char *ret = malloc (L + 1);
	memcpy (ret, source, L);
	ret[L] = 0;
	return ret;
}

char *dupe_str_n (const char *source, size_t count) {
	size_t L = safe_strlen (source);
	if (L > count) {
		L = count;
	}
	char *ret = malloc (L + 1);
	memcpy (ret, source, L);
	ret[L] = 0;
	return ret;
}

char *merge_str (const char **sources, size_t count) {
	int i = 0;
	size_t LL[count];
	size_t L = 0;
	for (; i < count; i++) {
		size_t l = safe_strlen (sources[i]);
		if (l > max_str_len) {
			error_printf ("Exception: string that is too long - aborting\n");
			exit (1002);
		}
		LL[i] = l;
		L += l;
	}
	char *ret = malloc (L + 1);
	size_t o = 0;
	for (i = 0; i < count; i++) {
		if (sources[i] != NULL) {
			memcpy (ret + o, sources[i], LL[i]);
			o += LL[i];
		}
	}
	ret[L] = 0;
	return ret;
}

char *merge_strs (int count, ...) {
	if (count == 0) {
		return NULL;
	}
	va_list args;
	va_start (args, count);
	//char ** t = (char**) malloc(count * sizeof(char*));
	const char *t[count];
	int i = 0;
	for (; i < count; i++) {
		t[i] = va_arg (args, char *);
	}
	va_end (args);
	char *ret = merge_str (t, count);
//  free(t);
	return ret;
}

char *append_and_free (char *s1, char *s2) {
	size_t L1 = safe_strlen (s1);
	size_t L2 = safe_strlen (s2);
	if (L1 + L2 > max_str_len) {
		error_printf ("Exception: string that is too long - aborting\n");
		exit (1002);
	}
	char *ret = malloc (L1 + L2 + 1);
	memcpy (ret, s1, L1);
	memcpy (ret + L1, s2, L2);
	ret[L1 + L2] = 0;
	free (s1);
	free (s2);
	return ret;
}

char *update_int_string (char *str, int value, size_t * length) {
	char *ret = (char *) malloc (16);
	int l = snprintf (ret, 16, "%d\n", value);
	if (l > 14) {
		debug_printf ("update_int_string: FAIL: int too long (more than 14 characters) ???\n");
		ret[0] = 0;
		l = 0;
	}
	if (str != NULL) {
		free (str);
	}
	if (length != NULL) {
		*length = l;
	}
	return ret;
}

char *update_string_string (char *str, const char *value, size_t *length) {
	if (value == NULL) {
		debug_printf("WARNING: NULL value given to update_string_string\n");
		return str;
	}
	if (str != NULL) {
		free(str);
	}
	*length = strlen(value);
	return dupe_str(value);
}

size_t write_to_string (const char *buffer, size_t size, off_t offset, char **target) {
	if ((target == NULL) || (*target == NULL)) {
		debug_printf ("writing string failed: NULL target!\n");
		return -EACCES;
	}
	size_t writebuffersize = strlen (*target);
	return write_to_buffer (buffer, size, offset, target, &writebuffersize);
}

size_t safe_strlen (const char *str) {
	if (str == NULL) {
		return 0;
	}
	return strlen (str);
}

size_t truncate_buffer (char ** target, size_t bufferlength, size_t newbufferlength) {
	// let shrinking happen without reallocation, growing will trigger new malloc, even if it would fit
	if (newbufferlength <= bufferlength) {
		return newbufferlength;
	}
	char *old = *target;
	*target = (char *) calloc (1, newbufferlength);
	if (!*target) {
		// malloc failed
		*target = old;
		return -EACCES;
	}
	memcpy (*target, old, bufferlength);
	free (old);
	return newbufferlength;
}

size_t write_to_buffer (const char *buffer, size_t size, off_t offset, char **target, size_t * bufferlength) {
	DEPRECATED();
	debug_printf ("write_to_buffer:  writing %d bytes to %p with offset %" PRId64 " and bufferlength %d\n", size, *target, offset, *bufferlength);
	if ((target == NULL) || (bufferlength == NULL)) {
		debug_printf ("writing buffer failed: NULL pointer!\n");
		return -EACCES;
	}
	size_t writebuffersize = (*bufferlength);
	if (*target == NULL) {
		debug_printf ("write_to_buffer: creating new buffer for NULL target\n");
		writebuffersize = offset + size + 1;
		*target = (char *) malloc (writebuffersize);
		memset (*target, 0, writebuffersize);
		writebuffersize--;
	} else if ((offset + size) > writebuffersize) {
		writebuffersize = offset + size + 1;
		size_t l = truncate_buffer(target, *bufferlength, writebuffersize);
		if (!l || l < writebuffersize) {
			error_printf ("truncating buffer failed!\n");
			return -EACCES;
		}
		writebuffersize--;
	}
	assert (writebuffersize >= offset + size);
	char *dest = *target;
	memcpy (dest + offset, buffer, size);
	*bufferlength = writebuffersize;
	return size;
}

size_t string_read (const char *str, char *buf, size_t size, const off_t offset) {
	size_t length = safe_strlen (str);
	if ((length == 0) || (offset >= length) || (offset < 0)) {
		return 0;
	}
	return string_read_with_length (str, buf, size, offset, length);
}

size_t string_read_with_length (const char *str, char *buf, size_t size, const off_t offset, size_t length) {
	if ((str == NULL) || (length == 0) || (offset >= length) || (offset < 0)) {
		return 0;
	}
	assert (offset >= 0);
	if ((offset + size) > length) {
		size = length - offset;
	}
	memcpy (buf, str + offset, size);
	return size;
}

char *get_datestring_from_filename (const char *filename) {
	char *ret = dupe_str (filename);
	if (filename == NULL) {
		return ret;
	}
	const char *dot = strrchr (filename, '.');
	size_t s;
	if (dot == NULL) {
		s = safe_strlen (filename);
	} else {
		if (safe_strlen (filename) - (dot - filename) <= 5) {
			s = dot - filename;
		} else {
			s = safe_strlen (filename);
		}
	}
	if (s <= 18 || s > 1024) {
		return ret;
	}
	free (ret);
	return dupe_str_n ((const char *) (filename + s - 19), 19);
}

char *equalize_date_string (const char *s) {
	char *ret = dupe_str (s);
	if (safe_strlen (s) > 16) {
		ret[4] = 0x20;
		ret[7] = 0x20;
		ret[10] = 0x20;
		ret[13] = 0x20;
		ret[16] = 0x20;
	}
	return ret;
}

int compare_date_strings (const char *s1, const char *s2) {
	char *t1 = equalize_date_string (s1);
	char *t2 = equalize_date_string (s2);
	int ret = strcmp (t1, t2);
	free (t1);
	free (t2);
	return ret;
}

/* does NOT care of month and year! */

int datestring_to_timestamp (const char *s) {
	if (safe_strlen (s) < 19) {
		return 0;
	}
	int ret = (s[8] - '0') * 10;
	ret += s[9] - '0';
	ret *= 24;
	ret += (s[11] - '0') * 10;
	ret += s[12] - '0';
	ret *= 60;
	ret += (s[14] - '0') * 10;
	ret += s[15] - '0';
	ret *= 60;
	ret += (s[17] - '0') * 10;
	ret += s[18] - '0';
	if (ret < 0) {
		return 0;
	}
	return ret;
}

int get_unix_timestamp_from_filename (const char *filename) {
	if (filename == NULL) {
		return 0;
	}
	char * datestring = get_datestring_from_filename(filename);
	if (datestring == NULL) {
		return 0;
	}
//fprintf (stdout, "datestring: '%s'\n", datestring);
	char * datestring2 = equalize_date_string(datestring);
	free(datestring);
	if (datestring2 == NULL) {
		return 0;
	}
//fprintf (stdout, "datestring2: '%s'\n", datestring2);
	struct tm tm;
	time_t epoch;
	tm.tm_isdst = 0;
	if ( strptime(datestring2, "%Y %m %d %H %M %S", &tm) != NULL ) {
		tm.tm_isdst = 0;
		epoch = mktime(&tm);
	} else {
		epoch = 0;
		error_printf ("Error: Can not parse date and time from filename: %s\n", filename);
	}
	free(datestring2);
	return (int) epoch;
}

/* returns given number of frames as fractioned seconds,
   e.g. 251 -> 10.040
*/
char * frames_to_seconds (int frames, int fps) {
	char * ret = malloc(32); // must be enough :)
	int len = snprintf (ret, 31, "%d.%03d", frames / fps, (int)((frames % fps) * (1000.0 / frames_per_second )));
	if (len > 30) {
		debug_printf ("frames_to_seconds: unusual string size for converting frame number %d at frame rate %d\n", frames, fps);
		ret[31] = 0;
	}
	return ret;
}

