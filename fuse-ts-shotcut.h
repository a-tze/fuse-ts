extern size_t get_shotcut_project_file_size (const char * filename, int num_frames, int blanklen);
extern void init_shotcut_project_file ();
extern size_t shotcut_read(const char *path, char *buf, size_t size, off_t offset, const char* movie_path, int frames, int blanklen);
extern void open_shotcut_project_file (const char* movie_path, int frames, int blanklen, int truncate);
extern void truncate_shotcut_project_file();
extern size_t write_shotcut_project_file (const char * buffer, size_t size, off_t offset);
extern void close_shotcut_project_file ();
extern int find_cutmarks_in_shotcut_project_file(int* inframe, int* outframe, int* blanklen);

extern const char * shotcut_path;
