DEBUGopts = -g -O0 -fno-inline-functions -DDEBUG
NDEBUGopts = $(EXTRA_CFLAGS) -O2 -DNDEBUG
CFLAGS = -Wall -c $(DEBUG) -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25
LFLAGS = -Wall -lmxml -lfuse $(DEBUG)
CC = gcc
DEBUG=$(NDEBUGopts)

OBJS = fuse-ts-debug.o fuse-ts-tools.o fuse-ts-opts.o fuse-ts-filelist.o \
       fuse-ts-kdenlive.o fuse-ts-smoothsort.o fuse-ts-knowledge.o \
       fuse-ts-shotcut.o fuse-ts-filebuffer.o

all: fuse-ts

debug: DEBUG=$(DEBUGopts)
debug: fuse-ts

$(OBJS) fuse-ts.o fuse-vdv-test.o: %.o: %.c *.h
	$(CC) $(CFLAGS) $< -o $@

fuse-ts: $(OBJS) fuse-ts.o
	$(CC) $(OBJS) fuse-ts.o $(LFLAGS) -o fuse-ts

deb:
	dpkg-buildpackage -us -uc

clean:
	rm -f *.o fuse-ts fuse-ts-test

.PHONY: clean deb
