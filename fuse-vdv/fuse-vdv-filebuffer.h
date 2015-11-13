
typedef struct _filebuffer {
	char * buffer; 
	size_t buffersize;	// 32bit size!
	size_t contentsize;	// 32bit size!
	pthread_mutex_t mutex;
} filebuffer_t;


// OOP light:

extern filebuffer_t* filebuffer__new ();
extern filebuffer_t* filebuffer__copy (filebuffer_t* source);
extern void filebuffer__destroy (filebuffer_t* source);

// warning: 32bit sizes!
extern size_t filebuffer__truncate(filebuffer_t* self, size_t new_size);
extern size_t filebuffer__contentsize (filebuffer_t* self);
extern size_t filebuffer__write (filebuffer_t* self, const char * source, size_t size, off_t offset); 
extern size_t filebuffer__read (filebuffer_t* self, off_t offset, char * target, size_t maxlength);
extern char* filebuffer__read_all_to_cstring (filebuffer_t* self);

