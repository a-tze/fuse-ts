#ifdef DEBUG
extern void debug_printf(const char * text, ...);
#define DEPRECATED(...) debug_printf("XXX use of DEPRECATED function %s\n", __FUNCTION__);
#else
#define debug_printf(...)
#define DEPRECATED(...)
#endif

extern void print_parsed_opts(int argc_new);
extern void print_file_chain(const char * name, sourcefile_t * sourcefiles);
extern void print_file_chain_element(sourcefile_t * file);

extern void info_printf(const char * text, ...);
extern void error_printf(const char * text, ...);

#define MAX_LOGBUFFER_SIZE 8 * 1024

