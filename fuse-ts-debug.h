#ifdef DEBUG
extern void print_parsed_opts(int argc_new);
extern void print_file_chain(const char * name, sourcefile_t * sourcefiles);
extern void print_file_chain_element(sourcefile_t * file);
extern void debug_printf(const char * text, ...);
#define DEPRECATED(...) debug_printf("XXX use of DEPRECATED function %s\n", __FUNCTION__);
#else
#define print_parsed_opts(...)
#define print_file_chain(...)
#define print_file_chain_element(...)
#define error_log1(...)
#define debug_printf(...)
#define debug_buffer(...)  
#define DEPRECATED(...)
#endif

extern void error_printf(const char * text, ...);


