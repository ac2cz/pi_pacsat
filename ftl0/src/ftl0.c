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
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

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
struct ftl0_state_machine_t {
	int state; /* File Upload state machine state */
	int channel; /* The receiver that is being used to receive this data */
	char callsign[MAX_CALLSIGN_LEN];
	uint32_t file_id; /* File id of the file being uploaded */
	uint32_t offset;
	uint32_t length;
	time_t request_time; /* The time the request was received for timeout purposes */
	time_t TIMER_T3; /* This is our own T3 timer because direwolf is set at 300seconds.  We want to expire stations much faster if nothing heard */
};

/**
 * uplink_list
 * A list of callsigns that will receive attention from the FTL0 uplink process.
 * It stores the callsign and the status of the upload.
 * This is a static block of memory that exists throughout the duration of the program to
 * keep track of stations on the Uplink.
 */
static struct ftl0_state_machine_t uplink_list[MAX_UPLINK_LIST_LENGTH];

static InProcessFileUpload_t upload_table[MAX_IN_PROCESS_FILE_UPLOADS];

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
int ftl0_clear_upload_table();
int ftl0_remove_upload_file(uint32_t file_id);

/**
 * ftl0_send_status()
 *
 * Transmit the current status of the uplink
 *
 * Returns EXIT_SUCCESS unless it was unable to send the request to the TNC
 *
 */
int ftl0_send_status() {
	if (g_state_uplink_open == FTL0_STATE_SHUT) {
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
	uplink_list[number_on_uplink].offset = 0;
	uplink_list[number_on_uplink].length = 0;
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
	if (g_state_uplink_open == FTL0_STATE_COMMAND)
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
	int rc = ftl0_add_request(from_callsign, channel, 0);
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
//	if (strncasecmp(to_callsign, g_broadcast_callsign, MAX_CALLSIGN_LEN) == 0) {
//		// this was sent to the Broadcast Callsign
//		debug_print("Broadcast Request - Ignored\n");
//		return EXIT_SUCCESS;
//	}
	if (strncasecmp(to_callsign, g_bbs_callsign, MAX_CALLSIGN_LEN) != 0) return EXIT_SUCCESS;
		// this was sent another Callsign

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
			// Update the upload record in case nothing else is received
			InProcessFileUpload_t file_upload_record;
			if (ftl0_get_file_upload_record(uplink_list[selected_station].file_id, &file_upload_record) == EXIT_SUCCESS) {
				file_upload_record.request_time = time(0); // this is updated when we receive data
				file_upload_record.offset = uplink_list[selected_station].offset;
				if (ftl0_update_file_upload_record(&file_upload_record) != EXIT_SUCCESS) {
					debug_print("Unable to update upload record\n");
					// do not treat this as fatal because the file can still be uploaded
				}
			}
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
		default:
			ftl0_disconnect(uplink_list[selected_station].callsign, uplink_list[selected_station].channel);
			ftl0_remove_request(selected_station);
			return EXIT_SUCCESS; // don't increment or change the current station
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
	struct ftl0_state_machine_t *state = &uplink_list[selected_station];


	int ftl0_length = ftl0_parse_packet_length(data);
	if (ftl0_length != 8)
		return ER_ILL_FORMED_CMD;

	FTL0_UPLOAD_CMD *upload_cmd = (FTL0_UPLOAD_CMD *)(data + 2); /* Point to the data just past the header */

	state->file_id = upload_cmd->continue_file_no;
	state->length = upload_cmd->file_length;

	if (state->length == 0)
		return ER_ILL_FORMED_CMD;

	/* This is the data we are going to send */
	FTL0_UL_GO_DATA ul_go_data;

	/* Check if data is valid */
	if (state->file_id == 0) {
		/* first check against the maximum allowed file size, which should be a configurable param from the ground */
		if (state->length > g_ftl0_max_file_size)
			return ER_NO_ROOM;
		/* Do we have space */
		struct statvfs buffer;
		int ret = statvfs(get_dir_folder(), &buffer);
		if (ret != EXIT_SUCCESS) {
			/* Can't check if we have space, assume an error */
			error_print("Cant check file system space: %s\n",strerror(errno));
			return ER_NO_ROOM;
		} else {
			double GB = (1024 * 1024) * 1024;
			//unsigned long total = (double)(buffer.f_blocks * buffer.f_frsize);
			unsigned long available = (buffer.f_bavail * buffer.f_frsize);
			//debug_print("Disk Space: %d --> %.0fG\n", (int)total, total/GB);
			// TODO - this does not quite agree with typing df . in the same file system
			debug_print(" Available: %ld blks %ld --> %.0fG\n", buffer.f_bavail, (long)available, available/GB);

			/* Need to check all the partially uploaded files to see what remaining space they have claimed. */
			uint32_t upload_table_space = 0; //ftl0_get_space_reserved_by_upload_table();

			if ((state->length + upload_table_space + UPLOAD_SPACE_THRESHOLD) > available)
				return ER_NO_ROOM;
		}

		/* We have space so allocate a file number, store in uplink list and send to the station */
		state->file_id = dir_next_file_number();
		if (state->file_id == 0) {
			debug_print("Unable to allocated new file id: %s\n", strerror(errno));
			return ER_NO_ROOM;  // TODO - is this the best error to send?  File system is unavailable it seems
		}
		ul_go_data.server_file_no = state->file_id;
		debug_print("Allocated file id: %d\n",ul_go_data.server_file_no);
		ul_go_data.byte_offset = 0;
		state->offset = 0;

		/* Initialize the empty file */
		char tmp_filename[MAX_FILE_PATH_LEN];
		dir_get_upload_file_path_from_file_id(ul_go_data.server_file_no, tmp_filename, MAX_FILE_PATH_LEN);
		FILE * f = fopen(tmp_filename, "w");
		if (f == NULL) {
			error_print("Can't initilize new file %s\n",tmp_filename);
			return ER_NO_ROOM;
		}
		fclose(f);

		/* Store in an upload table record.  The state will now contain all the details */
		InProcessFileUpload_t file_upload_record;
		strlcpy(file_upload_record.callsign,state->callsign, sizeof(file_upload_record.callsign));
		file_upload_record.file_id = state->file_id;
		file_upload_record.length = state->length;
		file_upload_record.request_time = state->request_time;
		file_upload_record.offset = state->offset;
		if (ftl0_set_file_upload_record(&file_upload_record) != EXIT_SUCCESS ) {
			debug_print("Unable to create upload record for file id %04x\n",state->file_id);
			// this is not fatal as we may still be able to upload the file, though a later continue may not work
		}

	} else {
		/* File number was supplied in the Upload command.  In this situation we send a GO response with the offset
		 * that the station should use to continue the upload.  Space should still be available as it was allocated
		 * before.  If there is an error then that is returned instead of GO.
		 * If there is an upload record then that is not changed by a continue, even if there is an error.

		 * We first need to check the situation where we have the complete file but the ground station never received the ACK.
		 * So If we get a continue request and the offset is at the end of the file and the file is on the disk, then we send
		 * ER_FILE_COMPLETE.
		 *
		 * Then we check the upload record.  The length must be the same or this is an error.
		 *
		 * Then we check the file exists on disk at the right offset.
		 *
		 */

		char file_name_with_path[MAX_FILE_PATH_LEN];
		dir_get_file_path_from_file_id(state->file_id, get_dir_folder(), file_name_with_path, MAX_FILE_PATH_LEN);
		debug_print("Checking if file: %s is already uploaded\n",file_name_with_path);

		FILE * f = fopen(file_name_with_path, "rb");
		if (f != NULL) { // File is already on disk
			debug_print("File is already on disk\n");
			int32_t off = fseek(f, 0, SEEK_END);
			int32_t rc = fclose(f);
			if (rc != 0) {
				debug_print("Unable to close %s: %s\n", file_name_with_path, strerror(errno));
			}
			if (off == -1) {
				debug_print("Unable to seek %s  to end: %s\n", file_name_with_path, strerror(errno));
				return ER_NO_SUCH_FILE_NUMBER; // something is wrong with this file - tell ground station to ask for a new number
			} else {
				if (state->length == off) { // we have the full file
					debug_print("FTL0[%d]: We already have file %04x at final offset %d -- ER FILE COMPLETE\n",state->channel, state->file_id, state->offset);
					return ER_FILE_COMPLETE;
				} else {
					// Somehow the file is on disk but the wrong length.  So we must treat this as a new file
					debug_print("File on disk has wrong length %s\n", file_name_with_path);
					return ER_NO_SUCH_FILE_NUMBER; // something is wrong with this file - tell ground station to ask for a new number
				}
			}
		}

		/* Is this a valid continue, check to see there is an upload record */
		InProcessFileUpload_t upload_record;
		if (ftl0_get_file_upload_record(state->file_id, &upload_record) != EXIT_SUCCESS)  {
			debug_print("Could not read upload record for file id %04x - FAILED\n",state->file_id);
			return ER_NO_SUCH_FILE_NUMBER;
		} else {
			/* if <continue_file_no> is not 0 and the <file_length> does not
		               agree with the <file_length> previously associated with the file identified by
		               <continue_file_no>.  Continue is not possible.*/
			if (upload_record.length != state->length) {
				debug_print("Promised file length does not match - BAD CONTINUE\n");
				return ER_BAD_CONTINUE;
			}
			/* If this file does not belong to this callsign then reject */
			if (strcmp(upload_record.callsign, state->callsign) != 0) {
				debug_print("Callsign does not match - BAD CONTINUE\n");
				return ER_BAD_CONTINUE;
			}
		}

		/* Is this a valid continue, check to see there is a tmp file */
		char tmp_filename[MAX_FILE_PATH_LEN];
		dir_get_upload_file_path_from_file_id(state->file_id, tmp_filename, MAX_FILE_PATH_LEN);

		debug_print("Checking continue file: %s\n",tmp_filename);
		f = fopen(tmp_filename, "rb");
		if (f == NULL) {
			error_print("No such file number \n");
			return ER_NO_SUCH_FILE_NUMBER;
		}
		fseek(f, 0L, SEEK_END);
		int off = ftell(f);
		if (off == -1) {
			error_print("Unable to seek %s  to end: %s\n", tmp_filename, strerror(errno));
			fclose(f);
			return ER_NO_SUCH_FILE_NUMBER;
		} else {
			state->offset = off;
			debug_print("FTL0[%d]: Continuing file %04x at offset %d\n",state->channel, state->file_id, state->offset);
		}
		fclose(f);

		ul_go_data.server_file_no = state->file_id;
		ul_go_data.byte_offset = state->offset; // this is the end of the file so far

	}

	unsigned char data_bytes[sizeof(ul_go_data)+2];

	int rc = ftl0_make_packet(data_bytes, (unsigned char *)&ul_go_data, sizeof(ul_go_data), UL_GO_RESP);
	if (rc != EXIT_SUCCESS) {
		debug_print("Could not make FTL0 UL GO packet \n");
		return ER_ILL_FORMED_CMD; // TODO This will cause err 1 to be sent and the station to be offloaded.  Is that right..
	}
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
	dir_get_upload_file_path_from_file_id(uplink_list[selected_station].file_id, tmp_filename, MAX_FILE_PATH_LEN);
	//debug_print("Saving data to file: %s\n",tmp_filename);
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

	uplink_list[selected_station].offset += ftl0_length;
	if (uplink_list[selected_station].offset > uplink_list[selected_station].length) {
		debug_print("User tried to upload more bytes than were reserved for the file: %s\n",tmp_filename);
		return ER_NO_ROOM; // The user has tried to upload more bytes than reserved for this file
	}
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
	dir_get_upload_file_path_from_file_id(uplink_list[selected_station].file_id, tmp_filename, MAX_FILE_PATH_LEN);

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
	dir_get_file_path_from_file_id(uplink_list[selected_station].file_id, get_dir_folder(), new_filename, MAX_FILE_PATH_LEN);
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

int ftl0_on_the_uplink_now(uint32_t file_id) {
	int j;
	for (j=0; j < number_on_uplink; j++) {
		if (uplink_list[j].state != UL_UNINIT)
			if (uplink_list[j].file_id == file_id)
				return true;
	}
	return false;
}

/**
 * Read a record from the file upload table slot
 */
int ftl0_raw_get_file_upload_record(uint32_t slot, InProcessFileUpload_t * file_upload_record) {
	if (slot >= MAX_IN_PROCESS_FILE_UPLOADS) return EXIT_FAILURE;
	*file_upload_record = upload_table[slot];
	return EXIT_SUCCESS;
}

/**
 * Write a record to the file upload table
 */
int ftl0_raw_set_file_upload_record(uint32_t slot, InProcessFileUpload_t * file_upload_record) {
	if (slot >= MAX_IN_PROCESS_FILE_UPLOADS) return EXIT_FAILURE;
	upload_table[slot] = *file_upload_record;
    ftl0_save_upload_table(); // if this fails we ignore it as it is not fatal
    return EXIT_SUCCESS;
}

/**
 * Given a file_id, return the in process file upload record.
 * Returns true if the record exists or false if it does not exist
 *
 */
int ftl0_get_file_upload_record(uint32_t file_id, InProcessFileUpload_t * file_upload_record) {
    int i;
    for (i=0; i < MAX_IN_PROCESS_FILE_UPLOADS; i++) {
        int rc = ftl0_raw_get_file_upload_record(i, file_upload_record);
        if (rc == EXIT_FAILURE) return EXIT_FAILURE;
        if (file_id == file_upload_record->file_id)
            return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

/**
 * Given an upload record with a file_id, store the in process file upload record.
 * If the table is full then the oldest record is removed and replaced.
 * Return true if it could be stored or false otherwise.
 */
int ftl0_set_file_upload_record(InProcessFileUpload_t * file_upload_record) {
    int i;
    int oldest_id = -1;
    int oldest_file_id = -1;
    int first_empty_id = -1;
    uint32_t oldest_date = 0xFFFFFFFF;
    InProcessFileUpload_t tmp_file_upload_record;
    /* Check that we do not already have this record, note the first empty slot and the oldest slot */
    for (i=0; i < MAX_IN_PROCESS_FILE_UPLOADS; i++) {
        int rc = ftl0_raw_get_file_upload_record(i, &tmp_file_upload_record);
        if (rc == EXIT_FAILURE) return EXIT_FAILURE;
        if (tmp_file_upload_record.file_id == file_upload_record->file_id) {
            /* This file id is already being uploaded, so this is an error */
            return EXIT_FAILURE;
        }
        if (first_empty_id == -1 && tmp_file_upload_record.file_id == 0) {
            /* This is an empty slot, store it here.  We on't have to look further */
            first_empty_id = i;
            break;
        } else {
            /* Slot is occupied, see if it is the oldest slot in case we need to replace it */
            if (tmp_file_upload_record.request_time < oldest_date) {
                /* This time is older than the oldest date so far.  Make sure this is not
                 * live right now and then note it as the oldest */
                if (!ftl0_on_the_uplink_now(tmp_file_upload_record.file_id)) {
                    oldest_id = i;
                    oldest_file_id = tmp_file_upload_record.file_id;
                    oldest_date = tmp_file_upload_record.request_time;
                }
            }
        }
    }
    if (first_empty_id != -1) {
        int rc2 = ftl0_raw_set_file_upload_record(first_empty_id, file_upload_record);
        //debug_print("Store in empty slot %d\n",first_empty_id);
        if (rc2 == EXIT_FAILURE) return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }

    /* We could not find a slot so overwrite the oldest. */
    if (oldest_id != -1) {
        //debug_print("Store in oldest slot %d\n",oldest_id);
        int rc3 = ftl0_raw_set_file_upload_record(oldest_id, file_upload_record);
        if (rc3 == EXIT_FAILURE) {
        	return EXIT_FAILURE;
        } else {
            /* Remove the temp file on disk */
            ftl0_remove_upload_file(oldest_file_id);
        }
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

/**
 * Given an upload record with a file_id, update the file upload record.
 * Return true if it could be updated or false otherwise.
 */
int ftl0_update_file_upload_record(InProcessFileUpload_t * file_upload_record) {
    int i;
    InProcessFileUpload_t tmp_file_upload_record;
    /* Find the record and update it */
    for (i=0; i < MAX_IN_PROCESS_FILE_UPLOADS; i++) {
        int rc = ftl0_raw_get_file_upload_record(i, &tmp_file_upload_record);
        if (rc == EXIT_FAILURE) return EXIT_FAILURE;
        if (tmp_file_upload_record.file_id == file_upload_record->file_id) {
            /* Update the record in this slot */
            int rc3 = ftl0_raw_set_file_upload_record(i, file_upload_record);
            if (rc3 == EXIT_FAILURE) return EXIT_FAILURE;
            return EXIT_SUCCESS;
        }
    }

    return EXIT_FAILURE;
}

/**
 * ftl0_remove_file_upload_record()
 * Remove the record from the upload table based on the file id
 * Return EXIT_SUCCESS if it was removed or did not exist.  Otherwise return EXIT_FAILURE;
 */
int ftl0_remove_file_upload_record(uint32_t id) {
    InProcessFileUpload_t tmp_file_upload_record;
    InProcessFileUpload_t tmp_file_upload_record2;
    tmp_file_upload_record.file_id = 0;
    tmp_file_upload_record.length = 0;
    tmp_file_upload_record.request_time = 0;
    tmp_file_upload_record.callsign[0] = 0;
    tmp_file_upload_record.offset = 0;

    int i;
    for (i=0; i < MAX_IN_PROCESS_FILE_UPLOADS; i++) {
        int rc = ftl0_raw_get_file_upload_record(i, &tmp_file_upload_record2);
        if (rc == EXIT_FAILURE) return EXIT_FAILURE;
        if (tmp_file_upload_record2.file_id == id) {
            if (ftl0_raw_set_file_upload_record(i, &tmp_file_upload_record) != EXIT_SUCCESS) {
                return EXIT_FAILURE;
            } else {
            	 /* Remove the temp file on disk */
            	ftl0_remove_upload_file(id);
            }
            return EXIT_SUCCESS;
        }
    }
    // Nothing to remove
    return EXIT_SUCCESS;
}

/**
 * Calculate and return the total space consumed by the upload table.  This indicates
 * how much data we are expecting to receive from uploaded files.  If we want to guarantee
 * they can be uploaded them we need to keep this amount of space free.
 *
 */
int ftl0_get_space_reserved_by_upload_table() {
    int i;
    uint32_t space_reserved = 0;
    InProcessFileUpload_t rec;

    for (i=0; i < MAX_IN_PROCESS_FILE_UPLOADS; i++) {
        if (ftl0_raw_get_file_upload_record(i, &rec) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        if (rec.file_id != 0) {
            space_reserved += (rec.length - rec.offset); /* We exclude the offset because that will be included in the space consumed on the disk */
        }
    }
    return space_reserved;
}

/**
 * ftl0_clear_upload_table()
 * This does not re-save the empty file.  If that file needs to be cleared then save
 * must be called seperately
 */
int ftl0_clear_upload_table() {
    int i;
    for (i=0; i < MAX_IN_PROCESS_FILE_UPLOADS; i++) {
    	upload_table[i].file_id = 0;
    	upload_table[i].length = 0;
    	upload_table[i].request_time = 0;
    	upload_table[i].callsign[0] = 0;
    	upload_table[i].offset = 0;
    }
    return EXIT_SUCCESS;
}

/**
 * Load the upload table from disk.
 */
int ftl0_load_upload_table() {
	debug_print("Loading upload table from: %s:\n", g_upload_table_path);
	FILE *file = fopen ( g_upload_table_path, "r" );
	if ( file == NULL ) {
		error_print("Could not load upload table file: %s\n", g_upload_table_path);
		return EXIT_FAILURE;
	}
	int i = 0;
	char *search = ",";
	char line [ MAX_CONFIG_LINE_LENGTH ]; /* or other suitable maximum line size */
	char *token;
	while ( fgets ( line, sizeof line, file ) != NULL ) /* read a line */ {

		/* Token will point to the part before the , */
		token = strtok(line, search);
		//debug_print("%s",token);
		int id = atoi(token);
		upload_table[i].file_id = id;

		token = strtok(NULL, search);
		//debug_print(" , %s",token);
		int len = atoi(token);
		upload_table[i].length = len;

		token = strtok(NULL, search);
		//debug_print(" , %s",token);
		time_t t = atol(token);
		upload_table[i].request_time = t;

		token = strtok(NULL, search);
		//debug_print(" , %s",token);
		strlcpy(upload_table[i].callsign, token,sizeof(upload_table[i].callsign));

		token = strtok(NULL, search);
		token[strcspn(token,"\n")] = 0; // Remove the nul termination to get rid of the new line
		//debug_print(" , %s\n",token);
		int off = atoi(token);
		upload_table[i].offset = off;
		i++;
		if (i > MAX_IN_PROCESS_FILE_UPLOADS) {
			ftl0_clear_upload_table();
			return EXIT_FAILURE; // probablly the wrong file with too many lines
		}
	}
	fclose ( file );
	return EXIT_SUCCESS;
}

int ftl0_save_upload_table() {
	//debug_print("Saving upload table to: %s:\n", g_upload_table_path);
	int i;
	char buf[MAX_CONFIG_LINE_LENGTH];
	FILE *file = fopen ( g_upload_table_path, "w" );
	if (file == NULL) {
		debug_print("Unable to open %s for writing: %s\n", g_upload_table_path, strerror(errno));
		return EXIT_FAILURE;
	}

	for (i=0; i < MAX_IN_PROCESS_FILE_UPLOADS; i++) {
		if (upload_table[i].callsign[0] == 0)
			strlcpy(upload_table[i].callsign, "NONE",sizeof(upload_table[i].callsign));

		snprintf(buf, MAX_CONFIG_LINE_LENGTH, "%d,%d,%d,%s,%d\n",upload_table[i].file_id,upload_table[i].length,upload_table[i].request_time
				,upload_table[i].callsign,upload_table[i].offset);
		fputs(buf, file);
	}
	fclose(file);
	return EXIT_SUCCESS;
}

int ftl0_remove_upload_file(uint32_t file_id) {
	// Remove the tmp file
	char file_name_with_path[MAX_FILE_PATH_LEN];
	dir_get_upload_file_path_from_file_id(file_id, file_name_with_path, MAX_FILE_PATH_LEN);
	int32_t fp = remove(file_name_with_path);
	if (fp == -1) {
		debug_print("Unable to remove tmp file: %s : %s\n", file_name_with_path, strerror(errno));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


int ftl0_debug_list_upload_table() {
    int i;
    InProcessFileUpload_t rec;

    for (i=0; i < MAX_IN_PROCESS_FILE_UPLOADS; i++) {
        if (ftl0_raw_get_file_upload_record(i, &rec) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        if (rec.file_id != 0) {
            uint32_t now = time(0);
            debug_print("%d- File: %04x by %s length: %d offset: %d for %d seconds\n",i, rec.file_id, rec.callsign, rec.length, rec.offset, now-rec.request_time);
        }
    }
    uint32_t space = ftl0_get_space_reserved_by_upload_table();
    debug_print("Total Space Allocated: %d\n",space);
    return EXIT_SUCCESS;
}

/**
 * ftl0_maintenance()
 * Remove expired entries from the file upload table and delete their tmp file on disk
 * Remove any orphaned tmp files on disk
 *
 */
void ftl0_maintenance(time_t now, char *upload_folder) {
    //debug_print("Running FTL0 Maintenance\n");

    // First remove any expired entries in the table
    int i;
    InProcessFileUpload_t rec;
    InProcessFileUpload_t blank_file_upload_record;
    blank_file_upload_record.file_id = 0;
    blank_file_upload_record.length = 0;
    blank_file_upload_record.request_time = 0;
    blank_file_upload_record.callsign[0] = 0;
    blank_file_upload_record.offset = 0;

    for (i=0; i < MAX_IN_PROCESS_FILE_UPLOADS; i++) {
        if (ftl0_raw_get_file_upload_record(i, &rec) != EXIT_SUCCESS) {
            // skip and keep going in case this is temporary;
        	continue;
        }
        if (rec.file_id != 0) {
        	if (!ftl0_on_the_uplink_now(rec.file_id)) {
        		int32_t age = now-rec.request_time;
        		if (age > g_ftl0_max_upload_age_in_seconds) {
        			debug_print("REMOVING RECORD: %d- File: %04x by %s length: %d offset: %d for %ld seconds\n",i,
        					rec.file_id, rec.callsign, rec.length, rec.offset, now-rec.request_time);
        			if (ftl0_raw_set_file_upload_record(i, &blank_file_upload_record) == EXIT_SUCCESS) {
        				ftl0_remove_upload_file(rec.file_id);
        			} else {
        				debug_print(" FTL0 Maintenance - Could not remove upload record %d\n",i);
        			}
        		}
        	}
        }
    }

    // Next remove any orphaned tmp files
	struct dirent *pDirEnt;
    //printf("Checking TMP Directory from %s:\n",upload_folder);
    DIR * pDir = opendir(upload_folder);
    if (pDir == NULL) {
        debug_print("Unable to open tmp folder: %s\n", strerror(errno));
        return;
    }

    errno = 0; /* Set error to zero so we can distinguish between a real error and the end of the DIR */
    pDirEnt = readdir(pDir);
    while (pDirEnt != NULL) {
    	if ((strcmp(pDirEnt->d_name, ".") != 0) && (strcmp(pDirEnt->d_name, "..") != 0)){
            //debug_print("Checking: %s\n",pDirEnt->d_name);

            char file_name_with_path[MAX_FILE_PATH_LEN];
            strlcpy(file_name_with_path, upload_folder, MAX_FILE_PATH_LEN);
            strlcat(file_name_with_path, "/", MAX_FILE_PATH_LEN);
            strlcat(file_name_with_path, pDirEnt->d_name, MAX_FILE_PATH_LEN);

            uint32_t id = dir_get_file_id_from_filename(pDirEnt->d_name);
            if (id == 0 || ftl0_get_file_upload_record(id, &rec) != EXIT_SUCCESS) {
                debug_print("Could not find file %s in upload table\n",pDirEnt->d_name);
                // If this is not in the upload table then remove the file
                int32_t fp = remove(file_name_with_path);
                if (fp == -1) {
                    debug_print("Unable to remove orphaned tmp file: %s : %s\n", file_name_with_path, strerror(errno));
                } else {
                	debug_print("Removed orphaned tmp file: %s\n", file_name_with_path);
                }
            }
        }
        pDirEnt = readdir(pDir);
    }
    if (errno != 0) {
        debug_print("*** Error reading tmp directory: %s\n", strerror(errno));
    }
    int32_t rc2 = closedir(pDir);
    if (rc2 != 0) {
        debug_print("*** Unable to close tmp dir: %s\n", strerror(errno));
    }

}

/*********************************************************************************************
 *
 * SELF TESTS FOLLOW
 *
 */
void test_touch(char *f) {
	FILE *file = fopen ( f, "w" );
	fclose(file);
}

int test_ftl0_upload_table() {
    printf("##### TEST UPLOAD TABLE:\n");
    int rc = EXIT_SUCCESS;
	mkdir("/tmp/pacsat",0777);

    char * upload_folder = "/tmp/pacsat/upload";
    dir_init("/tmp");

    if (ftl0_clear_upload_table() != EXIT_SUCCESS) { debug_print("Could not clear upload table - FAILED\n"); return EXIT_FAILURE;}

    /* Test core set/get functions */
    InProcessFileUpload_t file_upload_record;
    strlcpy(file_upload_record.callsign,"G0KLA", sizeof(file_upload_record.callsign));
    file_upload_record.file_id = 9;
    file_upload_record.length = 12345;
    file_upload_record.request_time = 1692394562;
    file_upload_record.offset = 0;

    /* Store in the middle of the table.  In a later test this will be the oldest. */
    if (ftl0_raw_set_file_upload_record(8, &file_upload_record) != EXIT_SUCCESS) {  debug_print("Could not add record - FAILED\n"); return EXIT_FAILURE; }

    InProcessFileUpload_t record;
    if (ftl0_raw_get_file_upload_record(8, &record) != EXIT_SUCCESS)  {  debug_print("Could not read record - FAILED\n"); return EXIT_FAILURE; }

    if (record.file_id != file_upload_record.file_id)  {  debug_print("Wrong file id - FAILED\n"); return EXIT_FAILURE; }
    if (record.length != file_upload_record.length)  {  debug_print("Wrong length - FAILED\n"); return EXIT_FAILURE; }
    if (record.request_time != file_upload_record.request_time)  {  debug_print("Wrong request_time - FAILED\n"); return EXIT_FAILURE; }
    if (strcmp(record.callsign, file_upload_record.callsign) != 0)  {  debug_print("Wrong callsign - FAILED\n"); return EXIT_FAILURE; }

    uint32_t space = ftl0_get_space_reserved_by_upload_table();
    if (space != file_upload_record.length)  {  debug_print("Wrong table space - FAILED\n"); return EXIT_FAILURE; }

    /* Now test adding by file id */
    InProcessFileUpload_t file_upload_record2;
    strlcpy(file_upload_record2.callsign,"AC2CZ", sizeof(file_upload_record.callsign));
    file_upload_record2.file_id = 1010;
    file_upload_record2.length = 659;
    file_upload_record2.request_time = 1692394562+1;
    file_upload_record2.offset = 0;

    if (ftl0_set_file_upload_record(&file_upload_record2) != EXIT_SUCCESS ) {  debug_print("Could not add record2 - FAILED\n"); return EXIT_FAILURE; }

    InProcessFileUpload_t record2;
    if (ftl0_get_file_upload_record(1010, &record2) != EXIT_SUCCESS)  {  debug_print("Could not read record2 - FAILED\n"); return EXIT_FAILURE; }

    if (record2.file_id != file_upload_record2.file_id)  {  debug_print("Wrong file id for record 2 - FAILED\n"); return EXIT_FAILURE; }
    if (record2.length != file_upload_record2.length)  {  debug_print("Wrong length for record 2 - FAILED\n"); return EXIT_FAILURE; }
    if (record2.offset != file_upload_record2.offset)  {  debug_print("Wrong offset for record 2 - FAILED\n"); return EXIT_FAILURE; }
    if (record2.request_time != file_upload_record2.request_time)  {  debug_print("Wrong request_time for record 2 - FAILED\n"); return EXIT_FAILURE; }
    if (strcmp(record2.callsign, file_upload_record2.callsign) != 0)  {  debug_print("Wrong callsign for record 2 - FAILED\n"); return EXIT_FAILURE; }

    /* Test update */
    record2.offset = 98;
    if (ftl0_update_file_upload_record(&record2) != EXIT_SUCCESS) {  debug_print("Error - could not update record2 - FAILED\n"); return EXIT_FAILURE; }

    InProcessFileUpload_t record_up;
    if (ftl0_get_file_upload_record(1010, &record_up) != EXIT_SUCCESS)  {  debug_print("Could not read record_up - FAILED\n"); return EXIT_FAILURE; }

    if (record_up.file_id != file_upload_record2.file_id)  {  debug_print("Wrong file id for record_up - FAILED\n"); return EXIT_FAILURE; }
    if (record_up.length != file_upload_record2.length)  {  debug_print("Wrong length for record_up - FAILED\n"); return EXIT_FAILURE; }
    if (record_up.offset != 98)  {  debug_print("Wrong offset for record_up - FAILED\n"); return EXIT_FAILURE; }
    if (record_up.request_time != file_upload_record2.request_time)  {  debug_print("Wrong request_time for record_up - FAILED\n"); return EXIT_FAILURE; }
    if (strcmp(record_up.callsign, file_upload_record2.callsign) != 0)  {  debug_print("Wrong callsign for record_up - FAILED\n"); return EXIT_FAILURE; }

    /* Test add duplicate file id - error */
    InProcessFileUpload_t file_upload_record3;
    strlcpy(file_upload_record3.callsign,"VE2TCP", sizeof(file_upload_record.callsign));
    file_upload_record3.file_id = 1010;
    file_upload_record3.length = 6539;
    file_upload_record3.request_time = 1692394562+2;
    file_upload_record3.offset = 0;

    if (ftl0_set_file_upload_record(&file_upload_record3) != EXIT_FAILURE) {  debug_print("Error - added duplicate file id for record3 - FAILED\n"); return EXIT_FAILURE; }

    /* Now test that we replace the oldest if all slots are full.  Currently we have added two records. */
    InProcessFileUpload_t tmp_file_upload_record;
    strlcpy(tmp_file_upload_record.callsign,"D0MMY", sizeof(file_upload_record.callsign));
    tmp_file_upload_record.length = 123;
    int j;
    for (j=0; j < MAX_IN_PROCESS_FILE_UPLOADS; j++) {
        tmp_file_upload_record.file_id = 100 + j;
        tmp_file_upload_record.request_time = 1692394562 + 3 + j;
        tmp_file_upload_record.offset = 0;
        if (ftl0_set_file_upload_record(&tmp_file_upload_record) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
        char f[MAX_FILE_PATH_LEN];
    	dir_get_upload_file_path_from_file_id(tmp_file_upload_record.file_id,f,MAX_FILE_PATH_LEN);
        test_touch(f);
    }

    if (ftl0_debug_list_upload_table() != EXIT_SUCCESS) { debug_print("Could not print upload table - FAILED\n"); return EXIT_FAILURE; }

    /* The last two records added above should have replaced older records, with the second to last being in slot 8 */
    InProcessFileUpload_t record4;
    if (ftl0_raw_get_file_upload_record(8, &record4) != EXIT_SUCCESS)  {  debug_print("Could not read oldest record - FAILED\n"); return EXIT_FAILURE; }

    if (record4.file_id != (100 + MAX_IN_PROCESS_FILE_UPLOADS-2))  {  debug_print("Wrong second oldest file id - FAILED\n"); return EXIT_FAILURE; }
    if (record4.length != 123)  {  debug_print("Wrong length - FAILED\n"); return EXIT_FAILURE; }
    if (record4.request_time != 1692394562 + 3 + MAX_IN_PROCESS_FILE_UPLOADS-2)  {  debug_print("Wrong oldest request_time - FAILED\n"); return EXIT_FAILURE; }
    if (strcmp(record4.callsign, "D0MMY") != 0)  {  debug_print("Wrong oldest callsign - FAILED\n"); return EXIT_FAILURE; }

    /* Now clear an upload record as though it completes or is purged */
    if (ftl0_remove_file_upload_record(105) != EXIT_SUCCESS)  {  debug_print("Could not remove record for id 105 - FAILED\n"); return EXIT_FAILURE; }

    InProcessFileUpload_t record5;
    if (ftl0_get_file_upload_record(105, &record5) != EXIT_FAILURE)  {  debug_print("ERROR: Should not be able to read record5 - FAILED\n"); return EXIT_FAILURE; }

    /* Now add a new record and it should go exactly in that empty slot.  Record 105 was the 6th record added above
     * and only slot 0 and 5 were full.  So it should be in slot 6  */
    InProcessFileUpload_t file_upload_record6;
    strlcpy(file_upload_record6.callsign,"VE2TCP", sizeof(file_upload_record.callsign));
    file_upload_record6.file_id = 0x9990;
    file_upload_record6.length = 123999;
    file_upload_record6.request_time = 999;
    file_upload_record6.offset = 122999;
    char f6[MAX_FILE_PATH_LEN];
    dir_get_upload_file_path_from_file_id(file_upload_record6.file_id,f6,MAX_FILE_PATH_LEN);
    test_touch(f6);

    if (ftl0_set_file_upload_record(&file_upload_record6) != EXIT_SUCCESS) {  debug_print("Error - could not add record6 - FAILED\n"); return EXIT_FAILURE; }
    if (access("/tmp/pacsat/upload/0069.upload", F_OK) == 0) {  debug_print("ERROR: File 0069.upload still there after replaced - FAILED\n"); return EXIT_FAILURE; }

    InProcessFileUpload_t record7;
    if (ftl0_raw_get_file_upload_record(6, &record7) != EXIT_SUCCESS)  {  debug_print("ERROR: Could not read slot 6 - FAILED\n"); return EXIT_FAILURE; }
    if (record7.file_id != 0x9990)  {  debug_print("Wrong file id in slot 6- FAILED\n"); return EXIT_FAILURE; }

//    if (ftl0_debug_list_upload_table() != EXIT_SUCCESS) { debug_print("Could not print upload table - FAILED\n"); return EXIT_FAILURE; }

    int reserved = 123 * (MAX_IN_PROCESS_FILE_UPLOADS-1) + 1000;
    if (ftl0_get_space_reserved_by_upload_table() != reserved) { debug_print("Wrong space reserved: %d  - FAILED\n",reserved); return EXIT_FAILURE; }

    if (ftl0_clear_upload_table() != EXIT_SUCCESS) { debug_print("Could not clear upload table - FAILED\n"); return EXIT_FAILURE;}

    if (ftl0_load_upload_table() != EXIT_SUCCESS) { debug_print("Could not load upload table - FAILED\n"); return EXIT_FAILURE;}
    if (ftl0_debug_list_upload_table() != EXIT_SUCCESS) { debug_print("Could not print upload table - FAILED\n"); return EXIT_FAILURE; }

    // Reread and this should be the same
    if (ftl0_raw_get_file_upload_record(6, &record7) != EXIT_SUCCESS)  {  debug_print("ERROR: Could not read slot 6 - FAILED\n"); return EXIT_FAILURE; }
    if (record7.file_id != 0x9990)  {  debug_print("Wrong file id in slot 6- FAILED\n"); return EXIT_FAILURE; }

    debug_print("TEST MAINT\n");
    // Test the files which were uploaded at 1 second intervals from 1692394562 to 1692394562 + 2 + MAX_IN_PROCESS_FILE_UPLOADS
    g_ftl0_max_upload_age_in_seconds = MAX_IN_PROCESS_FILE_UPLOADS - 5;  // this should purge 5 files
    test_touch("/tmp/pacsat/upload/fred");  // orphaned file that will be cleaned up

    ftl0_maintenance(1692394562 + 2 + MAX_IN_PROCESS_FILE_UPLOADS, upload_folder);  // this is time of the final record uploaded
    // Check a slot that should survive
    if (ftl0_raw_get_file_upload_record(5, &record7) != EXIT_SUCCESS)  {  debug_print("ERROR: Could not read slot 5 after maint() - FAILED\n"); return EXIT_FAILURE; }
    if (record7.file_id != 104)  {  debug_print("ERROR: slot 5 has data after maint() - FAILED\n"); return EXIT_FAILURE; }
    if (access("/tmp/pacsat/upload/0068.upload", F_OK) != 0) {  debug_print("ERROR: file 0068.upload missing after maint() - FAILED\n"); return EXIT_FAILURE; }

    // Check a slot that should be purged
    if (ftl0_raw_get_file_upload_record(6, &record7) != EXIT_SUCCESS)  {  debug_print("ERROR: Could not read slot 6 after maint() - FAILED\n"); return EXIT_FAILURE; }
    if (record7.file_id != 0)  {  debug_print("ERROR: slot 6 has data after maint() - FAILED\n"); return EXIT_FAILURE; }

    // Check orphan file gone
    if (access("/tmp/pacsat/upload/fred", F_OK) == 0) {  debug_print("ERROR: orphan file fred still there after maint() - FAILED\n"); return EXIT_FAILURE; }

    if (ftl0_debug_list_upload_table() != EXIT_SUCCESS) { debug_print("Could not print upload table - FAILED\n"); return EXIT_FAILURE; }

    /* And reset everything */
    if (ftl0_clear_upload_table() != EXIT_SUCCESS) { debug_print("Could not clear upload table - FAILED\n"); return EXIT_FAILURE;}
    if (ftl0_save_upload_table() != EXIT_SUCCESS) { debug_print("Could not save empty upload table - FAILED\n"); return EXIT_FAILURE;}

    if (rc == EXIT_SUCCESS)
        printf("##### TEST UPLOAD TABLE: success:\n");
    else
        printf("##### TEST UPLOAD TABLE: fail:\n");

    return rc;

}
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
	//debug_print("Saving data to file: %s\n",filename);
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
