DEBUGopts = -g -O0 -fno-inline-functions -DDEBUG
NDEBUGopts = -O2 -DNDEBUG
CFLAGS = -Wall -c $(DEBUG) -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25
LFLAGS = -Wall -lmxml -lfuse $(DEBUG)
CC = gcc
DEBUG=$(NDEBUGopts)

OBJS = fuse-vdv-debug.o fuse-vdv-tools.o fuse-vdv-opts.o fuse-vdv-filelist.o \
       fuse-vdv-kdenlive.o fuse-vdv-smoothsort.o fuse-vdv-wav-demux.o \
       fuse-vdv-knowledge.o fuse-vdv-shotcut.o fuse-vdv-filebuffer.o

TOBJS = fuse-vdv-debug.o fuse-vdv-tools.o fuse-vdv-opts.o fuse-vdv-filelist.o \
       fuse-vdv-kdenlive.o fuse-vdv-smoothsort.o fuse-vdv-filebuffer.o

all: fuse-vdv

debug: DEBUG=$(DEBUGopts)
debug: fuse-vdv

$(OBJS) fuse-vdv.o fuse-vdv-test.o: %.o: %.c *.h
	$(CC) $(CFLAGS) $< -o $@

fuse-vdv: $(OBJS) fuse-vdv.o
	$(CC) $(OBJS) fuse-vdv.o $(LFLAGS) -o fuse-vdv

test: DEBUG=$(DEBUGopts)
test: clean $(TOBJS) fuse-vdv-test.o
	$(CC) $(TOBJS) $(DEBUGopts) fuse-vdv-test.o $(LFLAGS) -o fuse-vdv-test
	./fuse-vdv-test

clean:
	rm -f *.o fuse-vdv fuse-vdv-test

dist-clean:
	rm -f config.h
