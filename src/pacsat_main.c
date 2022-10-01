/*
 * pacsat_main.c
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
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

/* Program Include files */
#include "config.h"
#include "agw_tnc.h"
#include "util.h"

void connection_received(char *from_callsign, char *to_callsign, int incomming, char * data);

/*
 *  GLOBAL VARIABLES defined here.  They are declared in config.h
 *  These are the default values.  Many can be updated with a value
 *  in telem_radio.config or can be over riden on the command line.
 *
 */
int g_verbose = false;
char g_bbs_callsign[10] = "PFS3-12";
char g_broadcast_callsign[10] = "PFS3-11";

/* Local variables */
pthread_t tnc_listen_pthread;

//// TEMP VARS FOR TESTING PB
	int sent_pb_status = false;
	int on_pb = false;

int main(int argc, char *argv[]) {

	printf("PACSAT In-orbit Server\n");
	printf("Build: %s\n", VERSION);

	int rc = EXIT_SUCCESS;

	rc = tnc_connect("127.0.0.1", AGW_PORT);
	if (rc != EXIT_SUCCESS) {
		printf("\n Error : Could not connect to TNC \n");
		exit(EXIT_FAILURE);
	}

	rc = tnc_start_monitoring('k'); // k monitors raw frames
//	rc = tnc_start_monitoring('m');
	if (rc != EXIT_SUCCESS) {
		printf("\n Error : Could not monitor TNC \n");
		exit(EXIT_FAILURE);
	}

	rc = tnc_register_callsign(g_bbs_callsign);
	if (rc != EXIT_SUCCESS) {
		printf("\n Error : Could not register callsign with TNC \n");
		exit(EXIT_FAILURE);
	}

	printf("Start listen thread.\n");
	char *name = "TNC Listen Thread";
	rc = pthread_create( &tnc_listen_pthread, NULL, tnc_listen_process, (void*) name);
	if (rc != EXIT_SUCCESS) {
		printf("FATAL. Could not start the TNC listen thread.\n");
		exit(rc);
	}

//	char command[] = "PB Empty.";



	int frame_num = 0;
	while(1) {
		struct t_agw_frame_ptr frame;
		int rc = get_next_frame(frame_num, &frame);

		if (rc == EXIT_SUCCESS) {

			frame_num++;
			if (frame_num == MAX_RX_QUEUE_LEN)
				frame_num=0;

			switch (frame.header->data_kind) {
			case 'X': // Response to callsign registration
				break;
			case 'K': // Monitored frame
				debug_print("FRM:%d:",frame_num);
				print_header(frame.header);
				print_data(frame.data, frame.header->data_len);
				debug_print("\n");

				process_monitored_frame (frame.header->call_from, frame.header->call_to, frame.data, frame.header->data_len);
				break;
			case 'C': // Connected to a station
				debug_print("FRM:%d:",frame_num);
				print_header(frame.header);
				print_data(frame.data, frame.header->data_len);
				debug_print("\n");

				if (strncmp(frame.data, "*** CONNECTED To Station", 24) == 0) {
					// Incoming: Other station initiated the connect request.
					connection_received (frame.header->call_from, frame.header->call_to, 1, frame.data);
				}
				else if (strncmp(frame.data, "*** CONNECTED With Station", 26) == 0) {
					// Outgoing: Other station accepted my connect request.
					connection_received (frame.header->call_from, frame.header->call_to, 0, frame.data);
				}
				break;
			}
		}

		if (on_pb) {
			// Send a test DIR header

		}

		usleep(1000); // sleep 1000uS

		if (!sent_pb_status) { // TEST - this should be a timer
			// then send the status
			char command[] = "PB Empty.";
			rc = send_ui_packet(g_bbs_callsign, "PBLIST", command, sizeof(command));
			if (rc != EXIT_SUCCESS) {
				printf("\n Error : Could not send PB status to TNC \n");
			}
			sent_pb_status = true;
		}
	}

	/* For testing wait until the TNC thread returns.  Otherwise the program just ends. */
	int *return_code;
	pthread_join(tnc_listen_pthread, return_code);
	exit(return_code);

}



struct FRAME_HEADER {
     unsigned char flags;
     unsigned int file_id : 32;
     unsigned char file_type;
     unsigned int offset : 16;
     unsigned char offset_msb;
};

struct DIR_HEADER { // sent by Pacsat
	unsigned char flags;
	unsigned int file_id : 32;
	unsigned int offset : 32;
	time_t t_old;
	time_t t_new;
};

struct DIR_REQ_HEADER{ // sent by client
	unsigned char flags;
	unsigned int block_size : 16;
};

struct PAIR {
	time_t start;
	time_t end;
};

#define BROADCAST_REQUEST_HEADER_SIZE 17
struct t_broadcast_request_header {
	unsigned char flag;
	unsigned char to_callsign[7];
	unsigned char from_callsign2[7];
	unsigned char control_byte;
	unsigned char pid;
};

void process_monitored_frame(char *from_callsign, char *to_callsign, char * data, int len) {
	if (strncasecmp(to_callsign, g_bbs_callsign, 7) == 0) {
		// this was sent to the BBS Callsign
		debug_print("BBS Request\n");

	}
	if (strncasecmp(to_callsign, g_broadcast_callsign, 7) == 0) {
		// this was sent to the Broadcast Callsign


		struct t_broadcast_request_header *broadcast_request_header;
		broadcast_request_header = (struct t_broadcast_request_header *)data;
		debug_print("pid: %02x \n", broadcast_request_header->pid & 0xff);
		if ((broadcast_request_header->pid & 0xff) == 0xbd) {
			// Dir Request
			struct DIR_REQ_HEADER *dir_header;
			dir_header = (struct DIR_REQ_HEADER *)(data + BROADCAST_REQUEST_HEADER_SIZE);

			/* least sig 2 bits of flags are 00 if this is a fill request */
			if ((dir_header->flags & 0b11) == 0b00) {
				debug_print("DIR FILL REQUEST: flags: %02x BLK_SIZE: %04x\n", dir_header->flags & 0xff, dir_header->block_size &0xffff);
				int rc=0;
				// ACK the station
				char buffer[14]; // OK + 10 char for callsign with SSID
				strlcpy(buffer,"OK ", sizeof(buffer));
				strlcat(buffer, from_callsign, sizeof(buffer));
				rc = send_ui_packet(g_bbs_callsign, "BBSTAT", buffer, sizeof(buffer));
				if (rc != EXIT_SUCCESS) {
					printf("\n Error : Could not send OK Response to TNC \n");
					exit(EXIT_FAILURE);
				}
//				// OK AC2CZ
//				char pbd[] = {0x00, 0x82, 0x86, 0x64, 0x86, 0xB4, 0x40, 0x00, 0xA0, 0x8C, 0xA6,
//								0x66, 0x40, 0x40, 0x17, 0x03, 0xBB, 0x4F, 0x4B, 0x20, 0x41, 0x43, 0x32, 0x43, 0x5A, 0x0D, 0xC0};
//				//send_raw_packet('K', "PFS3-12", "AC2CZ", pbd, sizeof(pbd));
//				// ON PB AC2CZ/D
//				char pbd2[] ={0x00, 0xA0, 0x84, 0x98, 0x92, 0xA6, 0xA8, 0x00, 0xA0, 0x8C, 0xA6, 0x66, 0x40,
//									0x40, 0x17, 0x03, 0xF0, 0x50, 0x42, 0x3A, 0x20, 0x41, 0x43, 0x32, 0x43, 0x5A,
//									0x5C, 0x44, 0x0D};
//				//send_raw_packet('K', "PFS3-12", "AC2CZ", pbd2, sizeof(pbd2));

				// Add to PB - TEST
				on_pb = true;
				char buffer2[14]; // OK + 10 char for callsign with SSID
				strlcpy(buffer2,"PB: ", sizeof(buffer2));
				strlcat(buffer2, from_callsign, sizeof(buffer2));
				rc = send_ui_packet(g_bbs_callsign, "PBLIST", buffer2, sizeof(buffer2));
				if (rc != EXIT_SUCCESS) {
					printf("\n Error : Could not send OK Response to TNC \n");
					exit(EXIT_FAILURE);
				}
			}
		}
		if ((broadcast_request_header->pid & 0xff) == 0xbb) {
			// File Request
			debug_print("FILE REQUEST\n");
		}

	}
}

/**
 * PB
 * This Directory and File broadcast list is a list of callsigns that will receive attention from
 * the PACSAT.  It stores the callsign and the request, which is for a file or a directory.
 *
 * There are two functions.  One adds stations to the PB.  The other checks the PB and sends data if
 * that is needed. If we have finished sending data then it removes the station from the PB.
 *
 * If there are too many callsigns already then we send PBFULL.
 *
 */

void add_to_pb(char *from_callsign, int fileid) {

}

void connection_received(char *from_callsign, char *to_callsign, int incomming, char * data) {
	debug_print("HANDLE CONNECTION FOR FILE UPLOAD\n");
	char loggedin[] = {0x00,0x82,0x86,0x64,0x86,0xB4,0x40,0xE0,0xA0,0x8C,0xA6,0x66,0x40,0x40,0x79,0x00,
			0xF0,0x05,0x02,0x34,0xC4,0xB9,0x5A,0x04};
	send_raw_packet('K', "PFS3-12", "AC2CZ", loggedin, sizeof(loggedin));

	char go[] = {0x00,0x82,0x86,0x64,0x86,0xB4,0x40,0xE0,0xA0,0x8C,0xA6,0x66,0x40,0x40,0x79,0x22,
			0xF0,0x08,0x04,0x4E,0x03,0x00,0x00,0x00,0x00,0x00,0x00};
	send_raw_packet('K', "PFS3-12", "AC2CZ", go, sizeof(go));

	//Disconnect
	//header.data_kind = 'd';
	//header.data_len = 0;
	//int err = send(sockfd, (char*)(&header), sizeof(header), MSG_NOSIGNAL);
	//if (err == -1) {
	//	printf ("Socket Send error with header, Terminating.\n");
	//	exit (1);
	//}
}

