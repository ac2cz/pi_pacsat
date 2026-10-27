/*
 * pacsat_preload.c
 *
 *  Created on: Oct 24, 2026
 *      Author: g0kla
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>

#include <common_config.h>
#include <str_util.h>
#include "config.h"
#include "pacsat_dir.h"
#include "pacsat_header.h"

bool more_help = false;
char data_folder_path[MAX_FILE_PATH_LEN] = ".";
char config_file_name[MAX_FILE_PATH_LEN] = "pi_pacsat.config";
char file_name[MAX_FILE_PATH_LEN] = "";
int file_type = -1;
bool compress_if_needed = false;
char destination[MAX_FILE_PATH_LEN] = "ALL";
char install_folder[MAX_FILE_PATH_LEN] = "";
char signature_file[MAX_FILE_PATH_LEN] = "";

/**
 * Print this help if the -h or --help command line options are used
 */
void help(void) {
	printf(
			"Usage: pacsat_preload -f <file> -t <type> [OPTION] \n"
			"-f,--file                        the file to load\n"
			"-t,--type                        the file type as an integer\n"
			"-D,--dest                        the destination e.g. WOD. Defaults to ALL\n"
			"-F,--folder                      the folder that this is installed in (but no copy happens)\n"
			"-S,--sig                         the signature file to populate the header and sign the file\n"
			"-c,--config                      use config file specified, otherwise it looks in current dir\n"
			"-d,--dir                         use this data directory, rather than current dir\n"
			"-z,--zip                        compress the file with pkzip if large enough\n"
			"-h,--help                        help\n"
			"-v,--verbose                     print additional status and progress messages\n"
			"If the file needs to be compressed then a compressed version is written in the current dir\n"
	);
	exit(EXIT_SUCCESS);
}


/* Read exactly len bytes from path into sig. A shorter or longer file is an error. */
int load_signature(const char *path, uint8_t *sig, size_t len) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		error_print("Can't open signature file %s: %s\n", path, strerror(errno));
		return EXIT_FAILURE;
	}
	size_t n = fread(sig, 1, len, f);
	int extra = fgetc(f);   /* should be EOF if the file was exactly len bytes */
	fclose(f);

	if (n != len) {
		error_print("Signature file %s is %zu bytes, expected %zu\n", path, n, len);
		return EXIT_FAILURE;
	}
	if (extra != EOF) {
		error_print("Signature file %s is longer than %zu bytes\n", path, len);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {

	struct option long_option[] = {
			{"help", no_argument, NULL, 'h'},
			{"file", required_argument, NULL, 'f'},
			{"type", required_argument, NULL, 't'},
			{"dest", required_argument, NULL, 'D'},
			{"folder", required_argument, NULL, 'F'},
			{"sig", required_argument, NULL, 'S'},
			{"dir", required_argument, NULL, 'd'},
			{"config", required_argument, NULL, 'c'},
			{"zip", no_argument, NULL, 'z'},
			{"verbose", no_argument, NULL, 'v'},
			{NULL, 0, NULL, 0},
	};

	strlcpy(config_file_name, "pacsat.config", sizeof(config_file_name));

	while (1) {
		int c;
		if ((c = getopt_long(argc, argv, "hf:t:D:F:S:d:c:zv", long_option, NULL)) < 0)
			break;
		switch (c) {
		case 'h': // help
			more_help = true;
			break;
		case 'v': // verbose
			g_verbose = true;
			break;
		case 'z': // zip
			compress_if_needed= true;
			break;
		case 'c': // config file name
			strlcpy(config_file_name, optarg, sizeof(config_file_name));
			break;
		case 'f': // file
			strlcpy(file_name, optarg, sizeof(file_name));
			break;
		case 't': // type
			if (!parse_int(optarg, 0, 255, &file_type)) {
				debug_print("ERROR: Invalid file tupe: %s\n",optarg);
				exit(1);
			}
			break;
		case 'D': // dest
			strlcpy(destination, optarg, sizeof(destination));
			break;
		case 'F': // folder
			strlcpy(install_folder, optarg, sizeof(install_folder));
			break;
		case 'S': // dest
			strlcpy(signature_file, optarg, sizeof(signature_file));
			break;
		case 'd': // data folder
			strlcpy(data_folder_path, optarg, sizeof(data_folder_path));
			break;
		}
	}

	if (more_help) {
		help();
		return 0;
	}

	bool ok = true;
	if (file_name[0] == '\0') { error_print("-f <file> is required\n"); ok = false; }
	if (file_type < 0)        { error_print("-t <type> is required\n"); ok = false; }
	if (!ok) {
		help();
		return EXIT_FAILURE;
	}

	uint8_t signature[64];
	bool have_signature = false;
	if (signature_file[0] != '\0') {
		if (load_signature(signature_file, signature, sizeof(signature)) != EXIT_SUCCESS)
			exit(EXIT_FAILURE);
		have_signature = true;
	}

	printf("Preload Pacsat File\n");
	load_config(config_file_name);

	/* Initialize the directory */
	if (dir_init(data_folder_path) != EXIT_SUCCESS) { error_print("** Could not initialize the dir\n"); return EXIT_FAILURE; }
	dir_load();

	uint32_t now = time(0);
	char user_file_name[MAX_FILE_PATH_LEN] = "";
	char psf_name[MAX_FILE_PATH_LEN] = "";

	uint32_t id = dir_next_file_number();

	const char *base = strrchr(file_name, '/');   /* last '/' in the path, or NULL */
	if (base != NULL)
		strlcpy(user_file_name, base + 1, sizeof(user_file_name));   /* text after the '/' */
	else
		strlcpy(user_file_name, file_name, sizeof(user_file_name));  /* no path, use as is */

	char compression_type = BODY_NOT_COMPRESSED;
	/* Stat the file before we add the header to capture the create date */
	struct stat st;
	time_t create_time = now;
	if (stat(file_name, &st) == EXIT_SUCCESS) {
		create_time = st.st_mtim.tv_sec; /* We use the time of last modification as the create time.  */
		if (compress_if_needed) {
			if (st.st_size > UNCOMPRESSED_FILE_SIZE_LIMIT) {
				/* Compress if more than 200 bytes used */
				char zip_command[MAX_FILE_PATH_LEN];
				char compressed_file_name[MAX_FILE_PATH_LEN];

				strlcpy(compressed_file_name, file_name, sizeof(compressed_file_name));
				strlcat(compressed_file_name,".zip", sizeof(compressed_file_name));

				strlcpy(zip_command, "/usr/bin/zip -j -q ",sizeof(zip_command)); // -j to "junk" or not store the paths.  -q quiet
				strlcat(zip_command, compressed_file_name,sizeof(zip_command));
				strlcat(zip_command, " ",sizeof(zip_command));
				strlcat(zip_command, file_name,sizeof(zip_command));
				int zip_rc = system(zip_command);
				if (zip_rc == EXIT_SUCCESS) {
					/* We could compress it */
					strlcat(user_file_name,".zip", sizeof(user_file_name));
					compression_type = BODY_COMPRESSED_PKZIP;
					strlcpy(file_name, compressed_file_name, sizeof(file_name));
				}
			}
		}
	}
	HEADER *pfh = pfh_make_internal_header(now, file_type, id, "", g_bbs_callsign, destination, file_name, user_file_name,
			create_time, 0, compression_type);

	if (install_folder[0] != '\0') {
		//debug_print("Adding folder: %s\n",install_folder);
		pfh_add_keyword(pfh, install_folder);
		pfh->expireTime = 2145848400; // 2038-01-01
	}

	if (have_signature) {
		memcpy(pfh->signature, signature, sizeof(pfh->signature));
		pfh->signature_type = PFH_SIG_TYPE_ED25519;
	}

	dir_get_file_path_from_file_id(pfh->fileId, get_dir_folder(), psf_name, MAX_FILE_PATH_LEN);
	if (g_verbose) { debug_print("Adding file: %s\n",file_name);	pfh_debug_print(pfh); }
	dir_get_file_path_from_file_id(id, get_dir_folder(), psf_name, sizeof(psf_name));

	int rc;
	rc = pfh_make_internal_file(pfh, get_dir_folder(), file_name);
	if (rc != EXIT_SUCCESS) {
		printf("** Failed to make pacsat file %s\n", file_name);
		remove(psf_name); // remove this in case it was partially written, ignore any error
		if (pfh != NULL)
			free(pfh);
	}
	return rc;
}
