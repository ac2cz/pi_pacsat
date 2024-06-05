/*
 * pacsat_dir.c
 *
 *  Created on: Oct 7, 2022
 *      Author: g0kla
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 * ======================================================================
 *
 * On the spacecraft there is a directory that holds the files which are visible
 * to users.  Each file has an 32 bit id number.  On disk every file has a pacsat
 * header followed by its contents.
 *
 * Files that are in the process of being uploaded are handled by FLT0 and are not visible
 * in the directory.
 *
 * The directory is cached in memory.  When a file is added either by a system process or
 * because a file is uploaded then it is added to the list in memory.  When the directory is
 * large it takes too long to scan the files on disk and parse the headers.
 *
 * The directory is sorted by upload_time and each directory entry has: an "older limit" ,
 * t_old,  and a "newer limit", t_new. This is so that each broadcast directory entry gives
 * the client on the ground a pair numbers (t_old, t_new). The proper interpretation of this
 * pair is:
 *
 *          "There   are  no  files  other  than  this  file   with
 *          UPLOAD_TIME >= t_old and <= t_new."
 *
 * The creation of the packets that are sent to the ground is handled by the pacsat
 * broadcast (pb) module.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <assert.h>

#include <fcntl.h>
#include <errno.h>

#include <iors_command.h>

/* Program include files */
#include "config.h"
#include "state_file.h"
#include "pacsat_dir.h"
#include "pacsat_header.h"
#include "pacsat_broadcast.h"
#include "ftl0.h"
#include "str_util.h"
#include "debug.h"

/* Forward declarations */
void dir_free();
void dir_delete_node(DIR_NODE *node);
void dir_debug_print(DIR_NODE *p);
int dir_load_pacsat_file(char *psf_name);
int dir_fs_update_header(char *file_name_with_path, HEADER *pfh);

/* Dir variables */
static DIR_NODE *dir_head = NULL;  // the head of the directory linked list
static DIR_NODE *dir_tail = NULL;  // the tail of the directory linked list
DIR_NODE *dir_maint_node = NULL;   // the node where we are performing directory maintenance
static char data_folder[MAX_FILE_PATH_LEN]; // Directory path of the data folder
static char dir_folder[MAX_FILE_PATH_LEN]; // Directory path of the directory folder
static char wod_folder[MAX_FILE_PATH_LEN]; // Directory path of the wod telemetry folder
static char log_folder[MAX_FILE_PATH_LEN]; // Directory path of the log folder
static char upload_folder[MAX_FILE_PATH_LEN]; // Directory path of the upload folder
//static uint32_t next_file_id = 0; // This is incremented when we add files for upload.  Initialized when dir loaded.
unsigned char pfh_byte_buffer[MAX_PFH_LENGTH]; // needs to be bigger than largest header but does not need to be the whole file


int dir_make_dir(char * folder) {
	struct stat st = {0};
	if (stat(folder, &st) == -1) {
		if (mkdir(folder, 0700) == -1) {
			printf("** Could not make pacsat folder %s\n",folder);
			return EXIT_FAILURE;
		}
		debug_print("Created: %s\n", folder);
	}
	return EXIT_SUCCESS;
}

/**
 * dir_init()
 *
 * Initialize the directory by setting the dir_folder variable and creating the
 * directory if needed.  Note that this will only create the last part of the
 * path.  So the directory must be placed in a folder that already exists.
 *
 */
int dir_init(char *folder) {
	strlcpy(data_folder, folder, sizeof(data_folder));
	if (dir_make_dir(data_folder) != EXIT_SUCCESS) return EXIT_FAILURE;

	strlcpy(dir_folder, data_folder, sizeof(dir_folder));
	strlcat(dir_folder, "/", sizeof(dir_folder));
	strlcat(dir_folder, get_folder_str(FolderDir), sizeof(dir_folder));
	if (dir_make_dir(dir_folder) != EXIT_SUCCESS) return EXIT_FAILURE;

	strlcpy(wod_folder, data_folder, sizeof(wod_folder));
	strlcat(wod_folder, "/", sizeof(wod_folder));
	strlcat(wod_folder, get_folder_str(FolderWod), sizeof(wod_folder));
	if (dir_make_dir(wod_folder) != EXIT_SUCCESS) return EXIT_FAILURE;

	strlcpy(log_folder, data_folder, sizeof(log_folder));
	strlcat(log_folder, "/", sizeof(log_folder));
	strlcat(log_folder, get_folder_str(FolderLog), sizeof(log_folder));
	if (dir_make_dir(log_folder) != EXIT_SUCCESS) return EXIT_FAILURE;

	strlcpy(upload_folder, data_folder, sizeof(upload_folder));
	strlcat(upload_folder, "/", sizeof(upload_folder));
	strlcat(upload_folder, get_folder_str(FolderUpload), sizeof(upload_folder));
	if (dir_make_dir(upload_folder) != EXIT_SUCCESS) return EXIT_FAILURE;

	debug_print("Pacsat Initialized in: %s\n", data_folder);
	return EXIT_SUCCESS;
}

void dir_get_upload_file_path_from_file_id(int file_id, char *filename, int max_len) {
	char file_id_str[5];
	snprintf(file_id_str, 5, "%04x",file_id);
	strlcpy(filename, upload_folder, MAX_FILE_PATH_LEN);
	strlcat(filename, "/", MAX_FILE_PATH_LEN);
	strlcat(filename, file_id_str, MAX_FILE_PATH_LEN);
	strlcat(filename, ".", MAX_FILE_PATH_LEN);
	strlcat(filename, "upload", MAX_FILE_PATH_LEN);
}

///**
// * Given a file name, create the file name string with full path
// */
//void dir_get_file_path_from_file_name(uint32_t file_id, char *file_path, int max_len) {
//    char file_id_str[5];
//    snprintf(file_id_str, 5, "%04x",file_id);
//    strlcpy(file_path, DIR_FOLDER, max_len);
//    strlcat(file_path, file_id_str, max_len);
////}

/**
 * pfh_make_filename()
 *
 * Make a default filename for a PACSAT file based on its file id
 *
 */
void dir_get_file_path_from_file_id(int file_id, char *dir_name, char *filename, int max_len) {
	char file_id_str[5];
	snprintf(file_id_str, 5, "%04x",file_id);
	strlcpy(filename, dir_name, MAX_FILE_PATH_LEN);
	strlcat(filename, "/", MAX_FILE_PATH_LEN);
	strlcat(filename, file_id_str, MAX_FILE_PATH_LEN);
	strlcat(filename, PSF_FILE_EXT, MAX_FILE_PATH_LEN);
}

void dir_get_filename_from_file_id(uint32_t file_id, char *file_name, int max_len) {
    snprintf(file_name, max_len, "%04x",file_id);
}

uint32_t dir_get_file_id_from_filename(char *file_name) {
    char file_id_str[5];
    strlcpy(file_id_str,file_name,sizeof(file_id_str)); // copy first 4 chars
    int ret = strlen(file_id_str);
    if (ret != 4) return 0;
    uint32_t id = (uint32_t)strtol(file_id_str, NULL, 16);
    return id;
}

/**
 * dir_next_file_number()
 *
 * This returns the next file number available for the upload process.
 * TODO - this will not cope well with failed uploads.  Those ids will be lost and
 * never used.  We are supposed to "reserve" the file number when a DATA command is
 * received, but we need to allocate it before that.
 *
 */
int dir_next_file_number() {
	g_dir_next_file_number++;
	save_state();
	return g_dir_next_file_number;
}

char *get_data_folder() {
	return data_folder; // We can return this because it is static
}

char *get_dir_folder() {
	return dir_folder; // We can return this because it is static
}

char *get_upload_folder() {
	return upload_folder; // We can return this because it is static
}

char *get_wod_folder() {
	return wod_folder; // We can return this because it is static
}

char *get_log_folder() {
	return log_folder; // We can return this because it is static
}

/**
 * insert_after()
 * Insert new_node after node p in the linked list.
 * Handle the situation where p is the tail of the list
 * p may not be NULL
 *
 */
void insert_after(DIR_NODE *p, DIR_NODE *new_node) {
	assert(p != NULL);
	new_node->next = p->next; // which may be null if at end of list
	new_node->prev = p;
	if (p->next == NULL) // we are at the end of the list
		dir_tail = new_node;
	else
		p->next->prev = new_node;
	p->next = new_node;
}

/**
 * dir_add_pfh()
 * Add a pacsat file header to the directory and return a pointer
 * to the inserted node in the linked list.
 * This handles the situation where the list is empty and creates the first item.
 * In an existing list if this item does not have an upload time then it is new
 * and it is inserted at the end of the list with the current time.  If many items
 * are added at the same time then it is given an upload time 1 second after the
 * last item.
 * If this header already has an upload_time then we search backwards in the list
 * to find the insertion point.
 * If we find a header with the same upload_time then this must be a duplicate and it
 * is discarded.
 * To update an item correctly, remove it from the list, set the upload_time to zero and then call this routine to insert it at the end.
 *
 * If the upload_time was modified then the pacsat file header is resaved to disk
 *
 * New files have their expiry time set to zero.  This means that expiry is based on the upload time.
 * We use this system so it is possible to change the expiry time for all files at once.  i.e. if we
 * reduce the expiry time from 5 days to 3 then all files uploaded more than 3 days ago will be purged
 * straight away.  When we set expiryTime that gives a fixed point in the future that a file expires.
 * We only allow that to be set by a system process or by a command station.
 * e.g. we might give telem or wod files a very short expiry time.  Installed files are set to never expire
 * by giving it an expiry time way in the future.
 *
 */
DIR_NODE * dir_add_pfh(HEADER *new_pfh, char *filename) {
	int resave = false;
	DIR_NODE *new_node = (DIR_NODE *)malloc(sizeof(DIR_NODE));
	new_node->pfh = new_pfh;
	time_t now = time(0); // Get the system time in seconds since the epoch
	if (new_node == NULL) return NULL; // ERROR
	if (dir_head == NULL) { // This is a new list
		dir_head = new_node;
		dir_tail = new_node;
		if (new_pfh->uploadTime == 0) {
			new_pfh->uploadTime = now;
			//debug_print("Resave new PFH at head of list\n");
			new_pfh->expireTime = 0; /* This means use the upload time to calculate expiry */
			resave = true;
		}
		new_node->next = NULL;
		new_node->prev = NULL;
	} else if (new_pfh->uploadTime == 0){
		/* Insert this at the end of the list as the newest item.  Make sure it has a unique upload time */
		if (dir_tail->pfh->uploadTime >= now) {
			/* We have added more than one file within 1 second.  Add this at the next available second. */
			new_pfh->uploadTime = dir_tail->pfh->uploadTime+1;
		} else {
			new_pfh->uploadTime = now;
		}
		new_pfh->expireTime = 0; /* This means use the upload time to calculate expiry */
		insert_after(dir_tail, new_node);
		//debug_print("Resave new PFH with upload time 0\n");
		resave = true;
	} else {
		/* Insert this at the right point, searching from the back*/
		DIR_NODE *p = dir_tail;
		while (p != NULL) {
			if (p->pfh->uploadTime == new_pfh->uploadTime) {
				debug_print("ERROR: Attempt to insert duplicate PFH: ");
				pfh_debug_print(new_pfh);
				free(new_node);
				return NULL; // this is a duplicate
			} else if (p->pfh->uploadTime < new_pfh->uploadTime) {
				insert_after(p, new_node);
				break;
			} else if (p == dir_head) {
				// Insert at the head of the list
				new_node->next = p;
				p->prev = new_node;
				new_node->prev = NULL;
				dir_head = new_node;
				break;
			}
			p = p->prev;
		}
	}
	// Now re-save the file with the new time if it changed, this recalculates the checksums
	if (resave) {
    	char file_name_with_path[MAX_FILE_PATH_LEN];
    	dir_get_file_path_from_file_id(new_node->pfh->fileId, get_dir_folder(), file_name_with_path, MAX_FILE_PATH_LEN);

		int rc = dir_fs_update_header(file_name_with_path, new_node->pfh);

		if (rc != EXIT_SUCCESS) {
			// we could not save this
			error_print("** Could not update the header for %s to dir\n",filename);
			dir_delete_node(new_node);
			return NULL;
		} else {
			//debug_print("Saved:");
			//pfh_debug_print(new_node->pfh);
		}
	}
	return new_node;
}

/**
 * dir_delete_node()
 *
 * Remove an entry from the dir linked list and free the memory held by the node
 * and the pacsat file header.
 *
 * The files on disk are not removed.
 *
 */
void dir_delete_node(DIR_NODE *node) {
	if (node == NULL) return;
	if (node->prev == NULL && node->next == NULL) {
		// special case of only one item
		dir_head = NULL;
		dir_tail = NULL;
	} else if (node->prev == NULL) {
		// special case removing the head of the list
		dir_head = node->next;
		node->next->prev = NULL;
	} else if (node->next == NULL) {
		// special case removing the tail of the list
		dir_tail = node->prev;
		node->prev->next = NULL;

	} else {
		node->next->prev = node->prev;
		node->prev->next = node->next;
	}
	//debug_print("REMOVED: ");
	pfh_debug_print(node->pfh);
	free(node->pfh);
	free(node);
}

/**
 * dir_free()
 *
 * Remove all entries from the dir linked list and free all the
 * memory held by the list and the pacsat file headers.
 */
void dir_free() {
	DIR_NODE *p = dir_head;
	while (p != NULL) {
		DIR_NODE *node = p;
		p = p->next;
		dir_delete_node(node);
	}
	//debug_print("Dir List Cleared\n");
}

/**
 * dir_debug_print()
 *
 * If DEBUG is set then print out all of the entries of the dir linked list.
 *
 */
void dir_debug_print(DIR_NODE *p) {
#ifdef DEBUG

	if (p == NULL)
		p = dir_head;
	while (p != NULL) {

		char buf[30];
		time_t t_old, t_new;;

		if (p->prev != NULL)
			t_old = p->prev->pfh->uploadTime + 1;
		else
			t_old = 0;

		strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&t_old));
		debug_print("Old:%s ", buf);

		if (p->next != NULL)
			t_new = p->next->pfh->uploadTime - 1;
		else {
			t_new = p->pfh->uploadTime;
		}
		strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&t_new));
		debug_print("New:%s ", buf);
		pfh_debug_print(p->pfh);
		p = p->next;
	}
#endif
}

/**
 * dir_load_pacsat_file()
 *
 * Load a PACSAT file from disk and store it in the directory
 */
int dir_load_pacsat_file(char *psf_name) {
	if (g_run_self_test)
		debug_print("Loading: %s \n", psf_name);
	HEADER *pfh = pfh_load_from_file(psf_name);
	if (pfh == NULL)
		return EXIT_FAILURE;
	int err = dir_validate_file(pfh,psf_name);
	if (err != ER_NONE) {
		free(pfh);
		error_print("Err: %d - validating: %s\n", err, psf_name);
		return EXIT_FAILURE;
	}
	if (g_run_self_test)
		pfh_debug_print(pfh);
	DIR_NODE *p = dir_add_pfh(pfh, psf_name);
	if (p == NULL) {
		debug_print("** Could not add %s to dir\n",psf_name);
		if (pfh != NULL) free(pfh);
		return EXIT_FAILURE;
	} else {
		if (pfh->fileId > g_dir_next_file_number)
			g_dir_next_file_number = pfh->fileId;
		return EXIT_SUCCESS;
	}
	return EXIT_SUCCESS;

}

/**
 * dir_load()
 *
 * Load the directory from the dir_folder, which must have been previously set by calling
 * dir_init().  For every file that ends with PSF_FILE_EXT (.act) we attempt to extract a
 * pacsat file header and add it to dir.
 */
int dir_load() {
	dir_free();
	DIR * d = opendir(dir_folder);
	if (d == NULL) { error_print("** Could not open dir: %s\n",dir_folder); return EXIT_FAILURE; }
	struct dirent *de;
	char psf_name[MAX_FILE_PATH_LEN];
	for (de = readdir(d); de != NULL; de = readdir(d)) {
		strlcpy(psf_name, dir_folder, sizeof(psf_name));
		strlcat(psf_name, "/", sizeof(psf_name));
		strlcat(psf_name, de->d_name, sizeof(psf_name));
		if ((strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0)) {
			if (str_ends_with(de->d_name, PSF_FILE_EXT)) {
				int rc = dir_load_pacsat_file(psf_name);
				if (rc != EXIT_SUCCESS) {
					debug_print("May need to remove potentially corrupt or duplicate PACSAT file: %s\n", psf_name);
					/* Don't automatically remove here, otherwise loading the dir twice actually deletes all the
					 * files! BUT - if we clean dir before loading it should be safe. There is danger they will not
					 * be expired if not loaded into dir */
				}
			} else {
				debug_print("Skipping %s\n",de->d_name);
			}
		}
	}
	closedir(d);
	save_state(); // in case the next file number changed

	return EXIT_SUCCESS;
}

int dir_validate_file(HEADER *pfh, char *filename) {
	//debug_print("DIR: Checking data in file: %s\n",filename);

	/* Now check the body */
	short int body_checksum = 0;
	unsigned int body_size = 0;

	FILE *infile = fopen(filename, "rb");
	if (infile == NULL) {
		return ER_NO_SUCH_FILE_NUMBER;
	}
	fseek(infile, pfh->bodyOffset, SEEK_SET);
	int ch=fgetc(infile);
	while (ch!=EOF) {
		body_checksum += ch & 0xff;
		body_size++;
		ch=fgetc(infile);
	}
	fclose(infile);
	if (pfh->bodyCRC != (body_checksum & 0xffff)) {
		error_print("** Body check %04x does not match %04x in file - failed for %s\n",(body_checksum & 0xffff), pfh->bodyCRC, filename);
		return ER_BODY_CHECK;
	}
	if (pfh->fileSize != pfh->bodyOffset + body_size) {
		error_print("** Body check failed for %s\n",filename);
		return ER_FILE_COMPLETE;
	}

	return ER_NONE;
}

/**
 * dir_get_pfh_by_date()
 *
 * Traverse the directory and return a pointer to the next node (PFH) identified by the dates.
 * The node passed in is the previous node that we processed.  If there are no more nodes
 * then NULL is returned.
 *
 * The protocol defines the required functionality as:
 *
      For each PAIR, the server will transmit directories for those files with
      UPLOAD_TIME greater than or equal to <start> and less than or equal to <end>.

      If there are no files within the range, the directory for the first file with
      upload time greater than <end> will be transmitted. If there is no such file,
      the directory for this first file with upload time less than <start> will be
      transmitted. In either case, the <t_old> and <t_new> fields in this directory
      will indicate to the client that there are no entries between <start> and
      <end>.
 *
 * Returns a pointer to the PFH or NULL if none are found.  NULL is typically
 * returned as we progress through a hole and have reached the end.
 *
 * However, in the special case where p was passed in as NULL, meaning this is
 * a new search from the head of the dir, then we should never return NULL.  Instead
 * we should return 1 record to close the hole according to the logic above.
 *
 */
DIR_NODE * dir_get_pfh_by_date(DIR_DATE_PAIR pair, DIR_NODE *p ) {
	DIR_NODE * first_node_after_end = NULL;
	DIR_NODE * last_node_before_start = NULL;
	int search_from_head = false;

	if (p == NULL) {
		/* Then we are starting the search from the head.  TODO - could later optimize if search from head or tail */
		search_from_head = true;
		p = dir_head;
	}
	while (p != NULL) {
		DIR_NODE *node = p;
		p = p->next;
		if (node->pfh->uploadTime >= pair.start && node->pfh->uploadTime <= pair.end)
			return node;
		if (search_from_head) {
			if (node->pfh->uploadTime > pair.end && first_node_after_end == NULL)
				first_node_after_end = node;
			if (node->pfh->uploadTime < pair.start)
				last_node_before_start = node;
		}
	}

	if (search_from_head) {
		if (first_node_after_end != NULL)
			return first_node_after_end;
		return last_node_before_start;
	}
	return NULL;
}

/**
 * Given a folder return the first pfh after this node
 *
 */
DIR_NODE * dir_get_pfh_by_folder_id(char *folder, DIR_NODE *p ) {

	if (p == NULL) {
		/* Then we are starting the search from the head.  TODO - could later optimize if search from head or tail */
		p = dir_head;
	}
	while (p != NULL) {
		DIR_NODE *node = p;
		p = p->next;
		if (pfh_contains_keyword(node->pfh, folder))
			return node;
	}

	return NULL;
}


/**
 * dir_get_node_by_id()
 * Search for and return a file based on its id. If the file can not
 * be found then return NULL
 *
 */
DIR_NODE * dir_get_node_by_id(int file_id) {
	DIR_NODE *p = dir_head;
	while (p != NULL) {
		if (p->pfh->fileId == file_id)
			return p;
		p = p->next;
	}
	return NULL;
}

/**
 * Utility function to copy a file
 * Based on stackoverflow answer: https://stackoverflow.com/questions/2180079/how-can-i-copy-a-file-on-unix-using-c
 */
int cp(const char *from, const char *to)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;

    fd_to = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd_to < 0)
        goto out_error;

    while ((nread = read(fd_from, buf, sizeof buf), nread) > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);

        /* Success! */
        return EXIT_SUCCESS;
    }

  out_error:
    saved_errno = errno;

    close(fd_from);
    if (fd_to >= 0)
    	close(fd_to);

    errno = saved_errno;
    return -1;
}

/**
 * Perform maintenance on the next node in the directory
 */
void dir_maintenance(time_t now) {
	if (dir_head == NULL) return;

	if (dir_maint_node == NULL)
		dir_maint_node = dir_head;

	if (pb_is_file_in_use(dir_maint_node->pfh->fileId)) {
		// This file is currently being broadcast then skip it until next time
		dir_maint_node = dir_maint_node->next;
		return;
	}

	char file_name_with_path[MAX_FILE_PATH_LEN];
	dir_get_file_path_from_file_id(dir_maint_node->pfh->fileId, get_dir_folder(), file_name_with_path, MAX_FILE_PATH_LEN);
	//    	debug_print("CHECKING: File id: %04x name: %s up:%d age:%d sec\n",dir_maint_node->pfh->fileId,
	//    			file_name_with_path, dir_maint_node->pfh->uploadTime, now-dir_maint_node->pfh->uploadTime);
	long age = 0;
	if (dir_maint_node->pfh->expireTime == 0) {
		/* Then expiry is based on a fixed time after upload */
		age = now - dir_maint_node->pfh->uploadTime;
	} else {
		/* Then expiry is a fixed time stored in the file */
		age = now - dir_maint_node->pfh->expireTime + g_dir_max_file_age_in_seconds;
	}
	//debug_print("%s Age: %ld \n",file_name_with_path, age);
	if (age < 0) {
		// We have not reached the expire time or this looks wrong, something is corrupt.  Skip it
		dir_maint_node = dir_maint_node->next;
	} else if (age > g_dir_max_file_age_in_seconds) {
		// Remove this file it is over the max age
		debug_print("Purging: %s\n",file_name_with_path);
		if (remove(file_name_with_path) != 0) {
			error_print("Could not remove the temp file: %s\n", file_name_with_path);
			// This was probablly open because it is being update or broadcast.  So it is OK to skip until next time
			dir_maint_node = dir_maint_node->next;
		} else {
			// Remove from the dir
			DIR_NODE *node = dir_maint_node;
			dir_maint_node = dir_maint_node->next;
			dir_delete_node(node);
		}
	} else {
		// TODO - Check if there is an expiry date in the header
		dir_maint_node = dir_maint_node->next;
	}

}

void dir_file_queue_check(time_t now, char * folder, uint8_t file_type, char * destination) {
	//debug_print("Checking for files in queue: %s\n",folder);
	DIR * d = opendir(folder);
	if (d == NULL) { error_print("** Could not open dir: %s\n",folder); return; }
	struct dirent *de;
	char file_name[MAX_FILE_PATH_LEN];
	char user_file_name[MAX_FILE_PATH_LEN];
	char psf_name[MAX_FILE_PATH_LEN];
	for (de = readdir(d); de != NULL; de = readdir(d)) {
		strlcpy(user_file_name, de->d_name, sizeof(user_file_name));
		strlcpy(file_name, folder, sizeof(file_name));
		strlcat(file_name, "/", sizeof(file_name));
		strlcat(file_name, de->d_name, sizeof(file_name));
		if ((strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0)) {
			if (str_ends_with(de->d_name, PSF_FILE_TMP)) {
//				debug_print("Skiping file: %s\n",de->d_name);
				continue;
			}
			uint32_t id = dir_next_file_number();
			char compression_type = BODY_NOT_COMPRESSED;
			/* Stat the file before we add the header to capture the create date */
			struct stat st;
			time_t create_time = now;
			if (stat(file_name, &st) == EXIT_SUCCESS) {
				create_time = st.st_atim.tv_sec; /* We use the time of last access as the create time.  So for a wod file this is the time the last data was written. */
				if (st.st_size > UNCOMPRESSED_FILE_SIZE_LIMIT) {
					/* Compress if more than 200 bytes used */
					char zip_command[MAX_FILE_PATH_LEN];
					char compressed_file_name[MAX_FILE_PATH_LEN];

					strlcpy(compressed_file_name, file_name, sizeof(compressed_file_name));
					strlcat(compressed_file_name,".zip", sizeof(compressed_file_name));

					strlcpy(zip_command, "zip -j -q ",sizeof(zip_command)); // -j to "junk" or not store the paths.  -q quiet
					strlcat(zip_command, compressed_file_name,sizeof(zip_command));
					strlcat(zip_command, " ",sizeof(zip_command));
					strlcat(zip_command, file_name,sizeof(zip_command));
					int zip_rc = system(zip_command);
					if (zip_rc == EXIT_SUCCESS) {
						/* We could compress it */
						strlcat(user_file_name,".zip", sizeof(user_file_name));
						compression_type = BODY_COMPRESSED_PKZIP;
						remove(file_name);
						strlcpy(file_name, compressed_file_name, sizeof(file_name));
					}
				}
			}
			HEADER *pfh = pfh_make_internal_header(now, file_type, id, "", g_bbs_callsign, destination, de->d_name, user_file_name,
					create_time, 0, compression_type);
			dir_get_file_path_from_file_id(pfh->fileId, dir_folder, psf_name, MAX_FILE_PATH_LEN);

			debug_print("Adding file in queue: %s\n",folder);	pfh_debug_print(pfh);
			dir_get_file_path_from_file_id(id, dir_folder, psf_name, sizeof(psf_name));

			int rc;
			rc = pfh_make_internal_file(pfh, dir_folder, file_name);
			if (rc != EXIT_SUCCESS) {
				printf("** Failed to make pacsat file %s\n", file_name);
				remove(psf_name); // remove this in case it was partially written, ignore any error
				if (pfh != NULL) free(pfh);
				continue;
			}

			rc = dir_load_pacsat_file(psf_name);
			if (rc != EXIT_SUCCESS) {
				debug_print("May need to remove potentially corrupt file from queue: %s\n", file_name);
				continue;
			}
			remove(file_name);
		}
	}
	closedir(d);
}

/**
 * This saves a big endian 4 byte int into little endian format in the MRAM
 */
int dir_fs_save_int(FILE *fp, uint32_t value, uint32_t offset) {
    int32_t numOfBytesWritten = -1;
    int32_t rc = fseek(fp, offset + 3, SEEK_SET);
    if (rc == -1) {
        return -1;
    }
    uint8_t data[4];
    pfh_store_int(data, value);
    numOfBytesWritten = fwrite(&data, sizeof(uint8_t), sizeof(data), fp);
    if (numOfBytesWritten != sizeof(data)) {
        printf("Write returned: %d\n",numOfBytesWritten);
        return -1;
    }
    return numOfBytesWritten;
}

/**
 * This saves a big endian 2 byte short into into little endian format in the MRAM
 */
int dir_fs_save_short(FILE *fp, uint16_t value, uint32_t offset) {
    int32_t numOfBytesWritten = -1;
    int32_t rc = fseek(fp, offset + 3, SEEK_SET);
    if (rc != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    uint8_t data[2];
    pfh_store_short(data, value);
    numOfBytesWritten = fwrite(&data, sizeof(uint8_t), sizeof(data), fp);
    if (numOfBytesWritten != sizeof(data)) {
        printf("Write returned: %d\n",numOfBytesWritten);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}


/**
 * Update the header in place in the file. This preserves any pacsat header fields that the spacecraft
 * does not understand, but which are important to the sender/receiver.
 *
 * This uses fixed offsets for the mandatory fields, which are defined in pacsat_header.h
 *
 * Returns EXIT_SUCCESS or EXIT_FAILURE if there is an error
 */
int dir_fs_update_header(char *file_name_with_path, HEADER *pfh) {
    int32_t rc;

	FILE *fp = fopen(file_name_with_path, "r+"); // open for reading and writing
	if (fp == NULL) {
        debug_print("Unable to open %s for writing: %s\n", file_name_with_path, strerror(errno));
        return EXIT_FAILURE;
    }
	/* Save fileid directly in file */
    rc = dir_fs_save_int(fp, pfh->fileId, FILE_ID_BYTE_POS);
    if (rc == EXIT_FAILURE) {
        debug_print("Unable to save fileid to %s with data at offset %d: %s\n", file_name_with_path, FILE_ID_BYTE_POS, strerror(errno));
        rc = fclose(fp);
        return EXIT_FAILURE;
    }
    /* Save upload time directly in file */
    rc = dir_fs_save_int(fp, pfh->uploadTime, UPLOAD_TIME_BYTE_POS_EX_SOURCE_LEN + pfh->source_length);
    if (rc == EXIT_FAILURE) {
        debug_print("Unable to save uploadtime to %s with data at offset %d: %s\n", file_name_with_path,
        		UPLOAD_TIME_BYTE_POS_EX_SOURCE_LEN + pfh->source_length, strerror(errno));
        rc = fclose(fp);
        return EXIT_FAILURE;
    }

    /* Then recalculate the checksum and write it */
    uint16_t crc_result = 0;
    rc = fseek(fp, 0, SEEK_SET);
    int num = fread(pfh_byte_buffer, sizeof(char), 1024, fp);
    if (num == 0) {
    	debug_print("Unable to re-read header from %s\n", file_name_with_path);
    	fclose(fp);
    	return EXIT_FAILURE; // nothing was read
    }
    /* First zero out the existing header checksum */
    pfh_byte_buffer[HEADER_CHECKSUM_BYTE_POS +3] = 0x00;
    pfh_byte_buffer[HEADER_CHECKSUM_BYTE_POS +4] = 0x00;

    int j;
    /* Then calculate the new one and save it back to MRAM */
    for (j=0; j<pfh->bodyOffset; j++)
    	crc_result += pfh_byte_buffer[j] & 0xff;
    rc = dir_fs_save_short(fp, crc_result, HEADER_CHECKSUM_BYTE_POS);
    if (rc == -1) {
    	debug_print("Unable to save header checksum to %s with data at offset %d: %s\n",
    			file_name_with_path, HEADER_CHECKSUM_BYTE_POS, strerror(errno));
    	rc = fclose(fp);
    	return EXIT_FAILURE;
    }

    rc = fclose(fp);
    if (rc != 0) {
    	printf("Unable to close %s: %s\n", file_name_with_path, strerror(errno));
    }
    return EXIT_SUCCESS;
}


/*********************************************************************************************
 *
 * SELF TESTS FOLLOW
 */

int make_big_test_dir() {
	int rc = EXIT_SUCCESS;

	debug_print("TEST Create a file\n");

	mkdir("/tmp/pacsat",0777);
	if (dir_init("/tmp") != EXIT_SUCCESS) { printf("** Could not initialize the dir\n"); return EXIT_FAILURE; };

	for (int f=0; f<100; f++) {
		char userfilename1[MAX_FILE_PATH_LEN];
		char psf_name[MAX_FILE_PATH_LEN];
		dir_get_file_path_from_file_id(f, get_dir_folder(), psf_name, MAX_FILE_PATH_LEN);
		char snum[5];
		sprintf(snum, "%d",f);
		strlcpy(userfilename1, "file", sizeof(userfilename1));
		strlcat(userfilename1, snum, sizeof(userfilename1));
		strlcat(userfilename1, ".txt", sizeof(userfilename1));

		char title[30];
		strlcpy(title, "Test Message ", sizeof(title));
		strlcat(title, snum, sizeof(title));
		char msg[50];
		strlcpy(msg, "Hi there,\nThis is a test message\nNumber = ", sizeof(msg));
		strlcat(msg, snum, sizeof(msg));

		write_test_msg(dir_folder, userfilename1, msg, strlen(msg));
		HEADER *pfh1 = make_test_header(f, snum, "ve2xyz", "g0kla", title, userfilename1);
		rc = test_pfh_make_pacsat_file(pfh1, dir_folder); if (rc != EXIT_SUCCESS) { printf("** Failed to make pacsat file1\n"); return EXIT_FAILURE; }
		/* Add to the dir */
		DIR_NODE *p = dir_add_pfh(pfh1,psf_name); if (p == NULL) { printf("** Could not add pfh1 to dir\n"); return EXIT_FAILURE; }
		sleep(1);
	}
	return rc;
}

int make_three_test_entries() {
	int rc = EXIT_SUCCESS;
	/* Make three test files */
	char filename1[MAX_FILE_PATH_LEN];
	dir_get_file_path_from_file_id(1, get_dir_folder(), filename1, MAX_FILE_PATH_LEN);
	char *userfilename1 = "file1.txt";
	char *msg = "Hi there,\nThis is a test message first\n";
	write_test_msg(dir_folder, userfilename1, msg, strlen(msg));

	char filename2[MAX_FILE_PATH_LEN];
	dir_get_file_path_from_file_id(2, get_dir_folder(), filename2, MAX_FILE_PATH_LEN);
	char *userfilename2 = "file2.txt";
	char *msg2 = "Hi again,\nThis is a test message as a follow up\nAll the best\nChris";
	write_test_msg(dir_folder, userfilename2, msg2, strlen(msg2));

	char filename3[MAX_FILE_PATH_LEN];
	dir_get_file_path_from_file_id(3, get_dir_folder(), filename3, MAX_FILE_PATH_LEN);
	char *userfilename3 = "file3.txt";
	char *msg3 = "Hi finally,\nThis is my last message\n";
	write_test_msg(dir_folder, userfilename3, msg3, strlen(msg3));

	HEADER *pfh1 = make_test_header(1, "1", "ve2xyz", "g0kla", "Test Msg 1", userfilename1);
	rc = test_pfh_make_pacsat_file(pfh1, dir_folder); if (rc != EXIT_SUCCESS) { printf("** Failed to make pacsat file1\n"); return EXIT_FAILURE; }
	/* Add to the dir */
	DIR_NODE *p = dir_add_pfh(pfh1,filename1); if (p == NULL) { printf("** Could not add pfh1 to dir\n"); return EXIT_FAILURE; }

	sleep(2);
	HEADER *pfh2 = make_test_header(2, "2", "ve2xyz", "g0kla", "Test Msg 2", userfilename2);
	rc = test_pfh_make_pacsat_file(pfh2,  dir_folder); if (rc != EXIT_SUCCESS) { printf("** Failed to make pacsat file2\n"); return EXIT_FAILURE; }
	p = dir_add_pfh(pfh2,filename2); if (p == NULL) { printf("** Could not add pfh2 to dir\n"); return EXIT_FAILURE; }

	// no sleep between these adds to test the case where two files have same upload time
	HEADER *pfh3 = make_test_header(3, "3", "ve2xyz", "g0kla", "Test Msg 3", userfilename3);
	rc = test_pfh_make_pacsat_file(pfh3,  dir_folder); if (rc != EXIT_SUCCESS) { printf("** Failed to make pacsat file3\n"); return EXIT_FAILURE; }
	p = dir_add_pfh(pfh3,filename3);  if (p == NULL) { printf("** Could not add pfh3 to dir\n"); return EXIT_FAILURE; }

	sleep(1);
	/* TODO - This only works if the pfh spec is copied into the temp dir */
	char filename4[MAX_FILE_PATH_LEN];
	dir_get_file_path_from_file_id(4, get_dir_folder(), filename4, MAX_FILE_PATH_LEN);
	char *userfilename4 = "pfh_spec.txt";
	char *target = "/tmp/pacsat/dir/pfh_spec.txt";
	FILE *file;
	if ((file = fopen(target, "r"))) {
		fclose(file);
	} else {
		int c = cp(userfilename4, target);
		if (c != EXIT_SUCCESS) { printf("** Could not copy pfh_spec.txt to dir. Rc %d errno %d\n",c,errno); return EXIT_FAILURE; }
	}
	HEADER *pfh4 = make_test_header(4, "4", "ac2cz", "g0kla", "Pacsat Header Definition", userfilename4);
	rc = test_pfh_make_pacsat_file(pfh4,  dir_folder); if (rc != EXIT_SUCCESS) { printf("** Failed to make pacsat file4\n"); return EXIT_FAILURE; }
	p = dir_add_pfh(pfh4,filename4);  if (p == NULL) { printf("** Could not add pfh4 to dir\n"); return EXIT_FAILURE; }

	return rc;
}

/**
 * Note that this test is redundant compared to test_pacsat_dir() but is usueful for debugging
 * issues because it creates, saves and loads just one file.
 *
 */
int test_pacsat_dir_one() {
	printf("##### TEST PACSAT DIR 1 FILE:\n");
		int rc = EXIT_SUCCESS;
	debug_print("TEST Create a file\n");

	mkdir("/tmp/pacsat",0777);
	if (dir_init("/tmp") != EXIT_SUCCESS) { printf("** Could not initialize the dir\n"); return EXIT_FAILURE; };

	char filename1[MAX_FILE_PATH_LEN];
	dir_get_file_path_from_file_id(1, get_dir_folder(), filename1, MAX_FILE_PATH_LEN);
	char *userfilename1 = "file1.txt";
	char *msg = "Hi there,\nThis is a test message first\n";
	write_test_msg(dir_folder, userfilename1, msg, strlen(msg));
	HEADER *pfh1 = make_test_header(1, "1", "ve2xyz", "g0kla", "Test Msg 1", userfilename1);

	pfh_debug_print(pfh1);
	rc = test_pfh_make_pacsat_file(pfh1, dir_folder); if (rc != EXIT_SUCCESS) { printf("** Failed to make pacsat file1\n"); return EXIT_FAILURE; }
	pfh_debug_print(pfh1);

	debug_print(".. then load it\n");
	HEADER *pfh2 = pfh_load_from_file("/tmp/pacsat/dir/0001.act");
	if (pfh2 == NULL) {printf("** Could not load load file\n"); return EXIT_FAILURE; }

	debug_print(".. add to dir, which resaves it with new uptime and new CRC\n");
	DIR_NODE * node = dir_add_pfh(pfh1,filename1);
	if (node == NULL) { printf("** Error creating dir node\n"); return EXIT_FAILURE; }
	if (dir_head->pfh->fileId != 1) { printf("** Error creating file 1\n"); return EXIT_FAILURE; }

	dir_free();

	debug_print(".. Now TEST Load the file\n");
	HEADER *pfh3 = pfh_load_from_file("/tmp/pacsat/dir/0001.act");
	if (pfh3 == NULL) {printf("** Could not load load file\n"); return EXIT_FAILURE; }

	debug_print(".. TEST Load the dir\n");
	dir_load();
	if (dir_head == NULL) {printf("** Could not load file into node\n"); return EXIT_FAILURE; }
	if (dir_head->pfh->fileId != 1) { printf("** Error loading file id\n"); return EXIT_FAILURE; }
	debug_print("LOADED DIR LIST\n");
	dir_debug_print(dir_head);

	dir_free();

	if (rc == EXIT_SUCCESS)
		printf("##### TEST PACSAT DIR 1 FILE: success\n");
	else
		printf("##### TEST PACSAT DIR 1 FILE: fail\n");
	return rc;
}

int test_pacsat_dir() {
	printf("##### TEST PACSAT DIR:\n");
	int rc = EXIT_SUCCESS;

	mkdir("/tmp/pacsat",0777);
	/* Make test dir folder */
	if (dir_init("/tmp") != EXIT_SUCCESS) { printf("** Could not initialize the dir\n"); return EXIT_FAILURE; };

	if (make_three_test_entries() == EXIT_FAILURE) { printf("** Could not make test files\n"); return EXIT_FAILURE; }
	debug_print("TEST DIR LIST\n");
	dir_debug_print(dir_head);
	if (dir_head->pfh->fileId != 1) { printf("** Error creating file 1\n"); return EXIT_FAILURE; }
	if (dir_head->next->pfh->fileId != 2) { printf("** Error creating file 2\n"); return EXIT_FAILURE; }
	if (dir_tail->pfh->fileId != 4) { printf("** Error creating file 4\n"); return EXIT_FAILURE; }

	debug_print("DELETE HEAD\n");
	dir_delete_node(dir_head);
	dir_debug_print(dir_head);
	if (dir_head->pfh->fileId != 2) { printf("** Error deleting head with file 2\n"); return EXIT_FAILURE; }
	if (dir_head->next->pfh->fileId != 3) { printf("** Error deleting head with file 3\n"); return EXIT_FAILURE; }
	dir_free();

	if (make_three_test_entries() == EXIT_FAILURE) { printf("** Could not make test files\n"); return EXIT_FAILURE; }
	debug_print("DELETE MIDDLE\n");
	dir_delete_node(dir_head->next);
	dir_debug_print(dir_head);
	if (dir_head->pfh->fileId != 1) { printf("** Error deleting middle with file 1\n"); return EXIT_FAILURE; }
	if (dir_head->next->pfh->fileId != 3) { printf("** Error deleting middle with file 3\n"); return EXIT_FAILURE; }
	dir_free();

	if (make_three_test_entries() == EXIT_FAILURE) { printf("** Could not make test files\n"); return EXIT_FAILURE; }
	debug_print("DELETE TAIL\n");
	dir_delete_node(dir_tail);
	dir_debug_print(dir_head);
	if (dir_head->pfh->fileId != 1) { printf("** Error deleting tail with file 1\n"); return EXIT_FAILURE; }
	if (dir_head->next->pfh->fileId != 2) { printf("** Error deleting tail with file 2\n"); return EXIT_FAILURE; }

	dir_free();

	if (make_three_test_entries() == EXIT_FAILURE) { printf("** Could not make test files\n"); return EXIT_FAILURE; }
	dir_debug_print(dir_head);
	dir_free(); // we just want the fresh files on disk, but the dir list to be empty

	/* Now load the dir from the folder and check that it is the same
	 * This tests insert to blank list, insert at end and insert in the middle, assuming
	 * the load order is file1, file3, file2
	 */
	debug_print("LOAD DIR\n");
	//dir_load();
	if (dir_load_pacsat_file("/tmp/pacsat/dir/0001.act") != EXIT_SUCCESS) {  printf("** Could not load psf 1\n"); return EXIT_FAILURE; }
	if (dir_load_pacsat_file("/tmp/pacsat/dir/0002.act") != EXIT_SUCCESS) {  printf("** Could not load psf 2\n"); return EXIT_FAILURE; }
	if (dir_load_pacsat_file("/tmp/pacsat/dir/0003.act") != EXIT_SUCCESS) {  printf("** Could not load psf 3\n"); return EXIT_FAILURE; }
	if (dir_load_pacsat_file("/tmp/pacsat/dir/0004.act") != EXIT_SUCCESS) {  printf("** Could not load psf 4\n"); return EXIT_FAILURE; }

	if (dir_head == NULL) {printf("** Could not load head\n"); return EXIT_FAILURE; }
	if (dir_head->next == NULL) {printf("** Could not load head + 1\n"); return EXIT_FAILURE; }
	if (dir_tail == NULL) {printf("** Could not load fail\n"); return EXIT_FAILURE; }

	if (dir_head->pfh->fileId != 1) { printf("** Error loading file 1 as head\n"); return EXIT_FAILURE; }
	if (dir_head->next->pfh->fileId != 2) { printf("** Error loading file 2 as second entry\n"); return EXIT_FAILURE; }
	if (dir_tail->pfh->fileId != 4) { printf("** Error loading file 4 as tail\n"); return EXIT_FAILURE; }
	debug_print("LOADED DIR LIST\n");
	dir_debug_print(dir_head);
	debug_print("TEST DUPLICATE DIR LOAD - expecting load errors, but exit success\n");
	if (dir_load() != EXIT_SUCCESS) { printf("** Error testing duplicate insertion\n"); return EXIT_FAILURE; } // confirm duplicates not loaded

	/* Test search for file */
	if (dir_get_node_by_id(1) == NULL) { printf("** Error finding file 1\n"); return EXIT_FAILURE; }
	DIR_NODE * last = dir_get_node_by_id(4);
	if ( last == NULL) { printf("** Error finding file 4\n"); return EXIT_FAILURE; }
	if (dir_get_node_by_id(9999) != NULL) { printf("** Error with search for missing file\n"); return EXIT_FAILURE; }
	// We should have only 4 files
	int c = 0;
	DIR_NODE *p = dir_head;
	while (p != NULL) {
		p = p->next;
		c++;
	}
	if ( c != 4) { printf("** Error expected 4 files, found %d\n",c); return EXIT_FAILURE; }

	dir_free();

	if (rc == EXIT_SUCCESS)
		printf("##### TEST PACSAT DIR: success\n");
	else
		printf("##### TEST PACSAT DIR: fail\n");
	return rc;
}
