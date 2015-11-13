// some sane default... the longest stuff are filenames
static const size_t max_str_len = 8192;

///////////////// string stuff  ///////////////

extern char * dupe_str (const char * source);
extern char * dupe_str_n (const char * source, size_t count);
extern char * merge_str (const char ** sources, size_t count);
extern char * merge_strs (int count, ...);
extern char * append_and_free (char * s1, char * s2);
extern char * update_int_string (char * str, int value, size_t* length);
extern size_t write_to_string (const char * buffer, size_t size, off_t offset, char ** target);
extern size_t safe_strlen (const char * str);
extern size_t string_read (const char * str, char * buf, size_t size, const off_t offset);
extern size_t string_read_with_length (const char * str, char * buf, size_t size, const off_t offset, size_t length);
extern char * get_datestring_from_filename(const char * filename);
extern char * equalize_date_string (const char * s);
extern int    compare_date_strings (const char * s1, const char * s2);
extern int    datestring_to_timestamp (const char * s);

// TODO: eliminate:
extern size_t write_to_buffer (const char *buffer, size_t size, off_t offset, char **target, size_t * bufferlength);

///////////////// file stuff  ///////////////

extern int file_exists (const char * filename);

///////////////// memory stuff  ///////////////

#include <err.h>
#define CHECK_OOM(pointer) if (pointer == NULL) err (123, "Out of memory! (function: %s) Exiting!\n", __FUNCTION__);

