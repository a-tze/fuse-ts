#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>
#include <sys/types.h>
#include <pthread.h>
#include "fuse-ts.h"
#include "fuse-ts-tools.h"
#include "fuse-ts-debug.h"
#include "fuse-ts-filebuffer.h"

static size_t filebuffer__truncate__unlocked(filebuffer_t* self, size_t new_size);

size_t filebuffer__write (filebuffer_t* self, const char * source, size_t size, off_t offset) {
	debug_printf ("filebuffer__write:  writing %d bytes from %p to %p with offset %" PRId64 "\n", size, source, self, offset);
	if (self == NULL) {
		error_printf ("writing buffer failed: NULL pointer!\n");
		return -EACCES;
	}
	if (offset + size > (SIZE_MAX - 1)) {
		error_printf ("writing buffer failed: offset too big!\n");
		return -EACCES;
	}
	pthread_mutex_lock(&(self->mutex));
	if ((self->buffer == NULL) || ((offset + size) > self->buffersize)) { // need buffer change
		filebuffer__truncate__unlocked(self, offset + size);
	}
	assert (self->buffersize >= offset + size);
	char *dest = self->buffer;
	memcpy (dest + offset, source, size);
	if (self->contentsize < offset + size) self->contentsize = offset + size;
	pthread_mutex_unlock(&(self->mutex));
	return size;
}

filebuffer_t* filebuffer__new () {
	filebuffer_t* ret = (filebuffer_t*) malloc(sizeof(filebuffer_t));
	CHECK_OOM (ret);
	ret->buffer = NULL;
	ret->buffersize = 0;
	ret->contentsize = 0;
	pthread_mutex_init(&(ret->mutex), NULL);
	return ret;
}

filebuffer_t* filebuffer__copy (filebuffer_t* source) {
	filebuffer_t* ret = filebuffer__new();
	if (source->buffersize <= 0) return ret;
	filebuffer__truncate(ret, source->buffersize);
	memcpy(ret->buffer, source->buffer, source->buffersize);
	ret->contentsize = source->contentsize;
	return ret;
}

void filebuffer__destroy (filebuffer_t* body) {
	if (body == NULL) {
		debug_printf ("destroying filebuffer impossible, already NULL\n");
		return;
	}
	if (body->buffer != NULL) free (body->buffer);
	free (body);
}

size_t filebuffer__read (filebuffer_t* self, off_t offset, char * target, size_t maxlength) {
	debug_printf ("filebuffer__read:  reading %d bytes from %p with offset %" PRId64 " to %p\n", maxlength, self, offset, target);
	if (self == NULL) {
		error_printf ("writing buffer failed: NULL pointer!\n");
		return -EACCES;
	}
	if (self->buffersize < self->contentsize) {
		error_printf ("buffer %p corrupted: self->buffersize < self->contentsize!\n", self);
		return -EIO;
	}
	if (offset >= self->contentsize) {
		debug_printf ("trying to read after EOF of buffer\n");
		return 0;
	}
	size_t wanted = maxlength;
	if (offset + wanted > self->contentsize) {
		debug_printf("shortening read to EOF\n");
		wanted = self->contentsize - offset;
	}
	pthread_mutex_lock(&(self->mutex));
	char *source = self->buffer;
	memcpy (target, source + offset, wanted);
	pthread_mutex_unlock(&(self->mutex));
	return wanted;
}

static size_t filebuffer__truncate__unlocked(filebuffer_t* self, size_t new_size) {
	debug_printf ("filebuffer__truncate:  truncating filebuffer %p to size %d\n", self, new_size);
	if (self == NULL) {
		error_printf ("truncating buffer failed: NULL pointer!\n");
		return -EACCES;
	}
	if (self->buffer == NULL && new_size > 0) { // need new buffer
		debug_printf ("filebuffer__write: creating new buffer for %p\n", self);
		self->buffer = (char *) malloc (new_size);
		CHECK_OOM (self->buffer);
		self->buffersize = new_size;
		self->contentsize = new_size;
		memset (self->buffer, 0, new_size);
		return new_size;
	}
	// buffer exists, check need for growth
	if (new_size > self->buffersize) { // grow buffer
		char *old = self->buffer;
		size_t old_size  = self->buffersize;
		self->buffer = (char *) malloc (new_size);
		if (new_size > 0) CHECK_OOM(self->buffer);
		self->buffersize = new_size;
		self->contentsize = new_size;
		memcpy (self->buffer, old, old_size);
		free (old);
	} else {
		// just shrink
		self->contentsize = new_size;
	}
	assert (self->buffersize >= new_size);
	return new_size;
}

size_t filebuffer__truncate(filebuffer_t* self, size_t new_size) {
	pthread_mutex_lock(&(self->mutex));
	size_t ret = filebuffer__truncate__unlocked(self, new_size);
	pthread_mutex_unlock(&(self->mutex));
	return ret;
}

size_t filebuffer__contentsize (filebuffer_t* self) {
	if (self == NULL) {
		error_printf ("reading content size failed: NULL pointer!\n");
		return -EACCES;
	}
	return self->contentsize;
}

char* filebuffer__read_all_to_cstring (filebuffer_t* self) {
	debug_printf("%s on %p\n", __FUNCTION__, self);
	if (self == NULL) {
		error_printf ("reading complete buffer failed: NULL pointer!\n");
		return NULL;
	}
	pthread_mutex_lock(&(self->mutex));
	char* ret = (char*) malloc(self->contentsize + 1);
	CHECK_OOM(ret);
	memcpy(ret, self->buffer, self->contentsize);
	ret[self->contentsize] = 0;
	pthread_mutex_unlock(&(self->mutex));
	return ret;
}

