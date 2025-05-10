MXML_VERSION := \
  $(shell \
  if `pkg-config --exists mxml4` ; then echo '4'; \
  else echo ''; \
  fi \
  )
MXML_CFLAGS := $(shell pkg-config --cflags mxml$(MXML_VERSION))
MXML_LIBS := $(shell pkg-config --libs mxml$(MXML_VERSION))
DEBUGopts = -g -O0 -fno-inline-functions -DDEBUG
NDEBUGopts = $(EXTRA_CFLAGS) -O2 -DNDEBUG
CFLAGS = -Wall -g -Wpedantic -c $(DEBUG) -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 $(MXML_CFLAGS)
LFLAGS = -Wall -g -Wpedantic -lfuse $(DEBUG) $(EXTRA_LFLAGS) $(MXML_LIBS)
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
