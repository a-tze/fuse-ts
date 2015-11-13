#ifndef _FUSE_KNOWLEDGE_H
#define _FUSE_KNOWLEDGE_H

extern int get_index_from_pathname(const char* path);


#define 	INDEX_ROOTDIR 		0	// Index for '/'
#define 	INDEX_RAW 		1	// Index for '/uncut.dv'
#define 	INDEX_CUT 		2	// Index for '/cut.dv'
#define 	INDEX_CUTCOMPLETE	3	// Index for '/cutcomplete.dv'
#define 	INDEX_WAV 		4	// Index for '/cut-demux.wav'
#define 	INDEX_PID 		5	// Index for '/pid'
#define 	INDEX_OPTS 		6	// Index for '/cmdlineopts'
#define 	INDEX_INFRAME 		7	// Index for '/inframe'
#define 	INDEX_OUTFRAME 		8	// Index for '/outframe'
#define 	INDEX_KDENLIVE 		10	// Index for '/project.kdenlive'
#define 	INDEX_REBUILD 		11	// Index for '/rebuild'
#define		INDEX_SHOTCUT		12  //index for '/project_shotcut.mlt'


#endif
