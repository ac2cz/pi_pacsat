/*
 * ftl0.c
 *
 *  Created on: Sep 28, 2022
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
 *
 */

/* System include files */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/statvfs.h>

/* Program Include files */
#include "config.h"
#include "state_file.h"
#include "agw_tnc.h"
#include "str_util.h"
#include "pacsat_header.h"
#include "pacsat_dir.h"
#include "ftl0.h"
#include "pacsat_dir.h"

/* An entry on the uplink list keeps track of the file upload and where we are in the upload process */
struct uplink_entry {
	int state; /* File Upload state machine state */
	int channel; /* The receiver that is being used to receive this data */
	char callsign[MAX_CALLSIGN_LEN];
	int file_id; /* File id of the file being uploaded */
	time_t request_time; /* The time the request was received for timeout purposes */
	time_t TIMER_T3; /* This is our own T3 timer because direwolf is set at 300seconds.  We want to expire stations much faster if nothing heard */
};
typedef struct uplink_entry UPLINK_ENTRY;

/**
 * uplink_list
 * A list of callsigns that will receive attention from the FTL0 uplink process.
 * It stores the callsign and the status of the upload.
 * This is a static block of memory that exists throughout the duration of the program to
 * keep track of stations on the Uplink.
 */
static UPLINK_ENTRY uplink_list[MAX_UPLINK_LIST_LENGTH];

static int number_on_uplink = 0; /* This keeps track of how many stations are connected */
static int current_station_on_uplink = 0; /* This keeps track of which station we will send data to next */

time_t last_uplink_status_time;
time_t last_uplink_frames_queued_time;
char * ftl0_packet_type_names[] = {"DATA","DATA_END","LOGIN_RESP","UPLOAD_CMD","UL_GO_RESP","UL_ERROR_RESP","UL_ACK_RESP","UL_NAL_RESP"};

/* Forward declarations */
int ftl0_send_status();
int ftl0_add_request(char *from_callsign, int channel, int file_id);
int ftl0_remove_request(int pos);
void ftl0_make_list_str(char *buffer, int len);
void ftl0_debug_print_list();
void ftl0_debug_print_list_item(int i);
void ftl0_disconnect(char *to_callsign, int channel);
int ftl0_send_err(char *from_callsign, int channel, int err);
int ftl0_send_ack(char *from_callsign, int channel);
int ftl0_send_nak(char *from_callsign, int channel, int err);
int ftl0_process_upload_cmd(int list_num, char *from_callsign, int channel, unsigned char *data, int len);
int ftl0_process_data_cmd(int selected_station, char *from_callsign, int channel, unsigned char *data, int len);
int ftl0_process_data_end_cmd(int selected_station, char *from_callsign, int channel, unsigned char *data, int len);
int ftl0_make_packet(unsigned char *data_bytes, unsigned char *info, int length, int frame_type);
int ftl0_parse_packet_type(unsigned char * data);
int ftl0_parse_packet_length(unsigned char * data);

/**
 * ftl0_send_status()
 *
 * Transmit the current status of the uplink
 *
 * Returns EXIT_SUCCESS unless it was unable to send the request to the TNC
 *
 */
int ftl0_send_status() {
	if (!g_state_uplink_open) {
		unsigned char shut[] = "Shut: ABCD";
		int rc = send_raw_packet(g_bbs_callsign, BBSTAT, PID_NO_PROTOCOL, shut, sizeof(shut));
		return rc;
	} else 	if (number_on_uplink == MAX_UPLINK_LIST_LENGTH) {
		unsigned char full[] = "Full: ABCD";
		int rc = send_raw_packet(g_bbs_callsign, BBSTAT, PID_NO_PROTOCOL, full, sizeof(full));
		return rc;
	} else  {
		char buffer[256];
		char * CALL = BBSTAT;
		if (g_state_uplink_open == 2)
			CALL = BBCOM;

		ftl0_make_list_str(buffer, sizeof(buffer));
		unsigned char command[strlen(buffer)]; // now put the list in a buffer of the right size
		strlcpy((char *)command, (char *)buffer,sizeof(command));
		int rc = send_raw_packet(g_bbs_callsign, CALL, PID_NO_PROTOCOL, command, sizeof(command));
		return rc;
	}
}

/**
 * ftl0_add_request()
 *
 * Add a callsign and its request to the uplink
 *
 * Make a copy of all the data because the original packet will be purged soon from the
 * circular buffer
 *
 * When adding a request the variable number_on_uplink points to the next empty slot
 *
 * returns EXIT_SUCCESS it it succeeds or EXIT_FAILURE if the PB is shut or full
 *
 */
int ftl0_add_request(char *from_callsign, int channel, int file_id) {
	if (!g_state_uplink_open) return EXIT_FAILURE;
	if (number_on_uplink == MAX_UPLINK_LIST_LENGTH) {
		return EXIT_FAILURE; // Uplink full
	}

	/* Each station can only be on the Uplink once, so reject if the callsign is already in the list */
	for (int i=0; i < number_on_uplink; i++) {
		if ((strcmp(uplink_list[i].callsign, from_callsign) == 0)) {
			return EXIT_FAILURE; // Station is already on the PB
		}
	}

	strlcpy(uplink_list[number_on_uplink].callsign, from_callsign, MAX_CALLSIGN_LEN);
	uplink_list[number_on_uplink].state = UL_CMD_OK;
	uplink_list[number_on_uplink].channel = channel;
	uplink_list[number_on_uplink].file_id = file_id;
	uplink_list[number_on_uplink].request_time = time(0);

	number_on_uplink++;

	return EXIT_SUCCESS;
}

/**
 * ftl0_remove_request()
 *
 * Remove the callsign at the designated position.  This is most likely the
 * head because we finished a request.
 *
 * When removing an item the variable number_on_uplink is one greater
 * than the index of the last item given the array starts at 0.
 *
 * return EXIT_SUCCESS unless there is no item to remove.
 *
 */
int ftl0_remove_request(int pos) {
	time_t now = time(0);
	int duration = (int)(now - uplink_list[pos].request_time);
	debug_print("SESSION TIME: %s connected for %d seconds\n",uplink_list[number_on_uplink].callsign, duration);
	if (number_on_uplink == 0) return EXIT_FAILURE;
	if (pos >= number_on_uplink) return EXIT_FAILURE;
	if (pos != number_on_uplink-1) {

		/* Remove the item and shuffle all the other items to the left */
		for (int i = pos + 1; i < number_on_uplink; i++) {
			strlcpy(uplink_list[i-1].callsign, uplink_list[i].callsign, MAX_CALLSIGN_LEN);
			uplink_list[i-1].state = uplink_list[i].state;
			uplink_list[i-1].channel = uplink_list[i].channel;
			uplink_list[i-1].file_id = uplink_list[i].file_id;
			uplink_list[i-1].request_time = uplink_list[i].request_time;
		}
	}

	number_on_uplink--;

	/* We have to update the station we will next send data to.
	 * If a station earlier in the list was removed, then this decrements by one.
	 * If a station later in the list was remove we do nothing.
	 * If the current station was removed then we do nothing because we are already
	 * pointing to the next station, unless we are at the end of the list */
	if (pos < current_station_on_uplink) {
		current_station_on_uplink--;
		if (current_station_on_uplink < 0)
			current_station_on_uplink = 0;
	} else if (pos == current_station_on_uplink) {
		if (current_station_on_uplink >= number_on_uplink)
			current_station_on_uplink = 0;
	}
	return EXIT_SUCCESS;
}


/**
 * ftl0_make_list_str()
 *
 * Build the "open" status string that is periodically transmitted.
 * The *buffer to receive the string and its length len should be passed in.
 */
void ftl0_make_list_str(char *buffer, int len) {
	if (g_state_uplink_open == 2)
		strlcpy(buffer, "Command: ", len);
	else
		strlcpy(buffer, "Open: ", len);

	if (number_on_uplink == 0)
		strlcat(buffer, "ABCD.", len);
	else {
		strlcat(buffer, "A ", len);
		for (int i=0; i < number_on_uplink; i++) {
			strlcat(buffer, uplink_list[i].callsign, len);
			strlcat(buffer, " ", len);
		}
		// TODO - this does not print the right callsigns against the right channels!
		// TODO - this truncates the last character i.e. the D.
		strlcat(buffer, " BCD", len);
	}
}

void ftl0_debug_print_list() {
	char buffer[256];
	ftl0_make_list_str(buffer, sizeof(buffer));
	debug_print("%s\n",buffer);
	for (int i=0; i < number_on_uplink; i++) {
		ftl0_debug_print_list_item(i);
	}
}

void ftl0_debug_print_list_item(int i) {
	debug_print("--%s Ch:%d File:%d State: %d",uplink_list[i].callsign,uplink_list[i].channel,uplink_list[i].file_id,
			uplink_list[i].state);
	char buf[30];
	time_t now = time(0);
	int duration = (int)(now - uplink_list[i].request_time);
	debug_print(" for %d secs ",duration);
	time_t since = uplink_list[i].request_time;
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&since));
	debug_print(" since:%s\n", buf);
}

/**
 * ftl0_connection_received()
 *
When the data link is established the server transmits a LOGIN_RESP packet.

Packet: LOGIN_RESP
Information: 5 bytes
struct LOGIN_DATA{
     unsigned long login_time;
     unsigned char login_flags;
}

<login_time>  -  a 32-bit unsigned integer indicating the  number  of  seconds
since January 1, 1970.

<login_flags> - an 8-bit field.
     bit:76543210
         xxxxSHVV

Bit 3, the SelectionActive bit, will be 1 if there is already an active selec-
tion list for this client.  The SelectionActive bit will be 0 if there is no
active selection for this client already available.

Bit 2, the HeaderPFH bit, will be 1 if the server uses and requires PACSAT
File Headers on all files.  If the HeaderPFH bit is 1, the flag PFHserver used
in the following definition should be considered TRUE.

The HeaderPFH bit will be 0 if the server does not use PACSAT File Headers.
If the HeaderPFH bit is 0, the modified procedures specified in Section 7
should be used.

Bits 1 and 0 form a 2-bit FTL0 protocol version number.  The version described
here is 0.

Upon transmitting the LOGIN_RESP packet the server should initialize its state
variables to UL_CMD_WAIT

// TODO - Falconsat3 transmits a UI frame to confirm the login as well, perhaps so that
 * other stations can see who has logged in??
 *
 */
int ftl0_connection_received(char *from_callsign, char *to_callsign, int channel, int incomming, unsigned char * data) {
	debug_print("Connection for File Upload from: %s\n",from_callsign);

	/* Add the request, which initializes their uplink state machine. At this point we don't know the
	 * file number, offset or dir node */
	int rc = ftl0_add_request(from_callsign, channel, 3);
	if (rc != EXIT_SUCCESS){
		/* We could not add this request, either full or already on the uplink.  Disconnect. */
		ftl0_disconnect(from_callsign, channel);
		return EXIT_SUCCESS;
	} else {
		debug_print("Added %s to uplink list\n",from_callsign);
		ftl0_debug_print_list();
	}

	int frame_type = LOGIN_RESP;
	FTL0_LOGIN_DATA login_data;
	unsigned char data_bytes[sizeof(login_data)+2];

	time_t now = time(0); // TODO - we don't really need to call this again, we should use the time in the uplink_list for the station
	unsigned char flag = 0;
	flag |= 1UL << FTL0_VERSION_BIT1;
	flag |= 1UL << FTL0_VERSION_BIT2;
	flag |= 1UL << FTL0_PFH_BIT; // Set the bit to require PFHs

	login_data.login_time = (uint32_t)now;
	login_data.login_flags = flag;

	rc = ftl0_make_packet(data_bytes, (unsigned char *)&login_data, sizeof(login_data), frame_type);

	rc = tnc_send_connected_data(g_bbs_callsign, from_callsign, channel, data_bytes, sizeof(login_data)+2);
	if (rc != EXIT_SUCCESS) {
		error_print("Could not send FTL0 LOGIN packet to TNC \n");
		return EXIT_FAILURE;
	}
	uplink_list[current_station_on_uplink].TIMER_T3 = time(0); /* Start T3 */
	return EXIT_SUCCESS;
}

/**
 * ftl0_disconnect()
 *
 * Disconnect from the station specified in to_callsign
 */
void ftl0_disconnect(char *to_callsign, int channel) {
	debug_print("Disconnecting: %s\n", to_callsign);
	tnc_diconnect(g_bbs_callsign, to_callsign, channel);
}

int ftl0_get_list_number_by_callsign(char *from_callsign) {
	int selected_station = -1; /* The station that this event is for */
	for (int i=0; i < number_on_uplink; i++) {
		if (strncasecmp(from_callsign, uplink_list[i].callsign, MAX_CALLSIGN_LEN) == 0) {
			selected_station = i;
			break;
		}
	}
	return selected_station;
}

/**
 * ftl0_disconnected()
 *
 * We only receive this if the TNC has disconnected.  Remove the station
 * from the uplink list if they are still on it.
 */
int ftl0_disconnected(char *from_callsign, char *to_callsign, unsigned char *data, int len) {
	int selected_station = ftl0_get_list_number_by_callsign(from_callsign); /* The station that this event is for */

	if (selected_station == -1) {
		debug_print("Ignoring disconnect from %s as they are not in the list uplink\n", from_callsign);
		return EXIT_SUCCESS; /* Ignored, this station is not on the uplink */
	}
	int rc = ftl0_remove_request(selected_station);
	return rc;
}

int ftl0_process_data(char *from_callsign, char *to_callsign, int channel, unsigned char *data, int len) {
	if (strncasecmp(to_callsign, g_broadcast_callsign, MAX_CALLSIGN_LEN) == 0) {
		// this was sent to the Broadcast Callsign
		debug_print("Broadcast Request - Ignored\n");
		return EXIT_SUCCESS;
	}
	if (strncasecmp(to_callsign, g_bbs_callsign, MAX_CALLSIGN_LEN) != 0) return EXIT_SUCCESS;
		// this was sent to the Broadcast Callsign

	/* Now process the next station if there is one and take its action */
	if (number_on_uplink == 0) return EXIT_SUCCESS; // nothing to do

	int selected_station = ftl0_get_list_number_by_callsign(from_callsign); /* The station that this event is for */

	if (selected_station == -1) {
		debug_print("Ignoring data from %s as they are not in the list uplink\n", from_callsign);
		return EXIT_SUCCESS; /* Ignored, this station is not on the uplink */
	}

	uplink_list[selected_station].TIMER_T3 = time(0); /* Restart T3 */


	int ftl0_type = ftl0_parse_packet_type(data);
	if (ftl0_type > MAX_PACKET_ID) {
		int rc = ftl0_send_err(from_callsign, channel, ER_ILL_FORMED_CMD);
		if (rc != EXIT_SUCCESS) {
			/* We likely could not send the error.  Something serious has gone wrong.
			 * Not much we can do as we are going to offload the request anyway */
		}
		ftl0_disconnect(uplink_list[selected_station].callsign, uplink_list[selected_station].channel);
		ftl0_remove_request(selected_station);
	}

	//int ftl0_length = ftl0_parse_packet_length(data);
	int rc = EXIT_SUCCESS;

	switch (uplink_list[selected_station].state) {
	case UL_UNINIT:
		debug_print("%s: UNINIT - %s\n",uplink_list[selected_station].callsign, ftl0_packet_type_names[ftl0_type]);

		break;

	case UL_CMD_OK:
		debug_print("%s: UL_CMD_OK - %s\n",uplink_list[selected_station].callsign, ftl0_packet_type_names[ftl0_type]);

		/* Process the EVENT through the UPLINK STATE MACHINE */
		switch (ftl0_type) {
		case UPLOAD_CMD :
			/* if OK to upload send UL_GO_RESP.  We determine if it is OK by checking if we have space
			 * and that it is a valid continue of file_id != 0
			 * This will send UL_GO_DATA packet if checks pass
			 * TODO - the code would be clearer if this parses the request then returns here and then we
			 * send the packet from here.  Then all packet sends are from this level of the state machine*/
			int err = ftl0_process_upload_cmd(selected_station, from_callsign, channel, data, len);
			if (err != ER_NONE) {
				// send the error
				rc = ftl0_send_err(from_callsign, channel, err);
				if (rc != EXIT_SUCCESS) {
					/* We likely could not send the error.  Something serious has gone wrong.
					 * But the best we can do is remove the station and return the error code. */
					ftl0_disconnect(uplink_list[selected_station].callsign, uplink_list[selected_station].channel);
					ftl0_remove_request(selected_station);
				}
				// If we sent error successfully then we stay in state UL_CMD_OK and the station can try another file
				return rc;
			}
			// We move to state UL_DATA_RX
			uplink_list[selected_station].state = UL_DATA_RX;
			break;

		default:
			ftl0_disconnect(uplink_list[selected_station].callsign, uplink_list[selected_station].channel);
			ftl0_remove_request(selected_station);
			return EXIT_SUCCESS; // don't increment or change the current station
			break;
		}

		break;

	case UL_DATA_RX:
//		debug_print("%s: UL_DATA_RX - %s\n",uplink_list[selected_station].callsign, ftl0_packet_type_names[ftl0_type]);

		switch (ftl0_type) {
		case DATA :
//			debug_print("%s: UL_DATA_RX - DATA RECEIVED\n",uplink_list[selected_station].callsign);
			int err = ftl0_process_data_cmd(selected_station, from_callsign, channel, data, len);
			if (err != ER_NONE) {
				rc = ftl0_send_nak(from_callsign, channel, err);
				if (rc != EXIT_SUCCESS) {
					/* We likely could not send the error.  Something serious has gone wrong.
					 * But the best we can do is remove the station and return the error code. */
				}
				ftl0_disconnect(uplink_list[selected_station].callsign, uplink_list[selected_station].channel);
				ftl0_remove_request(selected_station);
				return rc;
			}
			uplink_list[selected_station].state = UL_DATA_RX;
			break;

		case DATA_END :
//			debug_print("%s: UL_DATA_RX - DATA END RECEIVED\n",uplink_list[selected_station].callsign);
			err = ftl0_process_data_end_cmd(selected_station, from_callsign, channel, data, len);
			if (err != ER_NONE) {
				rc = ftl0_send_nak(from_callsign, channel, err);
			} else {
//				debug_print(" *** SENDING ACK *** \n");
				rc = ftl0_send_ack(from_callsign, channel);
			}
			uplink_list[selected_station].state = UL_CMD_OK;
			if (rc != EXIT_SUCCESS) {
				/*  We likely could not send the error.  Something serious has gone wrong.
				 * But the best we can do is remove the station and return the error code. */
				ftl0_disconnect(uplink_list[selected_station].callsign, uplink_list[selected_station].channel);
				ftl0_remove_request(selected_station);
			}
			return rc;
			break;
		}

		break;

	case UL_ABORT:
		debug_print("%s: UL_ABORT - %s\n",uplink_list[selected_station].callsign, ftl0_packet_type_names[ftl0_type]);
		ftl0_disconnect(uplink_list[selected_station].callsign, uplink_list[selected_station].channel);
		ftl0_remove_request(selected_station);
		return EXIT_SUCCESS; // don't increment or change the current station
		break;
	}

	return EXIT_SUCCESS;
}

int ftl0_send_err(char *from_callsign, int channel, int err) {
	int frame_type = UL_ERROR_RESP;
	char err_info[1];
	unsigned char data_bytes[sizeof(err_info)+2];

	err_info[0] = err;

	int rc = ftl0_make_packet(data_bytes, (unsigned char *)&err_info, sizeof(err_info), frame_type);

	rc = tnc_send_connected_data(g_bbs_callsign, from_callsign, channel, data_bytes, sizeof(data_bytes));
	if (rc != EXIT_SUCCESS) {
		error_print("Could not send FTL0 ERR packet to TNC \n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int ftl0_send_ack(char *from_callsign, int channel) {
	int frame_type = UL_ACK_RESP;
	unsigned char data_bytes[2];

	int rc = ftl0_make_packet(data_bytes, (unsigned char *)NULL, 0, frame_type);

	rc = tnc_send_connected_data(g_bbs_callsign, from_callsign, channel, data_bytes, sizeof(data_bytes));
	if (rc != EXIT_SUCCESS) {
		error_print("Could not send FTL0 ACK packet to TNC \n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int ftl0_send_nak(char *from_callsign, int channel, int err) {
	int frame_type = UL_NAK_RESP;
	char err_info[1];
	unsigned char data_bytes[sizeof(err_info)+2];

	err_info[0] = err;

	int rc = ftl0_make_packet(data_bytes, (unsigned char *)&err_info, sizeof(err_info), frame_type);

	rc = tnc_send_connected_data(g_bbs_callsign, from_callsign, channel, data_bytes, sizeof(data_bytes));
	if (rc != EXIT_SUCCESS) {
		error_print("Could not send FTL0 NAK packet to TNC \n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/**
 * ftl0_process_upload_cmd()
 *
 * selected_station is the index of the station in the upload list
 * The upload command packets data is parsed
 *
Packet: UPLOAD_CMD
Information: 8 bytes
struct {
     unsigned long continue_file_no;
     unsigned long file_length;
}

<continue_file_no> - a 32-bit unsigned integer identifying the file to contin-
ue.   Used to continue a previously-aborted upload.  Must be 0 when commencing
a new upload.

<file_length> -  32-bit unsigned integer indicating the number of bytes in the
file.

 */
int ftl0_process_upload_cmd(int selected_station, char *from_callsign, int channel, unsigned char *data, int len) {

	int ftl0_length = ftl0_parse_packet_length(data);
	if (ftl0_length != 8)
		return ER_ILL_FORMED_CMD;

	FTL0_UPLOAD_CMD *upload_cmd = (FTL0_UPLOAD_CMD *)(data + 2); /* Point to the data just past the header */

	uint32_t file_no = upload_cmd->continue_file_no;
	uint32_t length = upload_cmd->file_length;

	if (length == 0)
		return ER_ILL_FORMED_CMD;

	/* This is the data we are going to send */
	FTL0_UL_GO_DATA ul_go_data;

	/* Check if data is valid */
	if (file_no == 0) {
		/* Do we have space */
		struct statvfs buffer;
		int ret = statvfs(get_dir_folder(), &buffer);
		if (!ret) {
			//const unsigned int GB = (1024 * 1024) * 1024;
			//const double total = (double)(buffer.f_blocks * buffer.f_frsize);
			const double available = (double)(buffer.f_bfree * buffer.f_frsize);
			//debug_print("Disk Space: %f --> %.0f\n", total, total/GB);
			//debug_print(" Available: %f --> %.0f\n", available, available/GB);

			if (available - length < UPLOAD_SPACE_THRESHOLD)
				return ER_NO_ROOM;
		} else {
			/* Can't check if we have space, assume an error */
			return ER_NO_ROOM;
		}

		/* We have space so allocate a file number, store in uplink list and send to the station */
		ul_go_data.server_file_no = dir_next_file_number();
		debug_print("Allocated file id: %d\n",ul_go_data.server_file_no);
		ul_go_data.byte_offset = 0;
		/* Initialize the empty file */
		char tmp_filename[MAX_FILE_PATH_LEN];
		dir_make_tmp_filename(ul_go_data.server_file_no, tmp_filename, MAX_FILE_PATH_LEN);
		FILE * f = fopen(tmp_filename, "w");
		if (f == NULL) {
			error_print("Can't initilize new file %s\n",tmp_filename);
			return ER_NO_ROOM;
		}
		fclose(f);
	} else {
		/* Is this a valid continue? Check to see if there is a tmp file and read its length */
		// TODO - we also need to check the situation where we have the complete file but the ground station never received the ACK.
		//        So an atttempt to upload a finished file that belongs to this station, that has the right length, should get an ACK to finish upload off
		char tmp_filename[MAX_FILE_PATH_LEN];
		dir_make_tmp_filename(file_no, tmp_filename, MAX_FILE_PATH_LEN);
		debug_print("Checking continue file: %s\n",tmp_filename);
		FILE * f = fopen(tmp_filename, "rb");
		if (f == NULL) {
			error_print("No such file number \n");
			return ER_NO_SUCH_FILE_NUMBER;
		}
		fseek(f, 0L, SEEK_END);
		int offset = ftell(f);
		fclose(f);

		// TODO - we need to remember the "promised" file length after the station is removed from the Uplink list
		/* if <continue_file_no> is not 0 and the <file_length> does not
			agree with the <file_length> previously associated with the file identified by
			<continue_file_no>.  Continue is not possible.*/
		// code this error check

		ul_go_data.server_file_no = file_no;
		ul_go_data.byte_offset = offset; // this is the end of the file so far

	}
	uplink_list[selected_station].file_id = ul_go_data.server_file_no;

	unsigned char data_bytes[sizeof(ul_go_data)+2];

	int rc = ftl0_make_packet(data_bytes, (unsigned char *)&ul_go_data, sizeof(ul_go_data), UL_GO_RESP);
	rc = tnc_send_connected_data(g_bbs_callsign, from_callsign, channel, data_bytes, sizeof(data_bytes));
	if (rc != EXIT_SUCCESS) {
		error_print("Could not send FTL0 UL GO packet to TNC \n");
		return ER_ILL_FORMED_CMD; //  This will cause err 1 to be sent and the station to be offloaded.  Is that right..
	}
	return ER_NONE;
}

/**
 * ftl0_process_data_cmd()
 *
 * Parse and process a data command from the ground station.  Reception of a data command also means
 * that a directory node is allocated so that a continue will be successful.  This is what "reserves" the
 * new file number.
 *
 */
int ftl0_process_data_cmd(int selected_station, char *from_callsign, int channel, unsigned char *data, int len) {
	int ftl0_type = ftl0_parse_packet_type(data);
	if (ftl0_type != DATA) {
		return ER_ILL_FORMED_CMD; /* We should never get this */
	}
	int ftl0_length = ftl0_parse_packet_length(data);
	if (ftl0_length == 0 || ftl0_length > len-2) {
		return ER_BAD_HEADER; /* This will cause a NAK to be sent as the data is corrupt in some way */
	}

	unsigned char * data_bytes = (unsigned char *)data + 2; /* Point to the data just past the header */

	char tmp_filename[MAX_FILE_PATH_LEN];
	dir_make_tmp_filename(uplink_list[selected_station].file_id, tmp_filename, MAX_FILE_PATH_LEN);
	debug_print("Saving data to file: %s\n",tmp_filename);
	FILE * f = fopen(tmp_filename, "ab"); /* Open the file for append of data to the end */
	if (f == NULL) {
		return ER_NO_SUCH_FILE_NUMBER;
	}
	for (int i=0; i< ftl0_length; i++) {
		int c = fputc((unsigned int)data_bytes[i],f);
		if (c == EOF) {
			fclose(f);
			return ER_NO_ROOM; // This is most likely caused by running out of file ids or space
		}
	}
	fclose(f);

	return ER_NONE;
}

int ftl0_process_data_end_cmd(int selected_station, char *from_callsign, int channel, unsigned char *data, int len) {
	int ftl0_type = ftl0_parse_packet_type(data);
	if (ftl0_type != DATA_END) {
		return ER_ILL_FORMED_CMD; /* We should never get this */
	}
	int ftl0_length = ftl0_parse_packet_length(data);
	if (ftl0_length != 0) {
		return ER_BAD_HEADER; /* This will cause a NAK to be sent as the data is corrupt in some way */
	}

	char tmp_filename[MAX_FILE_PATH_LEN];
	dir_make_tmp_filename(uplink_list[selected_station].file_id, tmp_filename, MAX_FILE_PATH_LEN);

	/* We can't call dir_load_pacsat_file() here because we want to check the tmp file but then
	 * add the file after we rename it. So we validate it first. */

	/* First check the header.  We must free the pfh memory if it is not added to the dir */
	HEADER *pfh = pfh_load_from_file(tmp_filename);
	if (pfh == NULL) {
		/* Header is invalid */
		error_print("** Header check failed for %s\n",tmp_filename);
		if (remove(tmp_filename) != 0) {
			error_print("Could not remove the temp file: %s\n", tmp_filename);
		}
		return ER_BAD_HEADER;
	}

	int rc = dir_validate_file(pfh, tmp_filename);
	if (rc != ER_NONE) {
		free(pfh);
		if (remove(tmp_filename) != 0) {
			error_print("Could not remove the temp file: %s\n", tmp_filename);
		}
		return rc;
	}

	/* Otherwise this looks good.  Rename the file and add it to the directory. */
	/* TODO - note that we are renaming the file before we know that the ground station has received an ACK
	 *  That is OK as long as we handle the situation where the ground station tries to finish the upload
           and we no longer have the tmp file.  This is handled in process_upload_command() where ER_FILE_COMPLETE
           is sent.
	 */
	char new_filename[MAX_FILE_PATH_LEN];
	pfh_make_filename(uplink_list[selected_station].file_id, get_dir_folder(), new_filename, MAX_FILE_PATH_LEN);
	if (rename(tmp_filename, new_filename) == EXIT_SUCCESS) {
//		char file_id_str[5];
//		snprintf(file_id_str, 4, "%d",uplink_list[selected_station].file_id);
//		strlcpy(pfh->fileName, file_id_str, sizeof(pfh->fileName));
//		strlcpy(pfh->fileExt, PSF_FILE_EXT, sizeof(pfh->fileExt));

		DIR_NODE *p = dir_add_pfh(pfh, new_filename);
		if (p == NULL) {
			error_print("** Could not add %s to dir\n",new_filename);
			free(pfh);
			if (remove(tmp_filename) != 0) {
				error_print("Could not remove the temp file: %s\n", new_filename);
			}
			return ER_NO_ROOM; /* This is a bit of a guess at the error, but it is unclear why else this would fail. */
		}
	} else {
		/* This looks like an io error and we can't rename the file.  Send error to the ground */
		free(pfh);
		if (remove(tmp_filename) != 0) {
			error_print("Could not remove the temp file: %s\n", tmp_filename);
		}
		return ER_NO_ROOM;
	}
	return ER_NONE;
}

/*
 *
 * ftl0_next_action
 *
 * This handles periodic actions like timeout.  It does not tick the state machine,
 * that is event driven and is handled by received frames above.
 *
 * Returns EXIT_SUCCESS, unless something goes badly wrong, such as we can not send data to
 * the TNC
 *
 */
int ftl0_next_action() {
	int rc = EXIT_SUCCESS;

	/* First see if we need to send the status */
	time_t now = time(0);
	if (last_uplink_status_time == 0) last_uplink_status_time = now; // initialize at start
	if (now - last_uplink_status_time > g_uplink_status_period_in_seconds) {
		// then send the status
		rc = ftl0_send_status();
		if (rc != EXIT_SUCCESS) {
			error_print("Could not send PB status to TNC \n");
		}
		//ftl0_debug_print_list();
		last_uplink_status_time = now;

	}

	/* Print debug info to the console */
//	if (now - last_uplink_frames_queued_time > 5) {
//		//			char buffer[256];
//		//			ftl0_make_list_str(buffer, sizeof(buffer));
//		//			debug_print("%s\n", buffer);
//		ftl0_debug_print_list();
//		last_uplink_frames_queued_time = now;
//	}

	if (number_on_uplink == 0) return EXIT_SUCCESS; // nothing to do

	if (uplink_list[current_station_on_uplink].TIMER_T3 > 0) {
		/* Timer is running */
		if (now - uplink_list[current_station_on_uplink].TIMER_T3 > TIMER_T3_PERIOD_IN_SECONDS) {
			/* Timer T3 Expired - disconnect station */
			debug_print("%s: T3 TIMEOUT\n",uplink_list[current_station_on_uplink].callsign);
			ftl0_disconnect(uplink_list[current_station_on_uplink].callsign, uplink_list[current_station_on_uplink].channel);
			ftl0_remove_request(current_station_on_uplink);
			/* If we removed a station then we don't want/need to increment the current station pointer */
			uplink_list[current_station_on_uplink].TIMER_T3 = 0; /* stop the timer */
			return EXIT_SUCCESS;
		}
	}

	if (now - uplink_list[current_station_on_uplink].request_time > g_uplink_max_period_for_client_in_seconds) {
		/* This station has exceeded the time allowed on the PB */
		debug_print("%s: UPLINK TIMEOUT\n",uplink_list[current_station_on_uplink].callsign);
		ftl0_disconnect(uplink_list[current_station_on_uplink].callsign, uplink_list[current_station_on_uplink].channel);
		ftl0_remove_request(current_station_on_uplink);
		/* If we removed a station then we don't want/need to increment the current station pointer */
		return EXIT_SUCCESS;
	}

	current_station_on_uplink++;
	if (current_station_on_uplink == number_on_uplink)
		current_station_on_uplink = 0;

	return rc;
}

/**
 * ftl0_make_packet()
 *
 * Pass the ftl0 info bytes, the length of the info bytes and frame type
 * returns packet in unsigned char *data_bytes, which must be length + 2 long
 *
Packets flow as follows:

<length_lsb><h1>[<info>...]<length_lsb><h1>[<info>...]
|----First FTL0 packet-----|----Second FTL0 packet---|

<length_lsb>  - 8 bit unsigned integer supplying the least significant 8  bits
of data_length.

<h1> - an 8-bit field.
     bits 7-5 contribute 3 most significant bits to data_length.

     bits 4-0 encode 32 packet types
 */
int ftl0_make_packet(unsigned char *data_bytes, unsigned char *info, int length, int frame_type) {
//	FTL0_PKT_HEADER ftl0_pkt_header;

	unsigned char length_lsb = length & 0xff; // least 8 bits of length
	unsigned char h1 = (length >> 8) & 0x07; // 3 most sig bits of length
	h1 = (unsigned char)(frame_type | (h1 << 5)); // move into bits 7-5

	/* Copy the bytes into the frame */
	data_bytes[0] = length_lsb;
	data_bytes[1] = h1;
	if (info != NULL && sizeof(info) > 0) {
		for (int i=0; i<length;i++ )
			data_bytes[i+2] = info[i];
	}
	return EXIT_SUCCESS;
}

int ftl0_parse_packet_type(unsigned char * data) {
	int type = data[1] & 0b00011111;
	return type;
}

int ftl0_parse_packet_length(unsigned char * data) {
	int length = (data[1] >> 5) * 256 + data[0];
	return length;
}

/*********************************************************************************************
 *
 * SELF TESTS FOLLOW
 *
 */
int test_ftl0_frame() {
	printf("##### TEST FTL0 LIST\n");
	int rc = EXIT_SUCCESS;

	int frame_type = 0x15; // 0b10101

	unsigned char info[1808]; // 0x710 length
	unsigned char data_bytes[sizeof(info)+2];

	info[0] = 0xA;
	info[sizeof(info)-1] = 0xf;
	rc = ftl0_make_packet(data_bytes, info, sizeof(info), frame_type);

	debug_print("FTL0 Header: %02x %02x\n", data_bytes[0], data_bytes[1]);
	if (data_bytes[0] != 0x10) {printf("** Mismatched header byte 1 \n"); return EXIT_FAILURE; }
	if (data_bytes[1] != 0xf5) {printf("** Mismatched header byte 2 \n"); return EXIT_FAILURE; }
	if (data_bytes[2] != 0xa) {printf("** Mismatched info byte 1 \n"); return EXIT_FAILURE; }
	if (data_bytes[sizeof(data_bytes)-1] != 0xf) {printf("** Mismatched info byte at end \n"); return EXIT_FAILURE; }

	if (ftl0_parse_packet_length(data_bytes) != 1808 ) {printf("** Mismatched length \n"); return EXIT_FAILURE; }
	if (ftl0_parse_packet_type(data_bytes) != 0x15 ) {printf("** Mismatched type \n"); return EXIT_FAILURE; }

	debug_print("Test Login Packet\n");


	if (rc == EXIT_SUCCESS)
		printf("##### TEST FTL0 LIST: success\n");
	else
		printf("##### TEST FTL0 LIST: fail\n");
	return rc;
}


int test_ftl0_list() {
	printf("##### TEST FTL0 LIST\n");
	int rc = EXIT_SUCCESS;

	rc = ftl0_add_request("AC2CZ", 0,3);
	if (rc != EXIT_SUCCESS) {printf("** Could not add uplink request AC2CZ for file 3\n"); return EXIT_FAILURE; }
	rc = ftl0_add_request("G0KLA", 0, 2);
	if (rc != EXIT_SUCCESS) {printf("** Could not add uplink request G0KLA for file 2\n"); return EXIT_FAILURE; }
	rc = ftl0_add_request("VE2XYZ", 0, 1);
	if (rc != EXIT_SUCCESS) {printf("** Could not add uplink request ve2xyz for file 1\n"); return EXIT_FAILURE; }
	rc = ftl0_add_request("W1ABC", 0, 11);
	if (rc != EXIT_SUCCESS) {printf("** Could not add uplink request W1ABC for file 11\n"); return EXIT_FAILURE; }
	debug_print("TEST FULL\n");
	rc = ftl0_add_request("G1XCX", 0, 22);
	if (rc == EXIT_SUCCESS) {printf("** Added uplink request when full\n"); return EXIT_FAILURE; }

	ftl0_debug_print_list();
	if (strcmp(uplink_list[0].callsign, "AC2CZ") != 0) {printf("** Mismatched callsign AC2CZ\n"); return EXIT_FAILURE;}
	if (uplink_list[0].file_id != 3) {printf("** Mismatched file_id 3\n"); return EXIT_FAILURE;}
	if (uplink_list[0].channel != 0) {printf("** Mismatched channel 0\n"); return EXIT_FAILURE;}
	if (uplink_list[0].state != UL_CMD_OK) {printf("** Mismatched state 0\n"); return EXIT_FAILURE;}
	if (strcmp(uplink_list[1].callsign, "G0KLA") != 0) {printf("** Mismatched callsign G0KLA\n"); return EXIT_FAILURE;}
	if (strcmp(uplink_list[2].callsign, "VE2XYZ") != 0) {printf("** Mismatched callsign VE2XYZ\n"); return EXIT_FAILURE;}
	if (strcmp(uplink_list[3].callsign, "W1ABC") != 0) {printf("** Mismatched callsign W1ABC\n"); return EXIT_FAILURE;}

	current_station_on_uplink = 3;

	debug_print("REMOVE a middle item\n");
	rc = ftl0_remove_request(2);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove middle uplink request\n"); return EXIT_FAILURE; }
	if (strcmp(uplink_list[2].callsign, "W1ABC") != 0) {printf("** Mismatched callsign W1ABC\n"); return EXIT_FAILURE;}
	if (current_station_on_uplink != 2) {printf("** Mismatched current_station_on_uplink, expected 2\n"); return EXIT_FAILURE;}

	debug_print("REMOVE last item\n");
	rc = ftl0_remove_request(2);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove last uplink request\n"); return EXIT_FAILURE; }
	if (strcmp(uplink_list[0].callsign, "AC2CZ") != 0) {printf("** Mismatched callsign AC2CZ\n"); return EXIT_FAILURE;}
	if (strcmp(uplink_list[1].callsign, "G0KLA") != 0) {printf("** Mismatched callsign G0KLA\n"); return EXIT_FAILURE;}
	if (strcmp(uplink_list[2].callsign, "W1ABC") != 0) {printf("** Mismatched callsign W1ABC\n"); return EXIT_FAILURE;}
	if (current_station_on_uplink != 0) {printf("** Mismatched current_station_on_uplink, expected 0\n"); return EXIT_FAILURE;}

	// Add another
	rc = ftl0_add_request("G1XCX", 0, 22);
	if (rc != EXIT_SUCCESS) {printf("** Could not add uplink request G1XCX for file 22\n"); return EXIT_FAILURE; }

	debug_print("REMOVE Head\n");
	rc = ftl0_remove_request(0);
	if (rc != EXIT_SUCCESS) {printf("** Could not remove First uplink request\n"); return EXIT_FAILURE; }
	if (strcmp(uplink_list[0].callsign, "G0KLA") != 0) {printf("** Mismatched callsign G0KLA\n"); return EXIT_FAILURE;}
	if (strcmp(uplink_list[1].callsign, "G1XCX") != 0) {printf("** Mismatched callsign G1XCX\n"); return EXIT_FAILURE;}
	if (current_station_on_uplink != 0) {printf("** Mismatched current_station_on_uplink, expected 0\n"); return EXIT_FAILURE;}

	ftl0_next_action();


	if (rc == EXIT_SUCCESS)
		printf("##### TEST FTL0 LIST: success\n");
	else
		printf("##### TEST FTL0 LIST: fail\n");
	return rc;
}

int test_ftl0_action() {
	printf("##### TEST FTL0 ACTION\n");
	int rc = EXIT_SUCCESS;

	unsigned char data[] = "The quick brown fox jumps over the lazy dog";
	char *filename = "/tmp/fred.txt";
	FILE * outfile = fopen(filename, "wb");
	if (outfile == NULL) return EXIT_FAILURE;

	/* Save the header bytes, which might be shorter or longer than the original header */
	for (int i=0; i<7; i++) {
		int c = fputc(data[i],outfile);
		if (c == EOF) {
			fclose(outfile);
			return EXIT_FAILURE; // we could not write to the file
		}
	}
	fclose(outfile);

	FILE * f = fopen(filename, "rb");
	if (f == NULL) { error_print("No such file \n"); return EXIT_FAILURE; }
	fseek(f, 0L, SEEK_END);
	int offset = ftell(f);
	fclose(f);
	debug_print("OFFSET: %d\n", offset);
	//ftl0_next_action();

	// Now append
	debug_print("Saving data to file: %s\n",filename);
	FILE * f2 = fopen(filename, "ab"); /* Open the file for append of data to the end */
	if (f == NULL) {
		return ER_NO_SUCH_FILE_NUMBER;
	}
	for (int i=offset; i< sizeof(data); i++) {
		int c = fputc((unsigned int)data[i],f2);
		if (c == EOF) {
			fclose(f2);
			return EXIT_FAILURE; // we could not write to the file, assume it is not valid, was it purged?
		}
	}
	fclose(f2);

	if (rc == EXIT_SUCCESS)
		printf("##### TEST FTL0 ACTION: success\n");
	else
		printf("##### TEST FTL0 ACTION: fail\n");
	return rc;
}
