#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <dirent.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "fuse-ts.h"
#include "fuse-ts-debug.h"
#include "fuse-ts-tools.h"
#include "fuse-ts-opts.h"


char * opts_path = "/cmdlineopts";

static char * opts = NULL;
static size_t opts_len = 0;

static char * opts_tail = NULL;

void print_usage() {
	fprintf(stdout, "\nUsage:\n fuse-ts <option1> <option2> ... <mountpoint> \n\nwhere most options must be given.\n");
	fprintf(stdout, "\nIMPORTANT: for proper support of KDEnlive, the mountpoint");
	fprintf(stdout, "\nMUST be the LAST argument given!\n");
	fprintf(stdout, "\nOptions:\n");
	fprintf(stdout, "\t\tc=/foo/bar\tbase dir of capture files\n");
	fprintf(stdout, "\t\tp=xxxx\tfile _name_ prefix used for filtering input files first (optional)\n");
	fprintf(stdout, "\t\tst=xxxx-xx-xx_xx[-xx[-xx]]\tcapture start date and time\n");
	fprintf(stdout, "\t\tob=[1..2^63]\tnumber of bytes of resulting file\n");
	fprintf(stdout, "\t\tnumfiles=[1..2^31]\tmaximum number of input files to consider\n");
	fprintf(stdout, "\t\ttotalframes=[1..2^31]\ttotal number of frames expected to be in resulting output file\n");
	fprintf(stdout, "\t\tif=[1..2^32]\tnumber of inframe for cut (optional)\n");
	fprintf(stdout, "\t\tof=[1..2^32]\tnumber of outframe for cut (relative to start, optional)\n");
	fprintf(stdout, "\t\tintro=/foo/bar/x.dv\tintro file (optional, relative to capture dir)\n");
	fprintf(stdout, "\t\toutro=/foo/bar/x.dv\toutro file (optional, relative to capture dir)\n");
	fprintf(stdout, "\t\tslides\t\tslidemode, interpret filenames as timestamps (optional)\n");
	fprintf(stdout, "\t\twinpath\t\tpath prefix for win32 shotcut file, e.g. UNC path oder mapped drive (optional)\n");
	fprintf(stdout, "\t\tstripslashes\t\tstrip mountpoint by this number of slashes to get win32 path (optional)\n");
	fprintf(stdout, "\n\n");
	fprintf(stdout, "If both parameters ob and numfiles are given, fuse-TS stops when the first \n");
	fprintf(stdout, "of these two conditions is met\n");
	fprintf(stdout, "\n\n");
	fprintf(stdout, "\nDo not forget to use the FUSE options '-oallow_other,use_ino' and '-s'!\n");
	fprintf(stdout, "\n\n");
}

void parse_opts(int * p_argc, char*** p_argv) {

	int argc = *p_argc;
	char ** argv = *p_argv;
// temporary parameter variables
	int argc_new = 0;
	char * argv_new[argc];
//	int outbyte = -1;

	argv_new[argc_new++] = argv[0];

// default values
	slidemode = 0;
	inframe = 0;
	outframe = -1;

	int i;
// parsing options
	for (i = 1; i < argc; i++) {
		char * opt = argv[i];
#ifdef DEBUG
		printf("examining option '%s' with length %zu\n", opt, strlen(opt));
#endif

		if (strncmp(opt, "p=", 2) == 0) {
			prefix = dupe_str(opt + 2);
			continue;
		}
		if (strncmp(opt, "slides", 6) == 0) {
			slidemode = 1;
			continue;
		}
		if (strncmp(opt, "outro=", 6) == 0) {
			if (strlen(opt) < 7) {
				continue;
			}
			outro_file = dupe_str(opt + 6);
			continue;
		}
		if (strncmp(opt, "intro=", 6) == 0) {
			if (strlen(opt) < 7) {
				continue;
			}
			intro_file = dupe_str(opt + 6);
			continue;
		}
		if (strncmp(opt, "c=", 2) == 0) {
			if (strlen(opt) < 3) {
				fprintf(logging, "Error: base_dir must not be empty!\n");
				exit(106);
			}
			base_dir = dupe_str(opt + 2);
			continue;
		}
		if (strncmp(opt, "st=", 3) == 0) {
			if (strlen(opt) < 4) {
				fprintf(logging, "Error: capture file name prefix is too short!\n");
				exit(105);
			}
			start_time = dupe_str(opt + 3);
			continue;
		}
		if (strncmp(opt, "ob=", 3) == 0) {
			char * suffix = NULL;
			outbyte = strtoull(opt + 3, &suffix, 10);
			if (suffix != NULL && *suffix > 32) { // is there a character after the number?
				switch(*suffix) {
				// no breaks!
				case 'G':
				case 'g':
					outbyte *= 1024;
				case 'M':
				case 'm':
					outbyte *= 1024;
				case 'K':
				case 'k':
					outbyte *= 1024;
				}
			}
			if ((outbyte < 0) || (outbyte > ((off_t)1024 * 1024 * 1024 * 1024 * 1024) /* 1 TiB */)) {
				fprintf(logging, "Error: outbyte is negative or too big (%" PRId64 ")!\n", outbyte);
				exit(102);
			}
			continue;
		}
		if (strncmp(opt, "numfiles=", 9) == 0) {
			numfiles = atoi(opt + 9);
			if ((numfiles < 0) || (numfiles > 100000)) {
				fprintf(logging, "Error: numfiles is negative or too big (%d)!\n", numfiles);
				exit(103);
			}
			continue;
		}
		if (strncmp(opt, "totalframes=", 12) == 0) {
			totalframes = atoi(opt + 12);
			if ((totalframes < 0) || (totalframes > 1000000)) {
				fprintf(logging, "Error: totalframes is negative or too big (%d)!\n", totalframes);
				exit(103);
			}
			continue;
		}
		if (strncmp(opt, "if=", 3) == 0) {
			inframe = atoi(opt + 3);
			if ((inframe < 0) || (inframe > 1080000 /* 12 hours */)) {
				fprintf(logging, "Error: inframe is too big (%d)!\n", inframe);
				exit(104);
			}
			continue;
		}
		if (strncmp(opt, "of=", 3) == 0) {
			outframe = atoi(opt + 3);
			if ((outframe < 0) || (outframe > 1080000 /* 12 hours */)) {
				fprintf(logging, "Error: outframe is too big (%d)!\n", outframe);
				exit(103);
			}
			continue;
		}
		if (strncmp(opt, "winpath=", 8) == 0) {
			if (strlen(opt) > 8) {
				// accept empty option, since it's optional
				winpath = dupe_str(opt + 8);
			}
			continue;
		}
		if (strncmp(opt, "stripslashes=", 13) == 0) {
			winpath_stripslashes = atoi(opt + 13);
			if ((winpath_stripslashes < 1) || (winpath_stripslashes > 20)) {
				fprintf(logging, "Warning: stripslashes out of bounds: %d! Ignoring value\n", winpath_stripslashes);
				winpath_stripslashes = 0;
			}
			continue;
		}

		argv_new[argc_new++] = opt;
	}

	// validation of parameters
	if (prefix == NULL) {
		prefix = dupe_str("");
	}
	if (base_dir == NULL) {
		fprintf(logging, "base_dir must be given!\n");
		exit(100);
	}
	if (start_time == NULL) {
		fprintf(logging, "capture filename prefix must be given!\n");
		exit(100);
	}
	if (outbyte < 0 && numfiles < 0) {
		fprintf(logging, "outbyte or numfiles must be given!\n");
		exit(100);
	}
	if (slidemode != 0 && outro_file != NULL) {
		fprintf(logging, "WARNING: ignoring outro in slidemode.\n");
		free(outro_file);
		outro_file = NULL;
	}
	if (slidemode != 0 && intro_file != NULL) {
		fprintf(logging, "WARNING: ignoring intro in slidemode.\n");
		free(intro_file);
		intro_file = NULL;
	}

	if (inframe < 0) {
		inframe = 0;
	}
	if (outframe < 0 && totalframes > 0) {
		outframe = totalframes;
	}

#ifdef DEBUG
	print_parsed_opts(argc_new);
#endif

	char * tmp = malloc(1024);
	char * tmp2 = malloc(1024);

	for (i = 0; i < argc_new; i++) {
		(*p_argv)[i] = argv_new[i];
		if (i == 0) continue;
		snprintf(tmp2, 1024, "%s %s", tmp, argv_new[i]);
		memcpy (tmp, tmp2, 1024);
		tmp[1023] = 0;
	}
	opts_tail = tmp;
	free(tmp2);

	*p_argc = argc_new;
	mountpoint = dupe_str(argv_new[argc_new - 1]); 
	rebuild_opts();	
}

void rebuild_opts() {
	char * t;
	if (opts != NULL) {
		t = opts;
		opts_len = 0;
		opts = NULL;
		free(t);
	}
	size_t s = 1024; // lazy here.. 1k should really be enough ;)
	t = (char*) malloc(s);

// first the mandatory opts
	snprintf(t, s-1, " c=%s st=%s ob=%" PRId64, base_dir, start_time, outbyte);

// now the optional ones
	char * ret = dupe_str(t);
	if (numfiles > 0) {
		snprintf(t, s-1, " numfiles=%d ", numfiles);
		ret = append_and_free(ret, dupe_str(t));
	}

	if (totalframes > 0) {
		snprintf(t, s-1, " totalframes=%d ", totalframes);
		ret = append_and_free(ret, dupe_str(t));
	}

	if (inframe > 0) {
		snprintf(t, s-1, " if=%d ", inframe);
		ret = append_and_free(ret, dupe_str(t));
	}

	if (outframe > 0) {
		snprintf(t, s-1, " of=%d ", outframe);
		ret = append_and_free(ret, dupe_str(t));
	}

	if (prefix != NULL && strlen(prefix) > 0) {
		snprintf(t, s-1, " p=%s ", prefix);
		ret = append_and_free(ret, dupe_str(t));
	}
	if (intro_file != NULL) {
		snprintf(t, s-1, " intro=%s ", intro_file);
		ret = append_and_free(ret, dupe_str(t));
	}
	if (outro_file != NULL) {
		snprintf(t, s-1, " outro=%s ", outro_file);
		ret = append_and_free(ret, dupe_str(t));
	}
	if (winpath != NULL) {
		snprintf(t, s-1, " winpath=%s ", winpath);
		ret = append_and_free(ret, dupe_str(t));
	}
	if (winpath_stripslashes > 0) {
		snprintf(t, s-1, " stripslashes=%d ", winpath_stripslashes);
		ret = append_and_free(ret, dupe_str(t));
	}
	if (slidemode != 0) {
		snprintf(t, s-1, " slides ");
		ret = append_and_free(ret, dupe_str(t));
	}
	opts = malloc(s);
	opts[s-1] = 0;
	snprintf(opts, s - 1, "fuse-ts %s %s \n", ret, opts_tail);
	opts_len = strlen(opts);
	free(t);
	free(ret);
}

size_t opts_length() {
	return opts_len;
}

char * get_opts() {
	return dupe_str(opts);
}

