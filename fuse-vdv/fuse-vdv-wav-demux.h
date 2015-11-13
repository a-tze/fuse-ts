#ifndef FUSE_VDV_WAV_DEMUX_H
#define FUSE_VDV_WAV_DEMUX_H

#include "fuse-vdv.h"

// Offset between channel 0 and channel 1 inside a single frame is exactly:

#define CHANNEL_1_OFFSET 72000

extern const unsigned int pcm_pal_16bit_channel0_offsets[];
extern const char * wav_filepath;
extern off_t wav_filesize;

extern int vdv_wav_read (sourcefile_t * list, char *buf, size_t size, off_t offset, int depth);
extern void update_wav_filesize (off_t cut_filesize);

#endif
