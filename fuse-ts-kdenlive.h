extern size_t get_kdenlive_project_file_size (const char * filename, int num_frames, int blanklen);
extern void init_kdenlive_project_file ();
extern size_t kdenlive_read(const char *path, char *buf, size_t size, off_t offset, const char* movie_path, int frames, int blanklen);
extern void open_kdenlive_project_file (const char* movie_path, int frames, int blanklen, int truncate);
extern void truncate_kdenlive_project_file();
extern size_t write_kdenlive_project_file (const char * buffer, size_t size, off_t offset);
extern void close_kdenlive_project_file ();
extern int find_cutmarks_in_kdenlive_project_file(int* inframe, int* outframe, int* blanklen);
extern char * kdenlive_path;
