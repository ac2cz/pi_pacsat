/*
 * pacsat_command.c
 *
 *  Created on: May 31, 2024
 *      Author: g0kla
 */

/* System include files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

/* Program Include Files */
#include "config.h"
#ifdef IORS_CONTROL_BUILD
#include "iors_command.h"
#include "iors_log.h"
#else
#include "uplink_command.h"
#include "pacsat_log.h"
#endif
#include "state_file.h"
#include "debug.h"
#include "pacsat_header.h"
#include "pacsat_broadcast.h"
#include "pacsat_command.h"
#include "pacsat_dir.h"
#include "str_util.h"
#include "ax25_tools.h"

/* Static vars*/
static int last_command_rc = EXIT_SUCCESS;

int pc_purge_files(char *folder);
int pc_delete_file_from_folder(DIR_NODE *node, char *folder, int is_directory_folder);

/**
 * pb_handle_command()
 *
 * Send a received command to the command task.
 *
 * Returns EXIT_SUCCESS if it could be processed, otherwise it returns EXIT_FAILURE
 *
 */
int pc_handle_command(char *from_callsign, unsigned char *data, int len) {
		struct t_ax25_header *ax25_header;
		ax25_header = (struct t_ax25_header *)data;
		if ((ax25_header->pid & 0xff) != PID_COMMAND) {
			return EXIT_FAILURE;
		}
		SWCmdUplink *sw_command;
		sw_command = (SWCmdUplink *)(data + sizeof(AX25_HEADER));

		if(sw_command->namespaceNumber != SWCmdNSPacsat) return EXIT_SUCCESS; // This was not for us, ignore

//		debug_print("Received PACSAT Command %04x addr: %d names: %d cmd %d from %s length %d\n",(sw_command->dateTime),
//				sw_command->address, sw_command->namespaceNumber, (sw_command->comArg.command), from_callsign, len);

		//	int i;
	//	for (i=0; i<4; i++)
	//		debug_print("arg:%d %d\n",i,sw_command->comArg.arguments[i]);
		/* Pass the data to the command processor */
		int cmd_rc = AuthenticateSoftwareCommand(sw_command);
		if (cmd_rc == EXIT_FAILURE){
			int r = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
			if (r != EXIT_SUCCESS) {
				debug_print("\n Error : Could not send ERR Response to TNC \n");
			}
			last_command_rc = EXIT_FAILURE;
			return EXIT_FAILURE;
		}
		int rc;
		if (cmd_rc == EXIT_DUPLICATE) {
			if (last_command_rc == EXIT_SUCCESS)
			    rc = pb_send_ok(from_callsign);
			else
				rc = pb_send_err(from_callsign, last_command_rc);
			if (rc != EXIT_SUCCESS) {
				debug_print("\n Error : Could not send OK Response to TNC \n");
			}
			return EXIT_SUCCESS; // Duplicate
		}
		last_command_rc = EXIT_SUCCESS; // We can also change this if we in fact send an err or ok command

//		debug_print("Auth Command\n");
		log_alog2f(WARN_LOG, g_log_filename, ALOG_COMMAND, from_callsign, 0, sw_command->namespaceNumber, sw_command->comArg.command,
				sw_command->comArg.arguments[0],sw_command->comArg.arguments[1],sw_command->comArg.arguments[2],sw_command->comArg.arguments[3]);

		switch (sw_command->comArg.command) {
			case SWCmdPacsatEnablePB: {
				//debug_print("Enable PB Command\n");
				g_state_pb_open = sw_command->comArg.arguments[0];
				if (sw_command->comArg.arguments[1]) {
					g_pb_status_period_in_seconds = sw_command->comArg.arguments[1];
				}
				if (sw_command->comArg.arguments[2]) {
					g_pb_max_period_for_client_in_seconds = sw_command->comArg.arguments[2];
				}
				last_command_rc = EXIT_SUCCESS;
				int rc = pb_send_ok(from_callsign);
				if (rc != EXIT_SUCCESS) {
					debug_print("\n Error : Could not send OK Response to TNC \n");
				}
				save_state();
				break;
			}
			case SWCmdPacsatEnableUplink: {
				//debug_print("Enable Uplink Command\n");
				g_state_uplink_open = sw_command->comArg.arguments[0];
				if (sw_command->comArg.arguments[1]) {
					g_uplink_status_period_in_seconds = sw_command->comArg.arguments[1];
				}
				if (sw_command->comArg.arguments[2]) {
					g_uplink_max_period_for_client_in_seconds = sw_command->comArg.arguments[2];
				}
				last_command_rc = EXIT_SUCCESS;
				int rc = pb_send_ok(from_callsign);
				if (rc != EXIT_SUCCESS) {
					debug_print("\n Error : Could not send OK Response to TNC \n");
				}
				save_state();
				break;
			}
			case SWCmdPacsatInstallFile: {
				/* Args are 32 bit fild id, 16 bit folder id */
				uint32_t file_id = sw_command->comArg.arguments[0] + (sw_command->comArg.arguments[1] << 16) ;
				uint16_t folder_id = sw_command->comArg.arguments[2];
				//dir_debug_print(NULL);

				if (pb_is_file_in_use(file_id)) {
					// This file is currently being broadcast then we can't update it
					last_command_rc = PB_ERR_TEMPORARY;
					pb_send_err(from_callsign, PB_ERR_TEMPORARY);
					break;
				}

				if (folder_id == FolderDir) {
					debug_print("Error - cant install into Directory\n");
					last_command_rc = PB_ERR_FILE_INVALID_PACKET;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}

				DIR_NODE *node = dir_get_node_by_id(file_id);
				if (node == NULL) {
					error_print("File %d not available\n",file_id);
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}
				//debug_print("Installing %d into %s with keywords %s\n",node->pfh->fileId, node->pfh->userFileName, node->pfh->keyWords);

				char *folder = get_folder_str(folder_id);
				if (folder == NULL) {
					//debug_print("Error - invalid folder\n");
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}

				//debug_print("Install File: %04x : %s into dir: %d - %s | File Name:%d\n",*arg0, source_file, *arg1, dest_file, *arg2);
				if (pfh_extract_file_and_update_keywords(node->pfh, folder, true) != EXIT_SUCCESS) {
					debug_print("Error extracting file into %s\n",folder);
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}

				if (dir_update_node(node) != EXIT_SUCCESS) {
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not update the dir node \n");
					}
					break;
				}

				/* If the same tag exists on another file with the same user_filename then remove it, as it can not be valid */
				DIR_NODE *search_node = dir_get_pfh_by_userfilename(node->pfh->userFileName, NULL);
				while (search_node != NULL) {
					DIR_NODE *next = search_node->next;  /* capture before the node can move to the tail */
					if (search_node->pfh->fileId != node->pfh->fileId
							&& pfh_contains_keyword(search_node->pfh, folder)) {
						if (pb_is_file_in_use(search_node->pfh->fileId)) {
							// Maintenance will later clean this up
							debug_print("Install: File id %d in use, stale tag for folder %s not removed.  Maintenance will clean it later.\n",
									search_node->pfh->fileId, folder);
						} else {
							debug_print("Install: Removing stale folder tag: File id %d folder %s\n",
									search_node->pfh->fileId, folder);
							pfh_remove_keyword(search_node->pfh, folder);
							if (dir_update_node(search_node) != EXIT_SUCCESS)
								error_print("Install: Could not resave pfh after stale keyword removed, for file id %d\n",
										search_node->pfh->fileId);
						}
					}
					if (next == NULL)
						search_node = NULL;
					else
						search_node = dir_get_pfh_by_userfilename(node->pfh->userFileName, next);
				}

				last_command_rc = EXIT_SUCCESS;
				int rc = pb_send_ok(from_callsign);
				if (rc != EXIT_SUCCESS) {
					debug_print("\n Error : Could not send OK Response to TNC \n");
				}

				//dir_debug_print(NULL);

				break;
			}
			case SWCmdPacsatDeleteFile: {
//				debug_print("Arg: %02x %02x\n",sw_command->comArg.arguments[0],sw_command->comArg.arguments[1]);
				uint32_t file_id = sw_command->comArg.arguments[0] + (sw_command->comArg.arguments[1] << 16) ;
				uint16_t folder_id = sw_command->comArg.arguments[2];

				if (pb_is_file_in_use(file_id)) {
					// This file is currently being broadcast then we can't update it
					last_command_rc = PB_ERR_TEMPORARY;
					pb_send_err(from_callsign, PB_ERR_TEMPORARY);
					break;
				}

				char *folder = get_folder_str(folder_id);
				if (folder == NULL) {
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					break;
				}
				int is_directory_folder = false;
				if (folder_id == FolderDir)
					is_directory_folder = true;

				DIR_NODE *node = dir_get_node_by_id(file_id);
				if (node == NULL) {
					error_print("File %d not available\n",file_id);
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}
				int rc = pc_delete_file_from_folder(node, folder, is_directory_folder);
				if (rc == EXIT_SUCCESS) {
					if (!is_directory_folder) {
						if (dir_update_node(node) != EXIT_SUCCESS) {
							last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
							int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
							if (r != EXIT_SUCCESS) {
								debug_print("\n Error : Could not update the dir node \n");
							}
							break;
						}
					} else {
						/* Delete any installed copies first - the keywords record where they are */
						char tmp[PFH_LONG_CHAR_FIELD_LEN];
						char *saveptr;
						strlcpy(tmp, node->pfh->keyWords, PFH_LONG_CHAR_FIELD_LEN);
						char *key = strtok_r(tmp, " ", &saveptr);
						while (key != NULL) {
							if (pc_delete_file_from_folder(node, key, false) != EXIT_SUCCESS)
								error_print("Delete: Could not remove installed copy of %d from %s\n",
										node->pfh->fileId, key);
							key = strtok_r(NULL, " ", &saveptr);
						}
						dir_delete_node(node);
					}
					last_command_rc = EXIT_SUCCESS;
					rc = pb_send_ok(from_callsign);
					if (rc != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send OK Response to TNC \n");
					}
				} else {
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
				}

				break;
			}
			case SWCmdPacsatDeleteFolder: {
				uint16_t folder_id = sw_command->comArg.arguments[0];
				int purge_orphan_files = sw_command->comArg.arguments[1];

				char *folder = get_folder_str(folder_id);
				if (folder == NULL) {
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}

				if (folder_id == FolderDir) {
					debug_print("Error - cant delete the Directory while pacsat is running!\n");
					last_command_rc = PB_ERR_FILE_INVALID_PACKET;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}

				/* Send Ok here as command is valid and any other errors below are ignored. */
				last_command_rc = EXIT_SUCCESS;
				int rc = pb_send_ok(from_callsign);
				if (rc != EXIT_SUCCESS) {
					debug_print("\n Error : Could not send OK Response to TNC \n");
				}
				DIR_NODE *node = dir_get_pfh_by_folder_id(folder, NULL);
				while (node != NULL) {
					DIR_NODE *next = node->next;  /* capture before the node can move to the tail */
					if (pb_is_file_in_use(node->pfh->fileId)) {
						debug_print("Delete folder: File id %d in use, skipped\n", node->pfh->fileId);
					} else {
						pc_delete_file_from_folder(node, folder, false);
						if (dir_update_node(node) != EXIT_SUCCESS) {
							error_print("Delete folder: Could not resave pfh for file id %d\n", node->pfh->fileId);
						}
					}
					node = (next == NULL) ? NULL : dir_get_pfh_by_folder_id(folder, next);
				}

				// Purge all other files
				if (purge_orphan_files) {
					if (pc_purge_files(folder) != EXIT_SUCCESS) {
						error_print("Purge orphan files: Could not open folder %s\n", folder);
					}
				}
				break;
			}

			case SWCmdPacsatDefaultFileExpiryPeriod: {
				uint16_t age = sw_command->comArg.arguments[0];
				if (age > 3650) { // set limit at 10 years to avoid overflow in int below
					last_command_rc = PB_ERR_FILE_INVALID_PACKET;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
					if (r != EXIT_SUCCESS) {
						debug_print("Error : Invalid age for file %d\n",age);
					}
					break;
				}
				g_dir_max_file_age_in_seconds = age * 24 * 60 * 60;
				save_state();

				last_command_rc = EXIT_SUCCESS;
				int rc = pb_send_ok(from_callsign);
				if (rc != EXIT_SUCCESS) {
					debug_print("\n Error : Could not send OK Response to TNC \n");
				}
				break;
			}
			case SWCmdPacsatFileExpiryPeriod: {
				uint32_t file_id = sw_command->comArg.arguments[0] + (sw_command->comArg.arguments[1] << 16);
				uint32_t expire_time = sw_command->comArg.arguments[2] + (sw_command->comArg.arguments[3] << 16);

				if (pb_is_file_in_use(file_id)) {
					// This file is currently being broadcast then we can't update it
					last_command_rc = PB_ERR_TEMPORARY;
					pb_send_err(from_callsign, PB_ERR_TEMPORARY);
					break;
				}

				//This needs to set the expiry time on a specific file
				DIR_NODE *node = dir_get_node_by_id(file_id);
				if (node == NULL) {
					error_print("File %d not available\n",file_id);
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}
				/* Set the expire date on that file. */
				node->pfh->expireTime = expire_time;
				if (dir_update_node(node) != EXIT_SUCCESS) {
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not update the dir node \n");
					}
					break;
				}

				last_command_rc = EXIT_SUCCESS;
				int rc = pb_send_ok(from_callsign);
				if (rc != EXIT_SUCCESS) {
					debug_print("\n Error : Could not send OK Response to TNC \n");
				}

				break;
			}
			case SWCmdPacsatDirMaintPeriod: {
				uint16_t dir_period = sw_command->comArg.arguments[0];
				g_dir_maintenance_period_in_seconds = dir_period;
				save_state();

				last_command_rc = EXIT_SUCCESS;
				pb_send_ok(from_callsign);
				break;
			}
			case SWCmdPacsatFtl0MaintPeriod: {
				uint16_t ftl0_period = sw_command->comArg.arguments[0];
				g_ftl0_maintenance_period_in_seconds = ftl0_period;
				save_state();

				last_command_rc = EXIT_SUCCESS;
				pb_send_ok(from_callsign);
				break;
			}
			case SWCmdPacsatFileQueueCheckPeriod: {
				uint16_t check_period = sw_command->comArg.arguments[0];
				g_file_queue_check_period_in_seconds = check_period;
				save_state();

				last_command_rc = EXIT_SUCCESS;
				pb_send_ok(from_callsign);
				break;
			}
			case SWCmdPacsatMaxFileSize: {
				uint16_t file_size = sw_command->comArg.arguments[0];
				g_ftl0_max_file_size = file_size * 1024;
				save_state();

				last_command_rc = EXIT_SUCCESS;
				pb_send_ok(from_callsign);
				break;
			}
			case SWCmdPacsatMaxUploadAge: {
				uint16_t upload_file_age = sw_command->comArg.arguments[0];
				g_ftl0_max_upload_age_in_seconds = upload_file_age * 24 * 60 * 60;
				save_state();

				last_command_rc = EXIT_SUCCESS;
				pb_send_ok(from_callsign);
				break;
			}
			case SWCmdPacsatEnableFSTelemetry: {
				uint16_t enable = sw_command->comArg.arguments[0];
				uint16_t period = sw_command->comArg.arguments[1];

				if (enable) {
					if (period == 0) {
						g_telem_send_period_in_seconds = DEFAULT_PERIOD_TO_SEND_TELEM;
					} else {
						if (period < MIN_PACKET_PERIOD)
							period = MIN_PACKET_PERIOD;
						g_telem_send_period_in_seconds = period;
					}
				} else {
					g_telem_send_period_in_seconds = 0; // disable it
				}
				save_state();
				last_command_rc = EXIT_SUCCESS;
				pb_send_ok(from_callsign);
				break;
			}
#ifdef IORS_CONTROL_BUILD
			case SWCmdPacsatChangeSigningKey: {
				uint16_t key_no = sw_command->comArg.arguments[0];
				if (key_no >= NO_OF_SIGNING_KEYS) {
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					break;
				}
				if (load_signing_key(key_no) != EXIT_SUCCESS) {
					last_command_rc = PB_ERR_FILE_NOT_AVAILABLE;
					pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					break;
				}
				g_state_image_signing_key_number = key_no;
				save_state();

				last_command_rc = EXIT_SUCCESS;
				pb_send_ok(from_callsign);
				break;
			}
#endif

			default:
				error_print("\n Error : Unknown pacsat command: %d\n",sw_command->comArg.command);
				last_command_rc = PB_ERR_COMMAND_NOT_AVAILABLE;
				int r = pb_send_err(from_callsign, PB_ERR_COMMAND_NOT_AVAILABLE);
				if (r != EXIT_SUCCESS) {
					debug_print("\n Error : Could not send ERR Response to TNC \n");
				}

				return EXIT_FAILURE;
				break;
		}
		return EXIT_SUCCESS;
}


int load_signing_key(int key_number) {
    char signing_key_path[MAX_FILE_PATH_LEN];
    char image_signing_key_filename[MAX_FILE_PATH_LEN];
    strlcpy(signing_key_path, "/opt/iors/keys/",MAX_FILE_PATH_LEN);
    snprintf(image_signing_key_filename, sizeof(image_signing_key_filename), "image_key_public%d.raw",key_number);
    strlcat(signing_key_path, image_signing_key_filename,sizeof(signing_key_path));

    if (load_image_signing_key(signing_key_path, g_image_signing_public_key) != EXIT_SUCCESS) {
    	error_print("** Could not load image signing key %s\n",signing_key_path);
    	return EXIT_FAILURE;
    } else {
    	debug_print("Loaded signing key: %s\n",signing_key_path);
    }
    return EXIT_SUCCESS;
}

/**
 * Delete all the files in a folder
 */
int pc_purge_files(char *folder) {
	char dir_folder[MAX_FILE_PATH_LEN];
	strlcpy(dir_folder, get_data_folder(), MAX_FILE_PATH_LEN);
	strlcat(dir_folder, "/", MAX_FILE_PATH_LEN);
	strlcat(dir_folder, folder, MAX_FILE_PATH_LEN);
	//debug_print("Purging remaining files from: %s\n",dir_folder);
	DIR * d = opendir(dir_folder);
	if (d == NULL) {
		error_print("** Could not open dir: %s\n",dir_folder);
		return EXIT_FAILURE;
	} else {
		struct dirent *de;
		for (de = readdir(d); de != NULL; de = readdir(d)) {
			char orphan_file_name[MAX_FILE_PATH_LEN];
			strlcpy(orphan_file_name, dir_folder, sizeof(orphan_file_name));
			strlcat(orphan_file_name, "/", sizeof(orphan_file_name));
			strlcat(orphan_file_name, de->d_name, sizeof(orphan_file_name));
			if ((strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0)) {
				//debug_print("Purging: %s\n",orphan_file_name);
				remove(orphan_file_name);
			}
		}
		closedir(d);
	}
	return EXIT_SUCCESS;
}
int pc_delete_file_from_folder(DIR_NODE *node, char *folder, int is_directory_folder) {
//	debug_print("Deleting %d from %s with keywords %s\n",node->pfh->fileId, node->pfh->userFileName, node->pfh->keyWords);
	char dest_file[MAX_FILE_PATH_LEN];
	char file_name[10];
	snprintf(file_name, 10, "%04x",node->pfh->fileId);
	if (is_directory_folder || strlen(node->pfh->userFileName) == 0) {
		strlcpy(dest_file, get_data_folder(), MAX_FILE_PATH_LEN);
		strlcat(dest_file, "/", MAX_FILE_PATH_LEN);
		strlcat(dest_file, folder, MAX_FILE_PATH_LEN);
		strlcat(dest_file, "/", MAX_FILE_PATH_LEN);
		strlcat(dest_file, file_name, MAX_FILE_PATH_LEN);
		if (is_directory_folder) {
			strlcat(dest_file, PSF_FILE_EXT, MAX_FILE_PATH_LEN);
		}
	} else {
		strlcpy(dest_file, get_data_folder(), MAX_FILE_PATH_LEN);
		strlcat(dest_file, "/", MAX_FILE_PATH_LEN);
		strlcat(dest_file, folder, MAX_FILE_PATH_LEN);
		strlcat(dest_file, "/", MAX_FILE_PATH_LEN);
		strlcat(dest_file, node->pfh->userFileName, MAX_FILE_PATH_LEN);
		//debug_print("Delete File by userfilename: %04x in dir: %s - %s\n",node->pfh->fileId, folder, dest_file);
	}
//	debug_print("Remove: %s\n",dest_file);
	struct stat st = {0};
	if (stat(dest_file, &st) == -1) {
		// No file exists, but try to remove the redundant keywords
		pfh_remove_keyword(node->pfh, folder);
		return EXIT_SUCCESS;
	}

	if (remove(dest_file) == EXIT_SUCCESS) {
		/* If successful we change the header to remove the keyword for the installed dir and set the upload date */
		pfh_remove_keyword(node->pfh, folder);
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}

}
