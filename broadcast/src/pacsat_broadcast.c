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

/* Program Include Files */
#include "config.h"
#include "debug.h"
#include "agw_tnc.h"
#include "pacsat_header.h"
#include "pacsat_broadcast.h"
#include "pacsat_dir.h"
#include "str_util.h"
#include "crc.h"

/* An entry on the PB list keeps track of the requester and where we are in the request process */
struct pb_entry {
	int pb_type; /* DIR or FILE request */
	char callsign[MAX_CALLSIGN_LEN];
	DIR_NODE *node; /* Pointer to the node that we should broadcast next if this is a DIR request */
	int file_id; /* File id of the file we are broadcasting if this is a file request */
	int offset; /* The current offset in the file we are broadcasting or the PFH we are transmitting */
	int block_size; /* The maximum size of broadcasts. THIS IS CURRENTLY IGNORED */
	void *hole_list; /* This is a DIR or FILE hole list */
	int hole_num; /* The number of holes from the request */
	int current_hole_num; /* The next hole number from the request that we should process when this one is done */
	time_t request_time; /* The time the request was received for timeout purposes */
};
typedef struct pb_entry PB_ENTRY;

/* Forward declarations */
int pb_send_status();
int pb_add_request(char *from_callsign, int type, DIR_NODE * node, int file_id, int offset, void *holes, int num_of_holes);
int pb_handle_dir_request(char *from_callsign, char *data, int len);
int pb_handle_file_request(char *from_callsign, char *data, int len);
void pb_make_list_str(char *buffer, int len);
int pb_make_dir_broadcast(DIR_NODE *node, unsigned char *data_bytes);
DIR_DATE_PAIR * get_dir_holes_list(char *data);
int get_num_of_dir_holes(int request_len);
int pb_make_file_broadcast(HEADER *pfh, unsigned char *data_bytes,
		unsigned char *buffer, int number_of_bytes_read, int offset, int chunk_includes_last_byte);
FILE_DATE_PAIR * get_file_holes_list(char *data);
int get_num_of_file_holes(int request_len);

void pb_debug_dir_req(char *data, int len);
void pb_debug_print_dir_holes(DIR_DATE_PAIR *holes, int num_of_holes);
void pb_debug_print_file_holes(FILE_DATE_PAIR *holes, int num_of_holes);
void pb_debug_print_list_item(int i);

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


static int number_on_pb = 0; /* This keeps track of how many stations are in the pb_list array */
static int current_station_on_pb = 0; /* This keeps track of which station we will send data to next */
int pb_shut = false;

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
	if (pb_shut) {
		unsigned char shut[] = "PB Closed.";
		int rc = send_K_packet(g_broadcast_callsign, PBSHUT, PID_NO_PROTOCOL, shut, sizeof(shut));
		return rc;
//	} else if (number_on_pb == MAX_PB_LENGTH -1) {
//		char full[] = "PB Full.";
//		int rc = send_ui_packet(g_bbs_callsign, PBFULL, 0xf0, full, sizeof(full));
//		return rc;
	} else  {
		char buffer[256];
		char * CALL = PBLIST;
		if (number_on_pb == MAX_PB_LENGTH -1) {
			CALL = PBFULL;
		}
		pb_make_list_str(buffer, sizeof(buffer));
		unsigned char command[strlen(buffer)]; // now put the list in a buffer of the right size
		strlcpy((char *)command, (char *)buffer,sizeof(command));
		int rc = send_K_packet(g_broadcast_callsign, CALL, PID_NO_PROTOCOL, command, sizeof(command));
		return rc;
	}
}

/**
 * pb_send_ok()
 *
 * Send a UI frame from the bbs callsign to the station with PID BB and the
 * text OK <callsign>0x0Drequest_list
 */
int pb_send_ok(char *from_callsign) {
	int rc = EXIT_SUCCESS;
	char buffer[4 + strlen(from_callsign)]; // OK + 10 char for callsign with SSID
	strlcpy(buffer,"OK ", sizeof(buffer));
	strlcat(buffer, from_callsign, sizeof(buffer));
	rc = send_K_packet(g_broadcast_callsign, from_callsign, PID_FILE, (unsigned char *)buffer, sizeof(buffer));

	//				// OK AC2CZ
	//				char pbd[] = {0x00, 0x82, 0x86, 0x64, 0x86, 0xB4, 0x40, 0x00, 0xA0, 0x8C, 0xA6,
	//								0x66, 0x40, 0x40, 0x17, 0x03, 0xBB, 0x4F, 0x4B, 0x20, 0x41, 0x43, 0x32, 0x43, 0x5A, 0x0D, 0xC0};
	//				//send_raw_packet('K', "PFS3-12", "AC2CZ", 0xbb, pbd, sizeof(pbd));
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
	rc = send_K_packet(g_broadcast_callsign, from_callsign, PID_FILE, (unsigned char *)buffer, sizeof(buffer));

	return rc;
}

/**
 * pb_add_request()
 *
 * Add a callsign and its request to the PB
 *
 * Make a copy of all the data because the original packet will be purged soon from the
 * circular buffer
 *
 * returns EXIT_SUCCESS it it succeeds or EXIT_FAILURE if the PB is shut or full
 *
 */
int pb_add_request(char *from_callsign, int type, DIR_NODE * node, int file_id, int offset, void *holes, int num_of_holes) {
	if (pb_shut) return EXIT_FAILURE;
	if (number_on_pb == MAX_PB_LENGTH -1) {
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
	pb_list[number_on_pb].file_id = file_id;
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
 */
int pb_remove_request(int pos) {
	if (number_on_pb == 0) return EXIT_FAILURE;
	if (pos > number_on_pb) return EXIT_FAILURE;
	if (pos == number_on_pb) {
		/* Remove the last item */
		free(pb_list[pos].hole_list);
		number_on_pb--;
		return EXIT_SUCCESS;
	}
	/* Otherwise remove the item and shuffle all the other items to the left */
	for (int i = pos + 1; i < number_on_pb; i++) {
		strlcpy(pb_list[i-1].callsign, pb_list[i].callsign, MAX_CALLSIGN_LEN);
		pb_list[i-1].pb_type = pb_list[i].pb_type;
		pb_list[i-1].file_id = pb_list[i].file_id;
		pb_list[i-1].offset = pb_list[i].offset;
		pb_list[i-1].request_time = pb_list[i].request_time;
		pb_list[i-1].hole_num = pb_list[i].hole_num;
		pb_list[i-1].node = pb_list[i].node;
		pb_list[i-1].current_hole_num = pb_list[i].current_hole_num;
		pb_list[i-1].hole_list = pb_list[i].hole_list;
	}
	free(pb_list[number_on_pb].hole_list);
	number_on_pb--;

	if (pos <= current_station_on_pb)
		current_station_on_pb--;
	if (current_station_on_pb < 0)
		current_station_on_pb = 0;
	return EXIT_SUCCESS;
}

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
	debug_print("%s Ty:%d File:%d Off:%d Holes:%d Cur:%d",pb_list[i].callsign,pb_list[i].pb_type,pb_list[i].file_id,
			pb_list[i].offset,pb_list[i].hole_num,pb_list[i].current_hole_num);
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
 *
 */
void pb_process_frame(char *from_callsign, char *to_callsign, char *data, int len) {
	if (strncasecmp(to_callsign, g_bbs_callsign, 7) == 0) {
		// this was sent to the BBS Callsign
		debug_print("BBS Request\n");

	}
	if (strncasecmp(to_callsign, g_broadcast_callsign, 7) == 0) {
		// this was sent to the Broadcast Callsign

		struct t_broadcast_request_header *broadcast_request_header;
		broadcast_request_header = (struct t_broadcast_request_header *)data;
		debug_print("pid: %02x \n", broadcast_request_header->pid & 0xff);
		if ((broadcast_request_header->pid & 0xff) == PID_DIRECTORY) {
			debug_print("DIR REQUEST\n");
			pb_handle_dir_request(from_callsign, data, len);
		}
		if ((broadcast_request_header->pid & 0xff) == PID_FILE) {
			// File Request
			debug_print("FILE REQUEST\n");
			pb_handle_file_request(from_callsign, data, len);
		}
	}
}



/**
 * pb_handle_dir_request()
 *
 * Process a dir request from a ground station
 *
 */
int pb_handle_dir_request(char *from_callsign, char *data, int len) {
	// Dir Request
	int rc=EXIT_SUCCESS;
	DIR_REQ_HEADER *dir_header;
	dir_header = (DIR_REQ_HEADER *)(data + sizeof(AX25_HEADER));

	/* least sig 2 bits of flags are 00 if this is a fill request */
	if ((dir_header->flags & 0b11) == 0b00) {
		pb_debug_dir_req(data, len);
		debug_print("DIR FILL REQUEST: flags: %02x BLK_SIZE: %04x\n", dir_header->flags & 0xff, dir_header->block_size &0xffff);

//		for (int i=0; i < len; i++) {
//			printf("0x%02x,",data[i] & 0xff);
//		}

		int num_of_holes = get_num_of_dir_holes(len);
		if (num_of_holes < 1 || num_of_holes > AX25_MAX_DATA_LEN / sizeof(DIR_DATE_PAIR)) {
			/* This does not have a valid holes list */
			rc = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send ERR Response to TNC \n");
				//exit(EXIT_FAILURE);
			}
			return EXIT_SUCCESS;
		}
		// Add to the PB
		DIR_DATE_PAIR * holes = get_dir_holes_list(data);
		if (pb_add_request(from_callsign, PB_DIR_REQUEST_TYPE, NULL, 0, 0, holes, num_of_holes) == EXIT_SUCCESS) {
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
		}
	} else {
		rc = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
		if (rc != EXIT_SUCCESS) {
			error_print("\n Error : Could not send ERR Response to TNC \n");
			//exit(EXIT_FAILURE);
		}
	}
	return rc;
}

int get_num_of_dir_holes(int request_len) {
	int num_of_holes = (request_len - sizeof(AX25_HEADER) - sizeof(DIR_REQ_HEADER)) / sizeof(DIR_DATE_PAIR);
	return num_of_holes;
}

DIR_DATE_PAIR * get_dir_holes_list(char *data) {
	DIR_DATE_PAIR *holes = (DIR_DATE_PAIR *)(data + sizeof(AX25_HEADER) + sizeof(DIR_REQ_HEADER) );
	return holes;
}


int get_num_of_file_holes(int request_len) {
	int num_of_holes = (request_len - sizeof(AX25_HEADER) - sizeof(FILE_REQ_HEADER)) / sizeof(FILE_DATE_PAIR);
	return num_of_holes;
}

FILE_DATE_PAIR * get_file_holes_list(char *data) {
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

void pb_debug_dir_req(char *data, int len) {
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
 * Returns EXIT_SUCCESS
 */
int pb_handle_file_request(char *from_callsign, char *data, int len) {
	// File Request
	int rc=EXIT_SUCCESS;
	FILE_REQ_HEADER *file_header;
	file_header = (FILE_REQ_HEADER *)(data + sizeof(AX25_HEADER));

	/////////// TODO - validate the packet here and send err -5 if it fails
	debug_print("FILE REQUEST: flags: %02x file: %04x BLK_SIZE: %04x\n", file_header->flags & 0xff, file_header->file_id &0xffff, file_header->block_size &0xffff);

	/* least sig 2 bits of flags are 00 if this is a request to send a new file */
	if ((file_header->flags & 0b11) == PB_START_SENDING_FILE) {
		/* Start Sending the file */
		//pb_debug_dir_req(data, len);

		// Add to the PB
		/* First, does the file exist */
		DIR_NODE * node = dir_get_node_by_id(file_header->file_id);
		if (node == NULL) {
			rc = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send ERR Response to TNC \n");
				//exit(EXIT_FAILURE);
			}
			return EXIT_SUCCESS;
		}

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
	} else if ((file_header->flags & 0b11) == PB_STOP_SENDING_FILE) {
		/* A station can only stop a file broadcast if they started it */
		error_print("\n NOT IMPLEMENTED YET : Unable to handle a file download cancel request \n");
		return EXIT_FAILURE;

	} else if ((file_header->flags & 0b11) == PB_FILE_HOLE_LIST) {
		/* First, does the file exist */
		DIR_NODE * node = dir_get_node_by_id(file_header->file_id);
		if (node == NULL) {
			rc = pb_send_err(from_callsign, PB_ERR_FILE_NOT_AVAILABLE);
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send ERR Response to TNC \n");
				//exit(EXIT_FAILURE);
			}
			return EXIT_SUCCESS;
		}

		int num_of_holes = get_num_of_file_holes(len);
		if (num_of_holes < 1 || num_of_holes > AX25_MAX_DATA_LEN / sizeof(FILE_DATE_PAIR)) {
			/* This does not have a valid holes list */
			rc = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send ERR Response to TNC \n");
				//exit(EXIT_FAILURE);
			}
			return EXIT_SUCCESS;
		}
		FILE_DATE_PAIR * holes = get_file_holes_list(data);

		if (pb_add_request(from_callsign, PB_FILE_REQUEST_TYPE, node, file_header->file_id, 0, holes, num_of_holes) == EXIT_SUCCESS) {
			// ACK the station
			rc = pb_send_ok(from_callsign);
			if (rc != EXIT_SUCCESS) {
				error_print("\n Error : Could not send OK Response to TNC \n");
				//exit(EXIT_FAILURE);
			}
		} else {
			return EXIT_FAILURE;
		}
	} else {
		rc = pb_send_err(from_callsign, PB_ERR_FILE_INVALID_PACKET);
		if (rc != EXIT_SUCCESS) {
			error_print("\n Error : Could not send ERR Response to TNC \n");
			//exit(EXIT_FAILURE);
		}
		return EXIT_FAILURE;
	}
	return rc;
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

	/* First see if we need to send the status */
	time_t now = time(0);
	if (now - last_pb_status_time > PB_STATUS_PERIOD_IN_SECONDS) {
		// then send the status
		rc = pb_send_status();
		if (rc != EXIT_SUCCESS) {
			error_print("Could not send PB status to TNC \n");
		}
		last_pb_status_time = now;
		sent_pb_status = true;
	}

	if (now - last_pb_frames_queued_time > 2) {
		debug_print("Frames Queued: %d\n", g_frames_queued);
		last_pb_frames_queued_time = now;
	}

	/* Now process the next station on the PB if there is one and take its action */
	if (number_on_pb == 0) return EXIT_SUCCESS; // nothing to do

	if (g_frames_queued > 5) return EXIT_SUCCESS; /* TNC is Busy */

	if (pb_list[current_station_on_pb].pb_type == PB_DIR_REQUEST_TYPE) {
		/**
		 *  Request to broadcast directory
		 */
		debug_print("Preparing DIR Broadcast for %s\n",pb_list[current_station_on_pb].callsign);
		if (pb_list[current_station_on_pb].hole_num < 1) {
			/* This is not a valid DIR Request.  There is no hole list.  We should not get here because this
			 * should not have been added.  So just remove it. */
			debug_print("Invalid DIR request with no hole list from %s\n", pb_list[current_station_on_pb].callsign);
			pb_remove_request(current_station_on_pb);
			/* If we removed a station then we don't want/need to increment the current station pointer */
			return EXIT_SUCCESS;
		}

		int current_hole_num = pb_list[current_station_on_pb].current_hole_num;
		DIR_DATE_PAIR *holes = pb_list[current_station_on_pb].hole_list;
		DIR_NODE *node = dir_get_pfh_by_date(holes[current_hole_num], pb_list[current_station_on_pb].node);
		if (node == NULL) {
			/* We have finished the broadcasts for this hole, or there were no records for the hole, move to the next hole if there is one. */
			// TODO - need to test this logic.  If we list 10 holes with no data and then 1 hole with data, then do we miss our turn on the PB 10 times?
			pb_list[current_station_on_pb].current_hole_num++; /* Increment now.  If the data is bad and we can't make a frame, we want to move on to the next */
			if (pb_list[current_station_on_pb].current_hole_num == pb_list[current_station_on_pb].hole_num) {
				/* We have finished this hole list */
				debug_print("Added last hole for request from %s\n", pb_list[current_station_on_pb].callsign);
				pb_remove_request(current_station_on_pb);
				/* If we removed a station then we don't want/need to increment the current station pointer */
				return EXIT_SUCCESS;
			}

			// TODO - What response if there were no PFHs at all for the request?  An error?

			//////////////////////////// IS THIS NO -5???

		}
		else {
			/* We found a header */

			pb_list[current_station_on_pb].node = node->next; /* Store where we are in this broadcast of DIR fills */
			if (node->next == NULL) {
				/* There are no more records, we are at the end of the list */
				////// TODO set the bit saying this is the last record in the dir AND remove station from PB?
			}
			debug_print("DIR BD to send: ");
			pfh_debug_print(node->pfh);
			unsigned char data_bytes[AX25_MAX_DATA_LEN];

			int data_len = pb_make_dir_broadcast(node, data_bytes);
			if (data_len == 0) {printf("** Could not create the test DIR Broadcast frame\n"); return EXIT_FAILURE; }

			/* Send the fill and finish */
			int rc = send_K_packet(g_bbs_callsign, QST, PID_DIRECTORY, data_bytes, data_len);
			//int rc = send_raw_packet('K', g_bbs_callsign, QST, PID_DIRECTORY, data_bytes, data_len);
			if (rc != EXIT_SUCCESS) {
				error_print("Could not send broadcast packet to TNC \n");
				return EXIT_FAILURE;
			}
			//sleep(1);
			if (node->next == NULL) {
				/* There are no more records, we are at the end of the list, move to next hole if there is one */
				pb_list[current_station_on_pb].current_hole_num++;
				if (pb_list[current_station_on_pb].current_hole_num == pb_list[current_station_on_pb].hole_num) {
					/* We have finished this hole list */
					debug_print("Added last hole for request from %s\n", pb_list[current_station_on_pb].callsign);
					pb_remove_request(current_station_on_pb);
					/* If we removed a station then we don't want/need to increment the current station pointer */
					return EXIT_SUCCESS;
				}
			}
		}

	} else if (pb_list[current_station_on_pb].pb_type == PB_FILE_REQUEST_TYPE) {
		debug_print("Preparing FILE Broadcast for %s\n",pb_list[current_station_on_pb].callsign);
		if (pb_list[current_station_on_pb].hole_num == 0) {
			/**
			 *  Request to broadcast the whole file
			 */

			// SEND THE NEXT CHUNK OF THE FILE BASED ON THE OFFSET
			char psf_filename[MAX_FILE_PATH_LEN];
			pfh_get_filename(pb_list[current_station_on_pb].node->pfh,get_dir_folder(), psf_filename, MAX_FILE_PATH_LEN);
			FILE * f = fopen(psf_filename, "r");
			if (f == NULL) {
				/* Send an error because the file is not available or does not exist.  But also mark this as
				 * done or we will continue to send this error */
				rc = pb_send_err(pb_list[current_station_on_pb].callsign, PB_ERR_FILE_NOT_AVAILABLE); // missing file
				if (rc != EXIT_SUCCESS) {
					error_print("\n Error : Could not send ERR Response to TNC \n");
					return EXIT_FAILURE;
				}
				pb_remove_request(current_station_on_pb);
				return EXIT_SUCCESS;
			}

			// TODO - this is where the logic would go to check the block size that the client sends and potentially use that
			unsigned char buffer[PB_FILE_DEFAULT_BLOCK_SIZE]; // This is the chunk we will send
			int chunk_includes_last_byte = false;

//			if (pb_list[current_station_on_pb].offset == 0) {
//				/* This is the first chunk of the file, which starts right after the Pacsat Header */
//				pb_list[current_station_on_pb].offset = pb_list[current_station_on_pb].node->pfh->bodyOffset;
//			}
			if (fseek( f, pb_list[current_station_on_pb].offset, SEEK_SET ) != 0) {
				/* Something went wrong with that.  Need to assume that we can't send the file */
				rc = pb_send_err(pb_list[current_station_on_pb].callsign, PB_ERR_FILE_NOT_AVAILABLE); // missing file
				if (rc != EXIT_SUCCESS) {
					error_print("\n Error : Could not send ERR Response to TNC \n");
					return EXIT_FAILURE;
				}
				pb_remove_request(current_station_on_pb);
				return EXIT_SUCCESS;
			}
			int number_of_bytes_read = fread(buffer, sizeof(char), PB_FILE_DEFAULT_BLOCK_SIZE, f);
			debug_print("Read %d bytes from %s\n", number_of_bytes_read, psf_filename);
			fclose(f);

			/* Store offset of the next chunk to send */
			if (pb_list[current_station_on_pb].offset + number_of_bytes_read >= pb_list[current_station_on_pb].node->pfh->fileSize)
				chunk_includes_last_byte = true;

			debug_print("FILE BB to send: ");
			pfh_debug_print(pb_list[current_station_on_pb].node->pfh);
			unsigned char data_bytes[AX25_MAX_DATA_LEN];

			int data_len = pb_make_file_broadcast(pb_list[current_station_on_pb].node->pfh, data_bytes, buffer,
					number_of_bytes_read, pb_list[current_station_on_pb].offset, chunk_includes_last_byte);
			if (data_len == 0) {
				/* Hmm, something went badly wrong here.  We better remove this request or we will keep
				 * hitting this error.  It's unclear what went wrong do we mark the file as not available?
				 * Or just remove this request without an error?  But then the client will automatically
				 * request this file again.. */
				error_print("** Could not create the test DIR Broadcast frame\n");
				rc = pb_send_err(pb_list[current_station_on_pb].callsign, PB_ERR_FILE_NOT_AVAILABLE); // missing file
				if (rc != EXIT_SUCCESS) {
					error_print("\n Error : Could not send ERR Response to TNC \n");
					return EXIT_FAILURE;
				}
				pb_remove_request(current_station_on_pb);
				return EXIT_SUCCESS;
			}

			/* Send the fill and finish */
			int rc = send_K_packet(g_bbs_callsign, QST, PID_FILE, data_bytes, data_len);
			if (rc != EXIT_SUCCESS) {
				error_print("Could not send broadcast packet to TNC \n");
				return EXIT_FAILURE;
			}
			//sleep(1);

			pb_list[current_station_on_pb].offset += number_of_bytes_read;

			/* If we are done then remove this request */
			if (chunk_includes_last_byte) {
				pb_remove_request(current_station_on_pb);
				/* If we removed a station then we don't want/need to increment the current station pointer */
				return EXIT_SUCCESS;
			}
		} else {
			/**
			 *  Request to fill holes in the file
			 */

			debug_print("Holes File Fill Not yet supported: %s\n", pb_list[current_station_on_pb].callsign);
			pb_remove_request(current_station_on_pb);
			/* If we removed a station then we don't want/need to increment the current station pointer */
			return EXIT_SUCCESS;
		}
	}

	current_station_on_pb++;
	if (current_station_on_pb == number_on_pb)
		current_station_on_pb = 0;

	return rc;
}

/**
 * pb_make_dir_broadcast()
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

 */
int pb_make_dir_broadcast(DIR_NODE *node, unsigned char *data_bytes) {
	int length = 0;

	PB_DIR_HEADER dir_broadcast;
	char flag = 0;
	///////////////////////// TODO - some logic here to set the E bit if this is the entire PFH otherwise deal with offset etc
	flag |= 1UL << E_BIT; // Set the E bit, All of this header is contained in the broadcast frame

	dir_broadcast.offset = 0;
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

	/* TODO - We could regenerate the PFH bytes here. That would save a disk access.  For now we load them from disk */
	char psf_filename[MAX_FILE_PATH_LEN];
	pfh_get_filename(node->pfh,get_dir_folder(), psf_filename, MAX_FILE_PATH_LEN);
	FILE * f = fopen(psf_filename, "r");
	if (f == NULL) {
		return 0;
	}
	unsigned char buffer[node->pfh->bodyOffset]; // This incluces the whole PFH
	// TODO - if this is too large for a Broadcast Response we are supposed to split into two
	int num = fread(buffer, sizeof(char), node->pfh->bodyOffset, f);
	if (num != node->pfh->bodyOffset) {
		fclose(f);
		return 0; // Error with the read
	}
	fclose(f);
	/* Copy the bytes into the frame */
	unsigned char *header = (unsigned char *)&dir_broadcast;
	for (int i=0; i<sizeof(PB_DIR_HEADER);i++ )
		data_bytes[i] = header[i];
	for (int i=0; i<num; i++)
		data_bytes[i+sizeof(PB_DIR_HEADER)] = buffer[i];

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
 * pb_make)file_broadcast()
 *
 *

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
int pb_make_file_broadcast(HEADER *pfh, unsigned char *data_bytes,
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


/*********************************************************************************************
 *
 * SELF TESTS FOLLOW
 */
int test_pb_list() {
	printf(" TEST PB LIST\n");
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
	debug_print("REMOVE head\n");
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

	// Test PB Full
	debug_print("ADD Calls and test FULL\n");
	if( pb_add_request("A1A", PB_DIR_REQUEST_TYPE, NULL, 0, 0, holes, 1) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("B1B", PB_FILE_REQUEST_TYPE, NULL, 3, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("C1C", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("D1D", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("E1E", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("F1F", PB_FILE_REQUEST_TYPE, NULL, 3, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("G1G", PB_FILE_REQUEST_TYPE, NULL, 3, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("H1H", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("I1I", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_SUCCESS) {debug_print("ERROR: Could not add call to PB list\n");return EXIT_FAILURE; }
	if( pb_add_request("J1J", PB_DIR_REQUEST_TYPE, NULL, 0, 0, NULL, 0) != EXIT_FAILURE) {debug_print("ERROR: Added call to FULL PB list\n");return EXIT_FAILURE; }

	if (strcmp(pb_list[0].callsign, "A1A") != 0) {printf("** Mismatched callsign 0\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[1].callsign, "B1B") != 0) {printf("** Mismatched callsign 1\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[2].callsign, "C1C") != 0) {printf("** Mismatched callsign 2\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[3].callsign, "D1D") != 0) {printf("** Mismatched callsign 3\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[4].callsign, "E1E") != 0) {printf("** Mismatched callsign 4\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[5].callsign, "F1F") != 0) {printf("** Mismatched callsign 5\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[6].callsign, "G1G") != 0) {printf("** Mismatched callsign 6\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[7].callsign, "H1H") != 0) {printf("** Mismatched callsign 7\n"); return EXIT_FAILURE;}
	if (strcmp(pb_list[8].callsign, "I1I") != 0) {printf("** Mismatched callsign 8\n"); return EXIT_FAILURE;}

	pb_debug_print_list();

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

	debug_print("Remove 5\n");
	// Remove 5 as though it timed out
	rc = pb_remove_request(5); // Now FIF
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	if (strcmp(pb_list[current_station_on_pb].callsign, "B1B") != 0) {printf("** Mismatched callsign current call after remove 5\n"); return EXIT_FAILURE;}

	debug_print("Remove current station\n");
	// Remove the current station, which is also the head, should advance to next one
	rc = pb_remove_request(current_station_on_pb);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove request\n"); return EXIT_FAILURE; }
	if (strcmp(pb_list[current_station_on_pb].callsign, "C1C") != 0) {printf("** Mismatched callsign current call after remove current station\n"); return EXIT_FAILURE;}

	pb_debug_print_list();

	debug_print("Remove 5 stations\n");
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
		printf(" TEST PB LIST: success\n");
	else
		printf(" TEST PB LIST: fail\n");
	return rc;
}

int test_pb() {
	printf(" TEST PACSAT BROADCAST:\n");
	int rc = EXIT_SUCCESS;

	debug_print("LOAD DIR\n");
	if (dir_init("/tmp/test_dir") != EXIT_SUCCESS) { printf("** Could not initialize the dir\n"); return EXIT_FAILURE; }
	dir_load();

	// Add AC2CZ with a DIR request
	debug_print("ADD AC2CZ dir request\n");

	/* Test frame from AC2CZ requesting the whole dir with one hole */
	char data[] = {0x00,0xa0,0x8c,0xa6,0x66,0x40,0x40,0xf6,0x82,0x86,0x64,0x86,0xb4,0x40,
			0x61,0x03,0xbd,0x10,0xf4,0x00,0x25,0x9f,0x3d,0x63,0xff,0xff,0xff,0x7f};

	int num_of_holes = get_num_of_dir_holes(sizeof(data));
	if (num_of_holes != 1)  { printf("** Number of holes is wrong\n"); return EXIT_FAILURE; }
	DIR_DATE_PAIR * holes = get_dir_holes_list(data);

	rc = pb_add_request("AC2CZ", PB_DIR_REQUEST_TYPE, NULL, 0, 0, holes, num_of_holes);
	pb_debug_print_list();

	if (pb_next_action() != EXIT_SUCCESS) { printf("** Could not take next PB action\n"); return EXIT_FAILURE; }
	if (pb_next_action() != EXIT_SUCCESS) { printf("** Could not take next PB action\n"); return EXIT_FAILURE; }
	if (pb_next_action() != EXIT_SUCCESS) { printf("** Could not take next PB action\n"); return EXIT_FAILURE; }
	if (pb_next_action() != EXIT_SUCCESS) { printf("** Could not take next PB action\n"); return EXIT_FAILURE; }
	if (pb_next_action() != EXIT_SUCCESS) { printf("** Could not take next PB action\n"); return EXIT_FAILURE; }

	dir_free();

	if (rc == EXIT_SUCCESS)
		printf(" TEST PACSAT BROADCAST: success\n");
	else
		printf(" TEST PACSAT BROADCAST: fail\n");
	return rc;
}

int test_pb_file() {
	printf(" TEST PACSAT FILE BB:\n");
	int rc = EXIT_SUCCESS;

	char data[] = {0x00,0xa0,0x8c,0xa6,0x66,0x40,0x40,0xf6,0x82,0x86,0x64,0x86,
		0xb4,0x40,0x61,0x03,0xbb,0x10,0x01,0x00,0x00,0x00,0xf4,0x00};

	debug_print("ADD AC2CZ file request when no file available\n");
	pb_handle_file_request("AC2CZ", data, sizeof(data));
	pb_debug_print_list();
	/* Confirm not added to PB */
	if (number_on_pb > 0) { printf("** Added to PB when no file available\n"); return EXIT_FAILURE; }

	debug_print("LOAD DIR\n");
	if (dir_init("/tmp/test_dir") != EXIT_SUCCESS) { printf("** Could not initialize the dir\n"); return EXIT_FAILURE; }
	dir_load();

	pb_handle_file_request("AC2CZ", data, sizeof(data));
	pb_debug_print_list();
	if (strcmp(pb_list[0].callsign, "AC2CZ") != 0) {printf("** Mismatched callsign AC2CZ\n"); return EXIT_FAILURE;}
	if (pb_list[0].file_id != 1) {printf("** Mismatched file id\n"); return EXIT_FAILURE;}
	if (pb_list[0].pb_type != PB_FILE_REQUEST_TYPE) {printf("** Mismatched req type\n"); return EXIT_FAILURE;}
	if (pb_list[0].offset != 0) {printf("** Mismatched offset\n"); return EXIT_FAILURE;}

	if (pb_next_action() != EXIT_SUCCESS) { printf("** Could not take next PB action\n"); return EXIT_FAILURE; }
	pb_debug_print_list();
	//unsigned char data_bytes[AX25_MAX_DATA_LEN];

	dir_free();

	if (rc == EXIT_SUCCESS)
		printf(" TEST PACSAT FILE BB: success\n");
	else
		printf(" TEST PACSAT FILE BB: fail\n");
	return rc;
}

int test_pb_file_holes() {
	printf(" TEST PACSAT FILE HOLES:\n");
		int rc = EXIT_SUCCESS;

	char data[] = {0x00, 0xa0, 0x8c, 0xa6, 0x66, 0x40, 0x40, 0xf6, 0x82, 0x86, 0x64, 0x86, 0xb4, 0x40, 0x61, 0x03, 0xbb,
	0x12, 0x01, 0x00, 0x00, 0x00, 0xf4, 0x00, 0x00, 0x00, 0x00, 0xa9, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x02};

	debug_print("LOAD DIR\n");
	if (dir_init("/tmp/test_dir") != EXIT_SUCCESS) { printf("** Could not initialize the dir\n"); return EXIT_FAILURE; }
	dir_load();

	pb_handle_file_request("AC2CZ", data, sizeof(data));
	pb_debug_print_list();
	if (strcmp(pb_list[0].callsign, "AC2CZ") != 0) {printf("** Mismatched callsign AC2CZ\n"); return EXIT_FAILURE;}
	if (pb_list[0].file_id != 1) {printf("** Mismatched file id\n"); return EXIT_FAILURE;}
	if (pb_list[0].pb_type != PB_FILE_REQUEST_TYPE) {printf("** Mismatched req type\n"); return EXIT_FAILURE;}
	if (pb_list[0].hole_num != 1) {printf("** Mismatched offset\n"); return EXIT_FAILURE;}
	if (pb_list[0].current_hole_num != 0) {printf("** Mismatched offset\n"); return EXIT_FAILURE;}
	if (pb_list[0].offset != 0) {printf("** Mismatched offset\n"); return EXIT_FAILURE;}

	if (rc == EXIT_SUCCESS)
		printf(" TEST PACSAT FILE HOLES: success\n");
	else
		printf(" TEST PACSAT FILE HOLES: fail\n");
	return rc;
}


