/*
 * pacsat_broadcast.c
 *
 *  Created on: Oct 13, 2022
 *      Author: g0kla
 *
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
 * =====================================================================
 *
 * The PB handles all broadcasts to the ground and processes all requests
 * for broadcasts.  The PB list holds the list of stations that have
 * requested broadcasts.

ALL ABOUT THE BROADCASTs
========================

The server maintains a queue with 10 entries; each entry is a hole list
request or broadcast start request. A particular station (as identified by
callsign, not including SSID) can have only one entry on the queue.

Entries are removed from the queue:
           after 10 minutes;
           after a hole list has been completely transmitted (for hole list);
           after a file has been completely transmitted (for start request);
           when a new request is received from a station already in the queue;
           if the file associated with the entry cannot be opened and read;

On a periodic basis we broadcast the PB Status with a UI packet from the BBS callsign
to one of the following callsigns: PBLIST, PBFULL, PBSHUT, PBSTAT

* PBLIST - This is used when the list is empty or when there are a list of callsins
  on the PB
* PBFULL - When there is no more room on the PB this callsign is used
* PBSHUT - If the broadcast protocol is not available then this callsign is used
* PBSTAT - TBD

A DIR or FILE request is sent from a ground station to the BROADCAST callsign of the
spacecraft with a PID 0xBD for a dir request  or 0xBB for a file reqest.

When a request is received we send OK <callsign> from the bbs callsign directly to the
callsign that sent the request with a PID of 0xF0 and the text OK <callsign>.  It looks
like it is terminated with a 0x0D linefeed, but that needs some more research to see if
it is required.

If there is an error in the request then we send NO -X where X is the error number.  It
is sent from the bbs callsign with a PID of 0xF0.

A DIR or FILE request is added to the PB assuming we have space on the PB list

If the File Number is not available for a file request then we send an error
packet: NO

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
#include "iors_command.h"
#include "state_file.h"
#include "debug.h"
#include "agw_tnc.h"
#include "pacsat_header.h"
#include "pacsat_broadcast.h"
#include "pacsat_dir.h"
#include "str_util.h"
#include "crc.h"
#include "ax25_tools.h"

/* An entry on the PB list keeps track of the requester and where we are in the request process */
struct pb_entry {
	int pb_type; /* DIR or FILE request */
	char callsign[MAX_CALLSIGN_LEN];
	DIR_NODE *node; /* Pointer to the node that we should broadcast next */
//	int file_id; /* File id of the file we are broadcasting if this is a file request */
	int offset; /* The current offset in the file we are broadcasting or the PFH we are transmitting */
	int block_size; /* The maximum size of broadcasts. THIS IS CURRENTLY IGNORED but in theory is sent from the ground for file requests */
	void *hole_list; /* This is a DIR or FILE hole list */
	int hole_num; /* The number of holes from the request */
	int current_hole_num; /* The next hole number from the request that we should process when this one is done */
	time_t request_time; /* The time the request was received for timeout purposes */
};
typedef struct pb_entry PB_ENTRY;

/* Forward declarations */
int pb_send_status();
int pb_add_request(char *from_callsign, int type, DIR_NODE * node, int file_id, int offset, void *holes, int num_of_holes);
int pb_handle_dir_request(char *from_callsign, unsigned char *data, int len);
int pb_handle_file_request(char *from_callsign, unsigned char *data, int len);
int pb_handle_command(char *from_callsign, unsigned char *data, int len);
void pb_make_list_str(char *buffer, int len);
int pb_make_dir_broadcast_packet(DIR_NODE *node, unsigned char *data_bytes, int *offset);
DIR_DATE_PAIR * get_dir_holes_list(unsigned char *data);
int get_num_of_dir_holes(int request_len);
int pb_delete_file_from_folder(DIR_NODE *node, char*folder, int is_directory_folder);
int pb_broadcast_next_file_chunk(HEADER *psf, char * psf_filename, int offset, int length, int file_size);
int pb_make_file_broadcast_packet(HEADER *pfh, unsigned char *data_bytes,
		unsigned char *buffer, int number_of_bytes_read, int offset, int chunk_includes_last_byte);
FILE_DATE_PAIR * get_file_holes_list(unsigned char *data);
int get_num_of_file_holes(int request_len);

void pb_debug_print_dir_req(unsigned char *data, int len);
void pb_debug_print_dir_holes(DIR_DATE_PAIR *holes, int num_of_holes);
void pb_debug_print_file_holes(FILE_DATE_PAIR *holes, int num_of_holes);
void pb_debug_print_list_item(int i);

/* Local Variables */

/**
 * pb_list
 * This Directory and File broadcast list is a list of callsigns that will receive attention from
 * the PACSAT.  It stores the callsign and the request, which is for a file or a directory.
 * This is a static block of memory that exists throughout the duration of the program to
 * keep track of stations on the PB.
 */
static PB_ENTRY pb_list[MAX_PB_LENGTH];

/**
 * dir hole_list
 * The hole list works in parallel to the pb_list.  For each entry on the PB there can be a list
 * of holes in the directory or in the file.  This array keeps track of the holes.
 *
 */
//static DATE_PAIR hole_lists[MAX_PB_LENGTH][AX25_MAX_DATA_LEN/8]; /* The holes lists */

static char pb_status_buffer[135]; // 10 callsigns * 13 bytes + 4 + nul
unsigned char broadcast_buffer[PB_FILE_DEFAULT_BLOCK_SIZE]; // This is the chunk we will send
unsigned char packet_buffer[AX25_MAX_DATA_LEN];
unsigned char packet_data_bytes[AX25_MAX_DATA_LEN];
static int number_on_pb = 0; /* This keeps track of how many stations are in the pb_list array */
static int current_station_on_pb = 0; /* This keeps track of which station we will send data to next */
time_t last_pb_status_time;
time_t last_pb_frames_queued_time;
int sent_pb_status = false;

/**
 * pb_send_status()
 *
 * Transmit the current status of the PB
 *
 * Returns EXIT_SUCCESS unless it was unable to send the request to the TNC
 *
 */
int pb_send_status() {
	if (!g_state_pb_open) {
		unsigned char shut[] = "PB Closed.";
		int rc = EXIT_SUCCESS;
		if (!g_run_self_test)
			send_raw_packet(g_broadcast_callsign, PBSHUT, PID_NO_PROTOCOL, shut, sizeof(shut));
		return rc;
	} else  {
		char * CALL = PBLIST;
		if (number_on_pb == MAX_PB_LENGTH) {
			CALL = PBFULL;
		}
		pb_make_list_str(pb_status_buffer, sizeof(pb_status_buffer));
		unsigned char command[strlen(pb_status_buffer)]; // now put the list in a buffer of the right size
		strlcpy((char *)command, (char *)pb_status_buffer,sizeof(command));
		int rc = EXIT_SUCCESS;
		if (!g_run_self_test)
			send_raw_packet(g_broadcast_callsign, CALL, PID_NO_PROTOCOL, command, sizeof(command));
		return rc;
	}
}

/**
 * pb_send_ok()
 *
 * Send a UI frame from the broadcast callsign to the station with PID BB and the
 * text OK <callsign>0x0Drequest_list
 */
int pb_send_ok(char *from_callsign) {
	int rc = EXIT_SUCCESS;
	char buffer[4 + strlen(from_callsign)]; // OK + 10 char for callsign with SSID
	strlcpy(buffer,"OK ", sizeof(buffer));
	strlcat(buffer, from_callsign, sizeof(buffer));
    int len = 3 + strlen(from_callsign);
    buffer[len] = 0x0D; // this replaces the string termination
	if (!g_run_self_test)
		rc = send_raw_packet(g_broadcast_callsign, from_callsign, PID_FILE, (unsigned char *)buffer, sizeof(buffer));

	return rc;
}

/**
 * pb_send_err()
 *
 * Send a UI frame to the station containing an error response.  The error values are defined in
 * the header file
 *
 * returns EXIT_SUCCESS unless it is unable to send the data to the TNC
 *
 */
int pb_send_err(char *from_callsign, int err) {
	int rc = EXIT_SUCCESS;
	char err_str[2];
	snprintf(err_str, 3, "%d",err);
	char buffer[6 + strlen(err_str)+ strlen(from_callsign)]; // NO -XX + 10 char for callsign with SSID
	char CR = 0x0d;
	strlcpy(buffer,"NO -", sizeof(buffer));
	strlcat(buffer, err_str, sizeof(buffer));
	strlcat(buffer," ", sizeof(buffer));
	strlcat(buffer, from_callsign, sizeof(buffer));
	strncat(buffer,&CR,1); // very specifically add just one char to the end of the string for the CR
	if (!g_run_self_test)
		rc = send_raw_packet(g_broadcast_callsign, from_callsign, PID_FILE, (unsigned char *)buffer, sizeof(buffer));

	return rc;
}

/**
 * pb_add_request()
 *
 * Add a callsign and its request to the PB
 *
 * Make a copy of all the data because the original packet will be purged soon from the
 * circular buffer
 * Note that when we are adding an item the variable number_on_pb is pointing to the
 * empty slot where we want to insert data because the number is one greater than the
 * array index (which starts at 0)
 *
 * returns EXIT_SUCCESS it it succeeds or EXIT_FAILURE if the PB is shut or full
 *
 */
int pb_add_request(char *from_callsign, int type, DIR_NODE * node, int file_id, int offset, void *holes, int num_of_holes) {
	if (!g_state_pb_open) return EXIT_FAILURE;
	if (number_on_pb == MAX_PB_LENGTH) {
		return EXIT_FAILURE; // PB full
	}

	/* Each station can only be on the PB once, so reject if the callsign is already in the list */
	for (int i=0; i < number_on_pb; i++) {
		if ((strcmp(pb_list[i].callsign, from_callsign) == 0)) {
			return EXIT_FAILURE; // Station is already on the PB
		}
	}

	strlcpy(pb_list[number_on_pb].callsign, from_callsign, MAX_CALLSIGN_LEN);
	pb_list[number_on_pb].pb_type = type;
//	pb_list[number_on_pb].file_id = file_id;
	pb_list[number_on_pb].offset = offset;
	pb_list[number_on_pb].request_time = time(0);
	pb_list[number_on_pb].hole_num = num_of_holes;
	pb_list[number_on_pb].current_hole_num = 0;
	pb_list[number_on_pb].node = node;
	if (num_of_holes > 0) {
		if (type == PB_DIR_REQUEST_TYPE) {
			DIR_DATE_PAIR *dir_holes = (DIR_DATE_PAIR *)holes;
			DIR_DATE_PAIR *hole_list = (DIR_DATE_PAIR *)malloc(num_of_holes * sizeof(DIR_DATE_PAIR));
			for (int i=0; i<num_of_holes; i++) {
				hole_list[i].start = dir_holes[i].start;
				hole_list[i].end = dir_holes[i].end;
			}
			pb_list[number_on_pb].hole_list = hole_list;
		} else {
			FILE_DATE_PAIR *file_holes = (FILE_DATE_PAIR *)holes;
			FILE_DATE_PAIR *hole_list = (FILE_DATE_PAIR *)malloc(num_of_holes * sizeof(FILE_DATE_PAIR));
			for (int i=0; i<num_of_holes; i++) {
				hole_list[i].offset = file_holes[i].offset;
				hole_list[i].length = file_holes[i].length;
			}
			pb_list[number_on_pb].hole_list = hole_list;
		}
	}
	number_on_pb++;

	return EXIT_SUCCESS;
}

/**
 * pb_remove_request()
 *
 * Remove the callsign at the designated position.  This is most likely the
 * head because we finished a request.
 *
 * note that the variable number_on_pb is one greater than the last array position
 * with data.
 *
 * Returns EXIT_SUCCESS if it can be removed or EXIT_FAILURE if there was
 * no such item
 *
 */
int pb_remove_request(int pos) {
	if (number_on_pb == 0) return EXIT_FAILURE;
	if (pos >= number_on_pb) return EXIT_FAILURE;
	if (pos != number_on_pb-1) {

		/* Remove the item and shuffle all the other items to the left */
		for (int i = pos + 1; i < number_on_pb; i++) {
			strlcpy(pb_list[i-1].callsign, pb_list[i].callsign, MAX_CALLSIGN_LEN);
			pb_list[i-1].pb_type = pb_list[i].pb_type;
//			pb_list[i-1].file_id = pb_list[i].file_id;
			pb_list[i-1].offset = pb_list[i].offset;
			pb_list[i-1].request_time = pb_list[i].request_time;
			pb_list[i-1].hole_num = pb_list[i].hole_num;
			pb_list[i-1].node = pb_list[i].node;
			pb_list[i-1].current_hole_num = pb_list[i].current_hole_num;
			pb_list[i-1].hole_list = pb_list[i].hole_list;
		}
	}
	/* We want to free the hole list from the last position in the list before we decrement the number on pb
	 * number_on_pb is pointing to an empty position now. */
	if (pb_list[pos].hole_num  > 0)
		free(pb_list[number_on_pb].hole_list);
	number_on_pb--;

	/* We have to update the station we will next send data to.
	 * If a station earlier in the list was removed, then this decrements by one.
	 * If a station later in the list was remove we do nothing.
	 * If the current station was removed then we do nothing because we are already
	 * pointing to the next station, unless we are at the end of the list */
	if (pos < current_station_on_pb) {
		current_station_on_pb--;
		if (current_station_on_pb < 0)
			current_station_on_pb = 0;
	} else if (pos == current_station_on_pb) {
		if (current_station_on_pb >= number_on_pb)
			current_station_on_pb = 0;
	}
	return EXIT_SUCCESS;
}

/**
 * pb_make_list_str()
 *
 * Build the status string that is periodically transmitted.
 * The *buffer to receive the string and its length len should be passed in.
 */
void pb_make_list_str(char *buffer, int len) {
	if (number_on_pb == 0)
		strlcpy(buffer, "PB Empty.", len);
	else
		strlcpy(buffer, "PB ", len);
	for (int i=0; i < number_on_pb; i++) {
			strlcat(buffer, pb_list[i].callsign, len);
		if (pb_list[i].pb_type == PB_DIR_REQUEST_TYPE)
			strlcat(buffer, "/D ", len);
		else
			strlcat(buffer, " ", len);
	}
}

void pb_debug_print_list() {
	char buffer[256];
	pb_make_list_str(buffer, sizeof(buffer));
	debug_print("%s\n",buffer);
	for (int i=0; i < number_on_pb; i++) {
		pb_debug_print_list_item(i);
	}
}

void pb_debug_print_list_item(int i) {
	debug_print("--%s Ty:%d ",pb_list[i].callsign,pb_list[i].pb_type);
	if (pb_list[i].node != NULL)
		debug_print("File:%d ",pb_list[i].node->pfh->fileId);
	debug_print("Off:%d Holes:%d Cur:%d",pb_list[i].offset,pb_list[i].hole_num,pb_list[i].current_hole_num);
	char buf[30];
	time_t now = pb_list[i].request_time;
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
	debug_print(" at:%s", buf);

	if (pb_list[i].pb_type == PB_DIR_REQUEST_TYPE)
		pb_debug_print_dir_holes(pb_list[i].hole_list, pb_list[i].hole_num);
	else
		pb_debug_print_file_holes(pb_list[i].hole_list, pb_list[i].hole_num);
}

/**
 * pb_process_frame()
 *
 * process a UI frame received from a ground station.  This may contain a Pacsat Broadcast request,
 * otherwise it can be ignored.
 * This is called from the main processing loop whenever a type K frame is sent to the broadcast
 * callsign.
 *
 */
void pb_process_frame(char *from_callsign, char *to_callsign, unsigned char *data, int len) {
	struct t_ax25_header *broadcast_request_header;
	broadcast_request_header = (struct t_ax25_header *)data;

	//debug_print("Broadcast Request: pid: %02x \n", broadcast_request_header->pid & 0xff);
	if ((broadcast_request_header->pid & 0xff) == PID_DIRECTORY) {
		pb_handle_dir_request(from_callsign, data, len);
	}
	if ((broadcast_request_header->pid & 0xff) == PID_FILE) {
		// File Request
		pb_handle_file_request(from_callsign, data, len);
	}
	if ((broadcast_request_header->pid & 0xff) == PID_COMMAND) {
		// Command Request
		pb_handle_command(from_callsign, data, len);
	}
}


/**
 * pb_handle_dir_request()
 *
 * Process a dir request from a ground station
 *
 * Returns EXIT_SUCCESS if the request could be processed, even if the
 * station was not added to the PB.  Only returns EXIT_FAILURE if there is
 * an unexpected error, such as the TNC is unavailable.
 */
int pb_handle_dir_request(char *from_callsign, unsigned char *data, int len) {
	// Dir Request
	int rc=EXIT_SUCCESS;
	DIR_REQ_HEADER *dir_header;
	dir_header = (DIR_REQ_HEADER *)(data + sizeof(AX25_HEADER));

	// TODO - we do not check bit 5, which must be 1 or the version bits, which must be 00.

	/* least sig 2 bits of flags are 00 if this is a fill request */
	if ((dir_header->flags & 0b11) == 0b00) {
		pb_debug_print_dir_req(data, len);
		//debug_print("DIR FILL REQUEST: flags: %02x BLK_SIZE: %04x\n", dir_header->flags & 0xff, dir_header->block_size &0xffff);

		/* Get the number of holes in this request and make sure it is in a valid range */
		int num_of_holes = get_num_of_dir_holes(len);
		if (num_of_holes < 1 || num_of_holes > AX25_MAX_DATA_LEN / sizeof(DIR_DATE_PAIR)) {
			/* This does not have a valid holes list */
			rc = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send ERR Response to TNC \n");
				return EXIT_FAILURE;
			}
			return EXIT_SUCCESS;
		}
		/* Add to the PB if we can*/
		DIR_DATE_PAIR * holes = get_dir_holes_list(data);
		if (pb_add_request(from_callsign, PB_DIR_REQUEST_TYPE, NULL, 0, 0, holes, num_of_holes) == EXIT_SUCCESS) {
			// ACK the station
			rc = pb_send_ok(from_callsign);
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send OK Response to TNC \n");
				return EXIT_FAILURE;
			}
		} else {
			// the protocol says NO -1 means temporary problem. e.g. shut or you are already on the PB, and -2 means permanent
			rc = pb_send_err(from_callsign, PB_ERR_TEMPORARY);
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send ERR Response to TNC \n");
				return EXIT_FAILURE;
			}
		}
	} else {
		/* There are no other valid DIR Requests other than a fill */
		rc = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
		if (rc != EXIT_SUCCESS) {
			error_print("\n Error : Could not send ERR Response to TNC \n");
			return EXIT_FAILURE;
		}
	}
	return rc;
}

int get_num_of_dir_holes(int request_len) {
	int num_of_holes = (request_len - sizeof(AX25_HEADER) - sizeof(DIR_REQ_HEADER)) / sizeof(DIR_DATE_PAIR);
	return num_of_holes;
}

DIR_DATE_PAIR * get_dir_holes_list(unsigned char *data) {
	DIR_DATE_PAIR *holes = (DIR_DATE_PAIR *)(data + sizeof(AX25_HEADER) + sizeof(DIR_REQ_HEADER) );
	return holes;
}


int get_num_of_file_holes(int request_len) {
	int num_of_holes = (request_len - sizeof(AX25_HEADER) - sizeof(FILE_REQ_HEADER)) / sizeof(FILE_DATE_PAIR);
	return num_of_holes;
}

FILE_DATE_PAIR * get_file_holes_list(unsigned char *data) {
	FILE_DATE_PAIR *holes = (FILE_DATE_PAIR *)(data + sizeof(AX25_HEADER) + sizeof(FILE_REQ_HEADER) );
	return holes;
}

void pb_debug_print_dir_holes(DIR_DATE_PAIR *holes, int num_of_holes) {
	debug_print(" - %d holes: ",num_of_holes);
	for (int i=0; i< num_of_holes; i++) {
		char buf[30];
		time_t now = holes[i].start;
		strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
		debug_print("%s,", buf);
		now = holes[i].end;
		strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
		debug_print("%s ", buf);
	}
	debug_print("\n");
}

void pb_debug_print_file_holes(FILE_DATE_PAIR *holes, int num_of_holes) {
	debug_print(" - %d holes: ",num_of_holes);
	for (int i=0; i< num_of_holes; i++) {
		debug_print("%d,%d ", holes[i].offset, holes[i].length);
	}
	debug_print("\n");
}

void pb_debug_print_dir_req(unsigned char *data, int len) {
	DIR_REQ_HEADER *dir_header;
	dir_header = (DIR_REQ_HEADER *)(data + sizeof(AX25_HEADER));
	debug_print("DIR REQ: flags: %02x BLK_SIZE: %04x ", dir_header->flags & 0xff, dir_header->block_size &0xffff);
	if ((dir_header->flags & 0b11) == 0b00) {
		/* There is a holes list */
		int num_of_holes = get_num_of_dir_holes(len);
		if (num_of_holes == 0)
			debug_print("- missing hole list\n");
		else {
			DIR_DATE_PAIR *holes = get_dir_holes_list(data); //(DIR_DATE_PAIR *)(data + BROADCAST_REQUEST_HEADER_SIZE + DIR_REQUEST_HEADER_SIZE );
			pb_debug_print_dir_holes(holes, num_of_holes);
		}
	}
}

/**
 * pb_handle_file_request()
 *
 * Parse the data from a Broadcast File Request and add an entry on the PB.
 *
 * Returns EXIT_SUCCESS if the station was added to the PB, otherwise it
 * returns EXIT_FAILURE
 */
int pb_handle_file_request(char *from_callsign, unsigned char *data, int len) {
	// File Request
	int rc=EXIT_SUCCESS;
	int num_of_holes = 0;
	FILE_REQ_HEADER *file_header;
	file_header = (FILE_REQ_HEADER *)(data + sizeof(AX25_HEADER));

	//debug_print("FILE REQUEST: flags: %02x file: %04x BLK_SIZE: %04x\n", file_header->flags & 0xff, file_header->file_id &0xffff, file_header->block_size &0xffff);

	/* First, does the file exist */
	DIR_NODE * node = dir_get_node_by_id(file_header->file_id);
	if (node == NULL) {
		rc = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
		if (rc != EXIT_SUCCESS) {
			error_print("\n Error : Could not send ERR Response to TNC \n");
			//exit(EXIT_FAILURE);
		}
		return EXIT_FAILURE;
	}
	else {
	    // confirm it is really on Disk and we can read the size
		char file_name_with_path[MAX_FILE_PATH_LEN];
		dir_get_file_path_from_file_id(file_header->file_id,get_dir_folder(), file_name_with_path, MAX_FILE_PATH_LEN);
		FILE * f = fopen(file_name_with_path, "rb");
		if (f == NULL) {
			error_print("No file on disk, node in dir is wrong\n");
			rc = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send ERR Response to TNC \n");
			}
			return EXIT_FAILURE;
		}
		fseek(f, 0L, SEEK_END);
		int file_size = ftell(f);
		fclose(f);
		if (file_size == -1) {
			// We could not get the file size
			// TODO - we should either remove the file from the directory as well, or send a temporary error
			//        This will permanently mark the file as unavailable at the ground station, but it is still
			//        in the DIR.  Most likely the disk was full or another process held a lock
			rc = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
			if (rc != EXIT_SUCCESS) {
				debug_print("\n Error : Could not send ERR Response to TNC \n");
			}
			return EXIT_FAILURE;
		}
	}

	switch ((file_header->flags & 0b11)) {

	case PB_START_SENDING_FILE :
		/* least sig 2 bits of flags are 00 if this is a request to send a new file */
		// Add to the PB
		//debug_print(" - send whole file\n");
		if (pb_add_request(from_callsign, PB_FILE_REQUEST_TYPE, node, file_header->file_id, 0, 0, 0) == EXIT_SUCCESS) {
			// ACK the station
			rc = pb_send_ok(from_callsign);
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send OK Response to TNC \n");
				//exit(EXIT_FAILURE);
			}
		} else {
			// the protocol says NO -1 means temporary problem. e.g. shut and -2 means permanent
			rc = pb_send_err(from_callsign, PB_ERR_TEMPORARY); // shut or closed
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send ERR Response to TNC \n");
				//exit(EXIT_FAILURE);
			}
			return EXIT_FAILURE;
		}
		break;


	case PB_STOP_SENDING_FILE :
		/* A station can only stop a file broadcast if they started it */
		//debug_print(" - stop sending file\n");
		error_print("\n NOT IMPLEMENTED YET : Unable to handle a file download cancel request \n");
		return EXIT_FAILURE;
		break;

	case PB_FILE_HOLE_LIST :
		/* Process the hole list for the file */
		num_of_holes = get_num_of_file_holes(len);
		if (num_of_holes < 1 || num_of_holes > AX25_MAX_DATA_LEN / sizeof(FILE_DATE_PAIR)) {
			/* This does not have a valid holes list */
			rc = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
			if (rc != EXIT_SUCCESS) {
				error_print("Error : Could not send ERR Response to TNC \n");
				//exit(EXIT_FAILURE);
			}
			return EXIT_FAILURE;
		}
		FILE_DATE_PAIR * holes = get_file_holes_list(data);
		//pb_debug_print_file_holes(holes, num_of_holes);
		/* We could check the integrity of the holes list.  The offset should be inside the file length
		 * but note that the ground station can just give FFFF as the upper length for a hole*/
//		for (int i=0; i < num_of_holes; i++) {
//			if (holes[i].offset >= node->pfh->fileSize) {
//				/* This does not have a valid holes list */
//				rc = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
//				if (rc != EXIT_SUCCESS) {
//					error_print("\n Error : Could not send ERR Response to TNC \n");
//					//exit(EXIT_FAILURE);
//				}
//				return EXIT_FAILURE;
//			}
//		}
		if (pb_add_request(from_callsign, PB_FILE_REQUEST_TYPE, node, file_header->file_id, 0, holes, num_of_holes) == EXIT_SUCCESS) {
			// ACK the station
			rc = pb_send_ok(from_callsign);
			if (rc != EXIT_SUCCESS) {
				error_print("Error : Could not send OK Response to TNC \n");
				//exit(EXIT_FAILURE);
			}
		} else {
			return EXIT_FAILURE;
		}
		break;


	default :
		rc = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
		if (rc != EXIT_SUCCESS) {
			error_print("Error : Could not send ERR Response to TNC \n");
			//exit(EXIT_FAILURE);
		}
		return EXIT_FAILURE;
		break;

	}
	return rc;
}

/**
 * pb_handle_command()
 *
 * Send a received command to the command task.
 *
 * Returns EXIT_SUCCESS if it could be processed, otherwise it returns EXIT_FAILURE
 *
 */
int pb_handle_command(char *from_callsign, unsigned char *data, int len) {
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
			return EXIT_FAILURE;
		}
		if (cmd_rc == EXIT_DUPLICATE) {
			int rc = pb_send_ok(from_callsign);
			if (rc != EXIT_SUCCESS) {
				debug_print("\n Error : Could not send OK Response to TNC \n");
			}
			return EXIT_SUCCESS; // Duplicate
		}

//		debug_print("Auth Command\n");

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

				if (folder_id == FolderDir) {
					debug_print("Error - cant install into Directory\n");
					int r = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}

				DIR_NODE *node = dir_get_node_by_id(file_id);
				if (node == NULL) {
					error_print("File %d not available\n",file_id);
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
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}

				//debug_print("Install File: %04x : %s into dir: %d - %s | File Name:%d\n",*arg0, source_file, *arg1, dest_file, *arg2);
				if (pfh_extract_file_and_update_keywords(node->pfh, folder, true) != EXIT_SUCCESS) {
					debug_print("Error extracting file into %s\n",folder);
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}

				int rc = pb_send_ok(from_callsign);
				if (rc != EXIT_SUCCESS) {
					debug_print("\n Error : Could not send OK Response to TNC \n");
				}

				/* We updated the PACSAT dir. Reload. */
				dir_load();

				//dir_debug_print(NULL);

				break;
			}
			case SWCmdPacsatDeleteFile: {
//				debug_print("Arg: %02x %02x\n",sw_command->comArg.arguments[0],sw_command->comArg.arguments[1]);
				uint32_t file_id = sw_command->comArg.arguments[0] + (sw_command->comArg.arguments[1] << 16) ;
				uint16_t folder_id = sw_command->comArg.arguments[2];

				char *folder = get_folder_str(folder_id);
				if (folder == NULL) break;
				int is_directory_folder = false;
				if (folder_id == FolderDir)
					is_directory_folder = true;

				DIR_NODE *node = dir_get_node_by_id(file_id);
				if (node == NULL) {
					error_print("File %ld not available\n",file_id);
					int r = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}

				int rc = pb_delete_file_from_folder(node, folder, is_directory_folder);
				if (rc == EXIT_SUCCESS) {
					int rc = pb_send_ok(from_callsign);
					if (rc != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send OK Response to TNC \n");
					}
					node->pfh->uploadTime = time(0);
					if (pfh_update_pacsat_header(node->pfh, get_dir_folder()) != EXIT_SUCCESS) {
						debug_print("** Failed to re-write header in file.\n");
					}

					/* We updated the PACSAT dir. Reload. */
					dir_load();
				} else {
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
					int r = pb_send_err(from_callsign, 1);
					if (r != EXIT_SUCCESS) {
						debug_print("\n Error : Could not send ERR Response to TNC \n");
					}
					break;
				}

				/* Send Ok here as command is valid and any other errors below are ignored. */
				int rc = pb_send_ok(from_callsign);
				if (rc != EXIT_SUCCESS) {
					debug_print("\n Error : Could not send OK Response to TNC \n");
				}

				int is_directory_folder = false;
				if (folder_id == FolderDir)
					is_directory_folder = true;

				DIR_NODE *node;
				DIR_NODE *next_node = NULL;
				time_t now = time(0);

				while(node != NULL) {
					node = dir_get_pfh_by_folder_id(folder, next_node );
					if (node != NULL) {
						/* We have a header installed in this folder */
						//debug_print("Removing: File id %d from folder %s\n", node->pfh->fileId, folder);
						pb_delete_file_from_folder(node, folder, is_directory_folder);
						node->pfh->uploadTime = now++;
						if (pfh_update_pacsat_header(node->pfh, get_dir_folder()) != EXIT_SUCCESS) {
							debug_print("** Failed to re-write header in file.\n");
						}
						if (node->next == NULL) {
							break; // we are at end of dir
						} else {
							next_node = node->next;
						}
					}
				}

				// Purge all other files
				if (purge_orphan_files) {
					char dir_folder[MAX_FILE_PATH_LEN];
					strlcpy(dir_folder, get_data_folder(), MAX_FILE_PATH_LEN);
					strlcat(dir_folder, "/", MAX_FILE_PATH_LEN);
					strlcat(dir_folder, folder, MAX_FILE_PATH_LEN);
					//debug_print("Purging remaining files from: %s\n",dir_folder);
					DIR * d = opendir(dir_folder);
					if (d == NULL) {
						error_print("** Could not open dir: %s\n",dir_folder);
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
				}

				/* We update the PACSAT dir. Reload. */
				dir_load();
				break;
			}

			default:
				error_print("\n Error : Unknown pacsat command: %d\n",sw_command->comArg.command);
				int r = pb_send_err(from_callsign, 4);
				if (r != EXIT_SUCCESS) {
					debug_print("\n Error : Could not send ERR Response to TNC \n");
				}

				return EXIT_FAILURE;
				break;
		}
		return EXIT_SUCCESS;
}

int pb_delete_file_from_folder(DIR_NODE *node, char *folder, int is_directory_folder) {
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
			strlcat(dest_file, ".", MAX_FILE_PATH_LEN);
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
	if (remove(dest_file) == EXIT_SUCCESS) {
		/* If successful we change the header to remove the keyword for the installed dir and set the upload date */
		pfh_remove_keyword(node->pfh, folder);
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}

}

/**
 * pb_next_action()
 *
 * When called take the next action for next station on the PB
 *
 * Returns EXIT_SUCCESS, even if we can not process the request.  Only returns
 * EXIT_FAILURE if something goes badly wrong, such as we can not send data to
 * the TNC
 *
 */
int pb_next_action() {
	int rc = EXIT_SUCCESS;

	//TODO - broadcast the number of bytes transmitted to BSTAT periodically so stations can calc efficiency

	/* First see if we need to send the status */
	time_t now = time(0);
	if (last_pb_status_time == 0) last_pb_status_time = now; // Initialize at startup

	if ((now - last_pb_status_time) > g_pb_status_period_in_seconds) {
		// then send the status
		rc = pb_send_status();
		if (rc != EXIT_SUCCESS) {
			error_print("Could not send PB status to TNC \n");
		}
//		char buffer[256];
//		pb_make_list_str(buffer, sizeof(buffer));
//		debug_print("%s | %d frames queued\n", buffer, tnc_get_frames_queued());
		last_pb_status_time = now;
		sent_pb_status = true;
	}

	//TODO - broadcast the number of bytes transmitted to BSTAT periodically so stations can calc efficiency

	/* Now process the next station on the PB if there is one and take its action */
	if (number_on_pb == 0) return EXIT_SUCCESS; // nothing to do

	if ((now - pb_list[current_station_on_pb].request_time) > g_pb_max_period_for_client_in_seconds) {
		/* This station has exceeded the time allowed on the PB */
		pb_remove_request(current_station_on_pb);
		/* If we removed a station then we don't want/need to increment the current station pointer */
		return EXIT_SUCCESS;
	}

	if (!g_run_self_test)
		if (tnc_busy()) return EXIT_SUCCESS; /* TNC is Busy */

	/**
	 *  Process Request to broadcast directory
	 */
	if (pb_list[current_station_on_pb].pb_type == PB_DIR_REQUEST_TYPE) {

//		debug_print("Preparing DIR Broadcast for %s\n",pb_list[current_station_on_pb].callsign);
		if (pb_list[current_station_on_pb].hole_num < 1) {
			/* This is not a valid DIR Request.  There is no hole list.  We should not get here because this
			 * should not have been added.  So just remove it. */
			error_print("Invalid DIR request with no hole list from %s\n", pb_list[current_station_on_pb].callsign);
			pb_remove_request(current_station_on_pb);
			/* If we removed a station then we don't want/need to increment the current station pointer */
			return EXIT_SUCCESS;
		}

		/*
		 * We are processing a dir request, which we may be part way through.  There is a list of holes
		 * are we are at hole "current_hole_num", which is zero if we are just starting
		 * empty_hole_list is updated if we find at least one hole
		 */
		int current_hole_num = pb_list[current_station_on_pb].current_hole_num;
		DIR_DATE_PAIR *holes = pb_list[current_station_on_pb].hole_list;
		DIR_NODE *node = dir_get_pfh_by_date(holes[current_hole_num], pb_list[current_station_on_pb].node);
		if (node == NULL) {
			/* We have finished the broadcasts for this hole, or there were no records for the hole, move to the next hole if there is one. */
			pb_list[current_station_on_pb].current_hole_num++; /* Increment now.  If the data is bad and we can't make a frame, we want to move on to the next */
			if (pb_list[current_station_on_pb].current_hole_num == pb_list[current_station_on_pb].hole_num) {
				/* We have finished this hole list */
				//debug_print("Added last hole for request from %s\n", pb_list[current_station_on_pb].callsign);
				pb_remove_request(current_station_on_pb);
				/* If we removed a station then we don't want/need to increment the current station pointer */
				return EXIT_SUCCESS;
			} else {
				// debug_print("PB: No more files for this hole for request from %s\n", pb_list[current_station_on_pb].callsign);
				pb_list[current_station_on_pb].node = NULL; // next search will be from start of the DIR as we have no idea what the next hole may be
			}
		}
		else {
			/* We found a dir header */

			//debug_print("DIR BD Offset %d: ", pb_list[current_station_on_pb].offset);
			//pfh_debug_print(node->pfh);

			/* Store the offset and pass it into the function that makes the broadcast packet.  The offset after
			 * the broadcast is returned in this offset variable.  It equals the length of the PFH if the whole header
			 * has been broadcast. */
			int offset = pb_list[current_station_on_pb].offset;
			int data_len = pb_make_dir_broadcast_packet(node, packet_data_bytes, &offset);
			if (data_len == 0) {
				debug_print("ERROR: ** Could not create the test DIR Broadcast frame\n");
				/* To avoid a loop where we keep hitting this error, we remove the station from the PB */
				// TODO - this only occirs if we cant read from file system, so requested file is corrupt perhaps.
				// We can't now send ER as we already sent OK.  But can remove the entry from the dir and Mark as unavailable?
				pb_remove_request(current_station_on_pb);
				return EXIT_FAILURE;
			}

			/* Send the fill and finish */
			int rc = EXIT_SUCCESS;
			if (!g_run_self_test)
				send_raw_packet(g_broadcast_callsign, QST, PID_DIRECTORY, packet_data_bytes, data_len);
			//int rc = send_raw_packet('K', g_bbs_callsign, QST, PID_DIRECTORY, data_bytes, data_len);
			if (rc != EXIT_SUCCESS) {
				error_print("Could not send broadcast packet to TNC \n");
				/* To avoid a loop where we keep hitting this error, we remove the station from the PB */
				// TODO - we could not send the packet.  We should log/report it in telemetry
				pb_remove_request(current_station_on_pb);
				return EXIT_FAILURE;
			}

			/* check if we sent the whole PFH or if it is split into more than one broadcast */
			if (offset == node->pfh->bodyOffset) {
				/* Then we have sent this whole PFH */
				pb_list[current_station_on_pb].node = node->next; /* Store where we are in this broadcast of DIR fills */
				pb_list[current_station_on_pb].offset = 0; /* Reset this ready to send the next one */

				if (node->next == NULL) {
					/* There are no more records, we are at the end of the list, move to next hole if there is one */
					pb_list[current_station_on_pb].current_hole_num++;
					if (pb_list[current_station_on_pb].current_hole_num == pb_list[current_station_on_pb].hole_num) {
						/* We have finished this hole list */
//						debug_print("Added last hole for request from %s\n", pb_list[current_station_on_pb].callsign);
						pb_remove_request(current_station_on_pb);
						/* If we removed a station then we don't want/need to increment the current station pointer */
						return EXIT_SUCCESS;
					}
				}
			} else {
				pb_list[current_station_on_pb].offset = offset; /* Store the offset so we send the next part of the PFH next time */
			}
		}

	/**
	 *  Process Request to broadcast a file or parts of a file
	 */
	} else if (pb_list[current_station_on_pb].pb_type == PB_FILE_REQUEST_TYPE) {
//		debug_print("Preparing FILE Broadcast for %s\n",pb_list[current_station_on_pb].callsign);

		char psf_filename[MAX_FILE_PATH_LEN];
		dir_get_file_path_from_file_id(pb_list[current_station_on_pb].node->pfh->fileId,get_dir_folder(), psf_filename, MAX_FILE_PATH_LEN);

		if (pb_list[current_station_on_pb].hole_num == 0) {
			/* Request to broadcast the whole file */
			/* SEND THE NEXT CHUNK OF THE FILE BASED ON THE OFFSET */
			int number_of_bytes_read = pb_broadcast_next_file_chunk(pb_list[current_station_on_pb].node->pfh, psf_filename,
					pb_list[current_station_on_pb].offset, PB_FILE_DEFAULT_BLOCK_SIZE, pb_list[current_station_on_pb].node->pfh->fileSize);
			pb_list[current_station_on_pb].offset += number_of_bytes_read;
			if (number_of_bytes_read == 0) {
				pb_remove_request(current_station_on_pb);
				/* If we removed a station then we don't want/need to increment the current station pointer */
				return EXIT_SUCCESS;
			}

			/* If we are done then remove this request */
			if (pb_list[current_station_on_pb].offset >= pb_list[current_station_on_pb].node->pfh->fileSize) {
				pb_remove_request(current_station_on_pb);
				/* If we removed a station then we don't want/need to increment the current station pointer */
				return EXIT_SUCCESS;
			}
		} else { // there is a hole list

			/* Request to fill holes in the file */
			int current_hole_num = pb_list[current_station_on_pb].current_hole_num;
//			debug_print("Preparing Fill %d of %d from FILE %04x for %s --",(current_hole_num+1), pb_list[current_station_on_pb].hole_num,
//					pb_list[current_station_on_pb].node->pfh->fileId, pb_list[current_station_on_pb].callsign);

			FILE_DATE_PAIR *holes = pb_list[current_station_on_pb].hole_list;

			if (pb_list[current_station_on_pb].offset == 0) {
				/* Then this is probablly a new hole, initialize to the start of it */
				pb_list[current_station_on_pb].offset = holes[current_hole_num].offset;
			}
//			debug_print("  Chunk from %d length %d at offset %d\n",holes[current_hole_num].offset, holes[current_hole_num].length, pb_list[current_station_on_pb].offset);

			/* We are currently at byte pb_list[current_station_on_pb].offset for this request.  So this hole
			 * still has the following remaining bytes */
			int remaining_length_of_hole = holes[current_hole_num].offset + holes[current_hole_num].length - pb_list[current_station_on_pb].offset;

			int number_of_bytes_read = pb_broadcast_next_file_chunk(pb_list[current_station_on_pb].node->pfh, psf_filename,
					pb_list[current_station_on_pb].offset, remaining_length_of_hole, pb_list[current_station_on_pb].node->pfh->fileSize);
			pb_list[current_station_on_pb].offset += number_of_bytes_read;
			if (number_of_bytes_read == 0) {
				pb_remove_request(current_station_on_pb);
				/* If we removed a station then we don't want/need to increment the current station pointer */
				return EXIT_SUCCESS;
			}
			if (pb_list[current_station_on_pb].offset >= holes[current_hole_num].offset + holes[current_hole_num].length
					|| pb_list[current_station_on_pb].offset >= pb_list[current_station_on_pb].node->pfh->fileSize) {
				/* We have finished this hole, or we are at the end of the file */
				pb_list[current_station_on_pb].current_hole_num++;
				if (pb_list[current_station_on_pb].current_hole_num == pb_list[current_station_on_pb].hole_num) {
					/* We have finished the fole list */
					pb_remove_request(current_station_on_pb);
					/* If we removed a station then we don't want/need to increment the current station pointer */
					return EXIT_SUCCESS;
				} else {
					/* Move the offset to the start of the next hole */
					pb_list[current_station_on_pb].offset = holes[pb_list[current_station_on_pb].current_hole_num].offset;
				}
			}
		}
	}

	current_station_on_pb++;
	if (current_station_on_pb == number_on_pb)
		current_station_on_pb = 0;

	return rc;
}

/**
 * pb_braodcast_next_file_chunk()
 *
 * Broadcast a chunk of a file at a given offset with a given length.
 * At this point we already have the file on the PB, so we have validated
 * that it exists.  Any errors at this point are unrecoverable and should
 * result in the request being removed from the PB.
 *
 * Returns EXIT SUCCESS or the offset to be stored for the next transmission.
 * // TODO - we cant return EXIT_FAILURE here, but could return -ve number..
 */
int pb_broadcast_next_file_chunk(HEADER *pfh, char * psf_filename, int offset, int length, int file_size) {
	int rc = EXIT_SUCCESS;

	if (length > PB_FILE_DEFAULT_BLOCK_SIZE)
		length = PB_FILE_DEFAULT_BLOCK_SIZE;

	FILE * f = fopen(psf_filename, "r");
	if (f == NULL) {
		return EXIT_SUCCESS;
	}

	// TODO - this is where the logic would go to check the block size that the client sends and potentially use that

	if (fseek( f, offset, SEEK_SET ) != 0) {
		return EXIT_SUCCESS;
	}
	int number_of_bytes_read = fread(broadcast_buffer, sizeof(char), PB_FILE_DEFAULT_BLOCK_SIZE, f);
	//debug_print("Read %d bytes from %s\n", number_of_bytes_read, psf_filename);
	fclose(f);

	int chunk_includes_last_byte = false;

	if (offset + number_of_bytes_read >= file_size)
		chunk_includes_last_byte = true;

	//debug_print("FILE BB to send: ");
	//pfh_debug_print(pfh);

	int data_len = pb_make_file_broadcast_packet(pfh, packet_buffer, broadcast_buffer,
			number_of_bytes_read, offset, chunk_includes_last_byte);
	if (data_len == 0) {
		/* Hmm, something went badly wrong here.  We better remove this request or we will keep
		 * hitting this error.  It's unclear what went wrong do we mark the file as not available?
		 * Or just remove this request without an error?  But then the client will automatically
		 * request this file again.. */
		error_print("** Could not create the test DIR Broadcast frame\n");
		return EXIT_SUCCESS;
	}

	/* Send the broadcast and finish */
	if (!g_run_self_test)
		rc = send_raw_packet(g_broadcast_callsign, QST, PID_FILE, packet_buffer, data_len);
	if (rc != EXIT_SUCCESS) {
		error_print("Could not send broadcast packet to TNC \n");
		return EXIT_SUCCESS;
	}

	return number_of_bytes_read;
}

/**
 * pb_make_dir_broadcast_packet()
 *
 * Generate the bytes needed for a dir broadcast based on a pacsat file header
 * Pass in the Pacsat file header, a pointer to the broadcast frame, the offset
 * if this is the second frame for a long header
 *

      flags          A bit field as follows:

           7  6  5  4  3  2  1  0
          /----------------------\
          |*  N  E  0  V  V  T  T|
          \----------------------/
      TT                  Two bit frame type identifier
                          00   PFH broadcast
                          01   reserved
                          10   reserved
                          11   reserved

      VV                  Two bit version identifier.  This version is 00.

      0                   Always 0 indicates a server generated frame.

      E              1    Last byte of frame is the last byte of the directory PFH.
                     0    Not the last frame.

      N              1    This is the newest file on the server.
                     0    This is not the newest file on the server.

      *                   Reserved, always 0.


      file_id    A number which identifies the file.  All directory broadcast
      frames which are part of the same file's PFH are tagged with this number.

      offset     This is  the offset from the start of the PFH for the first data
      byte in this frame.BROADCAST_REQUEST_HEADER_SIZE

      t_old     Number of seconds since 00:00:00 1/1/80. See below.

      t_new     Number of seconds since 00:00:00 1/1/80. See below.

           There  are no files other than the file  identified  by
           file_id with t_old <= UPLOAD_TIME <= t_new.

      The data portion of a directory broadcast frame will contain all or part of
      the PACSAT File header from the file identified by <file_id>. The <offset>
      field indicates where the data from the current frame belongs in the PFH.

      An <offset> of 0 and a <flags> field with the E bit set to 1 indicates that
      this directory broadcast frame contains the entire PFH for the identified
      file.

      RETURNS the length of the data packet created

 */
int pb_make_dir_broadcast_packet(DIR_NODE *node, unsigned char *data_bytes, int *offset) {
	int length = 0;

	PB_DIR_HEADER dir_broadcast;
	char flag = 0;
	///////////////////////// TODO - some logic here to set the E bit if this is the entire PFH otherwise deal with offset etc
	if (node->pfh->bodyOffset < MAX_DIR_PFH_LENGTH) {
		flag |= 1UL << E_BIT; // Set the E bit, All of this header is contained in the broadcast frame
	}
	dir_broadcast.offset = *offset;
	dir_broadcast.flags = flag;
	dir_broadcast.file_id = node->pfh->fileId;

	/* The dates guarantee:
	 "There   are  no  files  other  than  this  file   with
      t_old <= UPLOAD_TIME <= t_new"

      Practically speaking
      t_old is 1 second after the upload time of the prev file
      t_new is 1 second before the upload time of the next file
     */
	if (node->prev != NULL)
		dir_broadcast.t_old = node->prev->pfh->uploadTime + 1;
	else
		dir_broadcast.t_old = 0;
	if (node->next != NULL)
		dir_broadcast.t_new = node->next->pfh->uploadTime - 1;
	else {
		dir_broadcast.t_new = node->pfh->uploadTime; // no files past this one so use its own uptime for now
		flag |= 1UL << N_BIT; /* Set the N bit to say this is the newest file on the server */
	}

	char psf_filename[MAX_FILE_PATH_LEN];
	dir_get_file_path_from_file_id(node->pfh->fileId,get_dir_folder(), psf_filename, MAX_FILE_PATH_LEN);
	FILE * f = fopen(psf_filename, "r");
	if (f == NULL) {
		return 0;
	}
	int buffer_size = node->pfh->bodyOffset - *offset;  /* This is how much we have left to read */
	if (buffer_size <= 0) return 0; /* This is a failure as we return length 0 */
	if (buffer_size >= MAX_DIR_PFH_LENGTH) {
		/* If we have an offset then we have already sent part of this, send the next part */
		buffer_size = MAX_DIR_PFH_LENGTH;
	}

	if (*offset != 0)
		if (fseek( f, *offset, SEEK_SET ) != 0) {
			return 0; /* This is a failure as we return length 0 */
		}
	int num = fread(packet_buffer, sizeof(char), buffer_size, f);
	if (num != buffer_size) {
		fclose(f);
		return 0; // Error with the read
	}
	fclose(f);
	*offset = *offset + num;
	/* Copy the bytes into the frame */
	unsigned char *header = (unsigned char *)&dir_broadcast;
	for (int i=0; i<sizeof(PB_DIR_HEADER);i++ )
		data_bytes[i] = header[i];
	for (int i=0; i<num; i++)
		data_bytes[i+sizeof(PB_DIR_HEADER)] = packet_buffer[i];

	length = sizeof(PB_DIR_HEADER) + num +2;
	int checksum = gen_crc(data_bytes, length-2);
	//debug_print("crc: %04x\n",checksum);

	/* Despite everything being little endian, the CRC needs to be in network byte order, or big endian */
	unsigned char one = (unsigned char)(checksum & 0xff);
	unsigned char two = (unsigned char)((checksum >> 8) & 0xff);
	data_bytes[length-1] = one;
	data_bytes[length-2] = two;

//	if (check_crc(data_bytes, length+2) != 0) {
//		error_print("CRC does not match\n");
//		return 0;
//	}
//	debug_print("\n%02x %02x crc check: %04x\n",one, two, checksum);
//	for (int i=0; i< length; i++) {
//			printf("%02x ",data_bytes[i]);
//			if (i%8 == 0 && i!=0) printf("\n");
//	}

	return length; // return length of header + pfh + crc
}

/**
 * pb_make_file_broadcast_packet()
 *
 *  Returns packet in unsigned char *data_bytes

flags          A bit field as follows:

     7  6  5  4  3  2  1  0
    /----------------------\
    |*  *  E  0  V  V  Of L|
    \----------------------/

L              1    length field is present
               0    length field not present

Of             1    offset is a byte offset from the beginning of the file.
               0    offset is a block number (not currently used).

VV                  Two bit version identifier.  This version is 0.

E              1    Last byte of frame is the last byte of the file.
               0    Not last.

0                   Always 0.

*                   Reserved, must be 0.
 */
int pb_make_file_broadcast_packet(HEADER *pfh, unsigned char *data_bytes,
		unsigned char *buffer, int number_of_bytes_read, int offset, int chunk_includes_last_byte) {
	int length = 0;
	PB_FILE_HEADER file_broadcast_header;

	char flag = 0;
	if (chunk_includes_last_byte) {
		flag |= 1UL << E_BIT; // Set the E bit, this is the last chunk of this file
	}
	file_broadcast_header.offset = offset;
	file_broadcast_header.flags = flag;
	file_broadcast_header.file_id = pfh->fileId;

	/* Copy the bytes into the frame */
	unsigned char *header = (unsigned char *)&file_broadcast_header;
	for (int i=0; i<sizeof(PB_FILE_HEADER);i++ )
		data_bytes[i] = header[i];
	for (int i=0; i<number_of_bytes_read; i++)
		data_bytes[i+sizeof(PB_FILE_HEADER)] = buffer[i];

	length = sizeof(PB_FILE_HEADER) + number_of_bytes_read +2;
	int checksum = gen_crc(data_bytes, length-2);
	//debug_print("crc: %04x\n",checksum);

	/* Despite everything being little endian, the CRC needs to be in network byte order, or big endian */
	unsigned char one = (unsigned char)(checksum & 0xff);
	unsigned char two = (unsigned char)((checksum >> 8) & 0xff);
	data_bytes[length-1] = one;
	data_bytes[length-2] = two;

	return length; // return length of header + pfh + crc
}

/**
 * Return true if this file is in use by the PB
 */
int pb_is_file_in_use(uint32_t file_id) {
    int i;
    for (i=0; i < number_on_pb; i++) {
    	if (pb_list[i].node != NULL)
    		if (pb_list[i].node->pfh != NULL)
    			if (pb_list[i].node->pfh->fileId == file_id)
    				return true;
    }
    return false;
}

/*********************************************************************************************
 *
 * SELF TESTS FOLLOW
 *
 */
int test_pb_list() {
	printf("##### TEST PB LIST\n");
	int rc = EXIT_SUCCESS;

	char data[] = {0x25,0x9f,0x3d,0x63,0xff,0xff,0xff,0x7f};
	DIR_DATE_PAIR * holes = (DIR_DATE_PAIR *)&data;

	rc = pb_add_request("AC2CZ", PB_FILE_REQUEST_TYPE, NULL, 3, 0, NULL, 0);
	if (rc != EXIT_SUCCESS) {printf("** Could not add callsign\n"); return EXIT_FAILURE; }
	rc = pb_add_request("VE2XYZ", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0);
	if (rc != EXIT_SUCCESS) {printf("** Could not add callsign\n"); return EXIT_FAILURE; }
	pb_debug_print_list();
	if (strcmp(pb_list[0].callsign, "AC2CZ") != 0) {printf("** Mismatched callsign 0\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[1].callsign, "VE2XYZ") != 0) {printf("** Mismatched callsign 1\n"); return EXIT_FAILURE;}

	// Now remove the head
	debug_print("REMOVE HEAD\n");
	rc = pb_remove_request(0);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	pb_debug_print_list();
	if (strcmp(pb_list[0].callsign, "VE2XYZ") != 0) {printf("** Mismatched callsign 0 after head removed\n"); return EXIT_FAILURE;}

	debug_print("ADD two more Calls\n");
	rc = pb_add_request("G0KLA", PB_FILE_REQUEST_TYPE, NULL, 3, 0, NULL, 0);
	if (rc != EXIT_SUCCESS) {printf("** Could not add callsign\n"); return EXIT_FAILURE; }
	rc = pb_add_request("WA1QQQ", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0);
	if (rc != EXIT_SUCCESS) {printf("** Could not add callsign\n"); return EXIT_FAILURE; }
	pb_debug_print_list();

	rc = pb_remove_request(0);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request 1\n"); return EXIT_FAILURE; }
	rc = pb_remove_request(0);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request 2\n"); return EXIT_FAILURE; }
	rc = pb_remove_request(0);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request 3\n"); return EXIT_FAILURE; }
	// Test Remove when empty, should do nothing
	rc = pb_remove_request(0);
	if (rc != EXIT_FAILURE) {printf("** Did not receive error message for remove request 4\n"); return EXIT_FAILURE; }
	rc = EXIT_SUCCESS; /* Reset rc after the failure test above*/

	pb_debug_print_list();

	DIR_NODE test_node;
	HEADER pfh;
	pfh.fileId = 3;
	pfh.bodyOffset = 36;
	pfh.fileSize = 175;
	test_node.pfh = &pfh;

	// Test PB Full
	debug_print("ADD Calls and test FULL\n");
	if( pb_add_request("A1A", PB_DIR_REQUEST_TYPE, NULL, 0, 0, holes, 1) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("B1B", PB_FILE_REQUEST_TYPE, NULL, 3, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("C1C", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("D1D", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("E1E", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("F1F", PB_FILE_REQUEST_TYPE, &test_node, 3, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("G1G", PB_FILE_REQUEST_TYPE, NULL, 3, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("H1H", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("I1I", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("J1J", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("K1K", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_FAILURE) {debug_print("ERROR: Added call to FULL PB list\n");return EXIT_FAILURE; }

	if (strcmp(pb_list[0].callsign, "A1A") != 0) {printf("** Mismatched callsign 0\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[1].callsign, "B1B") != 0) {printf("** Mismatched callsign 1\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[2].callsign, "C1C") != 0) {printf("** Mismatched callsign 2\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[3].callsign, "D1D") != 0) {printf("** Mismatched callsign 3\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[4].callsign, "E1E") != 0) {printf("** Mismatched callsign 4\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[5].callsign, "F1F") != 0) {printf("** Mismatched callsign 5\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[6].callsign, "G1G") != 0) {printf("** Mismatched callsign 6\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[7].callsign, "H1H") != 0) {printf("** Mismatched callsign 7\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[8].callsign, "I1I") != 0) {printf("** Mismatched callsign 8\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[9].callsign, "J1J") != 0) {printf("** Mismatched callsign 9\n"); return EXIT_FAILURE;}

	pb_debug_print_list();
	debug_print("TEST File 3 in use\n");
	if (!pb_is_file_in_use(3)) {
		debug_print("ERROR: File 3 is not in use when it should be \n");
		return EXIT_FAILURE;
	}

	debug_print("Process Current Call\n");
	if (pb_next_action() != EXIT_SUCCESS) { printf("** Could not take next PB action\n"); return EXIT_FAILURE; }
	pb_debug_print_list();
	debug_print("With current_station_on_pb = %d\n",current_station_on_pb);
	if (strcmp(pb_list[current_station_on_pb].callsign, "B1B") != 0) {printf("** Mismatched callsign current call\n"); return EXIT_FAILURE;}

//	debug_print("Remove head\n");
//	// Remove 0 as though it was done
//	rc = pb_remove_request(0); // Head
//	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
//	if (strcmp(pb_list[current_station_on_pb].callsign, "B1B") != 0) {printf("** Mismatched callsign current call after remove head\n"); return EXIT_FAILURE;}

	debug_print("Remove 3\n");
	// Remove 3 as though it timed out
	rc = pb_remove_request(3); // Now E1E
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	if (strcmp(pb_list[current_station_on_pb].callsign, "B1B") != 0) {printf("** Mismatched callsign current call after remove 3\n"); return EXIT_FAILURE;}
    if (strcmp(pb_list[3].callsign, "F1F") != 0) {printf("** Mismatched callsign 3: %s\n",pb_list[3].callsign); return EXIT_FAILURE;}

	 /* Also confirm that the node copied over correctly */
	if (pb_list[3].node->pfh->fileId != 3) {printf("** Mismatched file id for entry 3\n"); return EXIT_FAILURE;}
	if (pb_list[3].node->pfh->bodyOffset != 36) {printf("** Mismatched body offset for entry 3\n"); return EXIT_FAILURE;}
	if (pb_list[3].node->pfh->fileSize != 175) {printf("** Mismatched file size of %d for entry 3\n",pb_list[3].node->pfh->fileSize); return EXIT_FAILURE;}
	if (strcmp(pb_list[6].callsign, "I1I") != 0) {printf("** Mismatched callsign 6: %s\n",pb_list[6].callsign); return EXIT_FAILURE;}

	debug_print("Remove current station\n");
	// Remove the current station, which is also the head, should advance to next one
	rc = pb_remove_request(current_station_on_pb);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	if (strcmp(pb_list[current_station_on_pb].callsign, "C1C") != 0) {printf("** Mismatched callsign current call after remove current station\n"); return EXIT_FAILURE;}

	pb_debug_print_list();

	debug_print("Remove 7 stations\n");
	rc = pb_remove_request(current_station_on_pb);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	rc = pb_remove_request(current_station_on_pb);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	rc = pb_remove_request(current_station_on_pb);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	rc = pb_remove_request(current_station_on_pb);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	rc = pb_remove_request(current_station_on_pb);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	rc = pb_remove_request(current_station_on_pb);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	rc = pb_remove_request(current_station_on_pb);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	pb_debug_print_list();

	if (rc == EXIT_SUCCESS)
		printf("##### TEST PB LIST: success\n");
	else
		printf("##### TEST PB LIST: fail\n");
	return rc;
}

int test_pb() {
	printf("##### TEST PACSAT BROADCAST:\n");
	int rc = EXIT_SUCCESS;
	mkdir("/tmp/pacsat",0777);

	debug_print("LOAD DIR\n");
	if (dir_init("/tmp") != EXIT_SUCCESS) { printf("** Could not initialize the dir\n"); return EXIT_FAILURE; }
	dir_load();

	// Add AC2CZ with a DIR request
	debug_print("ADD AC2CZ dir request\n");

	/* Test frame from AC2CZ requesting the whole dir with one hole */
	unsigned char data[] = {0x00,0xa0,0x8c,0xa6,0x66,0x40,0x40,0xf6,0x82,0x86,0x64,0x86,0xb4,0x40,
			0x61,0x03,0xbd,0x10,0xf4,0x00,0x25,0x9f,0x3d,0x63,0xff,0xff,0xff,0x7f};

	int num_of_holes = get_num_of_dir_holes(sizeof(data));
	if (num_of_holes != 1)  { printf("** Number of holes is wrong\n"); return EXIT_FAILURE; }
	DIR_DATE_PAIR * holes = get_dir_holes_list(data);

	rc = pb_add_request("AC2CZ", PB_DIR_REQUEST_TYPE, NULL, 0, 0, holes, num_of_holes);
	debug_print("List at start:\n");
	pb_debug_print_list();

	int i;
	for (i=0; i<10; i++) {
		debug_print("ACTION: %d\n",i);
		if (pb_next_action() != EXIT_SUCCESS) { printf("** Could not take next PB action\n"); return EXIT_FAILURE; }
	}

	debug_print("List at end of actions:\n");
	pb_debug_print_list();
	if (number_on_pb > 0) { printf("** Request left on PB after processing it\n"); return EXIT_FAILURE; }

	dir_free();

	if (rc == EXIT_SUCCESS)
		printf("##### TEST PACSAT BROADCAST: success\n");
	else
		printf("##### TEST PACSAT BROADCAST: fail\n");
	return rc;
}

int test_pb_file() {
	printf("##### TEST PACSAT FILE BB:\n");
	int rc = EXIT_SUCCESS;

	unsigned char data[] = {0x00,0xa0,0x8c,0xa6,0x66,0x40,0x40,0xf6,0x82,0x86,0x64,0x86,
		0xb4,0x40,0x61,0x03,0xbb,0x10,0x01,0x00,0x00,0x00,0xf4,0x00};

	debug_print("ADD AC2CZ file request when no file available\n");
	pb_handle_file_request("AC2CZ", data, sizeof(data));
	pb_debug_print_list();
	/* Confirm not added to PB */
	if (number_on_pb > 0) { printf("** Added to PB when no file available\n"); return EXIT_FAILURE; }

	mkdir("/tmp/pacsat",0777);

	debug_print("LOAD DIR\n");
	if (dir_init("/tmp") != EXIT_SUCCESS) { printf("** Could not initialize the dir\n"); return EXIT_FAILURE; }
	dir_load();

	pb_handle_file_request("AC2CZ", data, sizeof(data));
	pb_debug_print_list();
	if (strcmp(pb_list[0].callsign, "AC2CZ") != 0) {printf("** Mismatched callsign AC2CZ\n"); return EXIT_FAILURE;}
	if (pb_list[0].node->pfh->fileId != 1) {printf("** Mismatched file id\n"); return EXIT_FAILURE;}
	if (pb_list[0].pb_type != PB_FILE_REQUEST_TYPE) {printf("** Mismatched req type\n"); return EXIT_FAILURE;}
	if (pb_list[0].offset != 0) {printf("** Mismatched offset\n"); return EXIT_FAILURE;}

	/* One or two chunks to send the file */
	if (pb_next_action() != EXIT_SUCCESS) { printf("** Could not take next PB action\n"); return EXIT_FAILURE; }
	if (pb_next_action() != EXIT_SUCCESS) { printf("** Could not take next PB action\n"); return EXIT_FAILURE; }
	pb_debug_print_list();
	if (number_on_pb > 0) { printf("** Request left on PB after processing it\n"); return EXIT_FAILURE; }
	//unsigned char data_bytes[AX25_MAX_DATA_LEN];

	dir_free();

	if (rc == EXIT_SUCCESS)
		printf("##### TEST PACSAT FILE BB: success\n");
	else
		printf("##### TEST PACSAT FILE BB: fail\n");
	return rc;
}

int test_pb_file_holes() {
	printf("##### TEST PACSAT FILE HOLES:\n");
		int rc = EXIT_SUCCESS;

	/* This is a File Request from AC2CZ for 2 holes in file 2*/
	unsigned char data[] = {0x00, 0xa0, 0x8c, 0xa6, 0x66, 0x40, 0x40, 0xf6, 0x82, 0x86, 0x64, 0x86, 0xb4, 0x40, 0x61, 0x03, 0xbb,
	0x12, 0x02, 0x00, 0x00, 0x00, 0xf4, 0x00, 0x00, 0x00, 0x00, 0xa9, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x02};

	mkdir("/tmp/pacsat",0777);

	debug_print("LOAD DIR\n");
	if (dir_init("/tmp") != EXIT_SUCCESS) { printf("** Could not initialize the dir\n"); return EXIT_FAILURE; }
	dir_load();

	if (pb_handle_file_request("AC2CZ", data, sizeof(data))) { printf("** Could handle file hole request\n"); return EXIT_FAILURE;}
	pb_debug_print_list();
	if (strcmp(pb_list[0].callsign, "AC2CZ") != 0) {printf("** Mismatched callsign AC2CZ\n"); return EXIT_FAILURE;}
	if (pb_list[0].node->pfh->fileId != 2) {printf("** Mismatched file id\n"); return EXIT_FAILURE;}
	if (pb_list[0].pb_type != PB_FILE_REQUEST_TYPE) {printf("** Mismatched req type\n"); return EXIT_FAILURE;}
	if (pb_list[0].hole_num != 2) {printf("** Mismatched hole_num\n"); return EXIT_FAILURE;}
	if (pb_list[0].current_hole_num != 0) {printf("** Mismatched offset\n"); return EXIT_FAILURE;}
	if (pb_list[0].offset != 0) {printf("** Mismatched offset\n"); return EXIT_FAILURE;}

	if (rc == EXIT_SUCCESS)
		printf("##### TEST PACSAT FILE HOLES: success\n");
	else
		printf("##### TEST PACSAT FILE HOLES: fail\n");
	return rc;
}


