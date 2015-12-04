#ifndef _FUSE_KNOWLEDGE_H
#define _FUSE_KNOWLEDGE_H

extern int get_index_from_pathname(const char* path);


#define 	INDEX_ROOTDIR 		0	// Index for '/'
#define 	INDEX_RAW 		1	// Index for '/uncut.ts'
#define 	INDEX_PID 		2	// Index for '/pid'
#define 	INDEX_OPTS 		3	// Index for '/cmdlineopts'
#define 	INDEX_INTIME 		4	// Index for '/intime'
#define 	INDEX_OUTTIME 		5	// Index for '/outtime'
#define 	INDEX_KDENLIVE 		6	// Index for '/project.kdenlive'
#define 	INDEX_REBUILD 		7	// Index for '/rebuild'
#define 	INDEX_INFRAME 		8	// Index for '/inframe'
#define 	INDEX_OUTFRAME 		9	// Index for '/outframe'
#define 	INDEX_DURATION 		10	// Index for '/duration'
#define 	INDEX_SHOTCUT  		12	// Index for '/project_shotcut.mlt'
#define 	INDEX_SHOTCUT_WIN	13	// Index for '/project_shotcut_win.mlt'


#endif
