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
#include "str_util.h"
#include "pacsat_header.h"
#include "pacsat_dir.h"
#include "pacsat_broadcast.h"

/* Forward declarations */
void connection_received(char *from_callsign, char *to_callsign, int incomming, char * data);
void process_monitored_frame(char *from_callsign, char *to_callsign, char *data, int len);

/*
 *  GLOBAL VARIABLES defined here.  They are declared in config.h
 *  These are the default values.  Many can be updated with a value
 *  in pacsat.config or can be overridden on the command line.
 *
 */
int g_verbose = false;
char g_bbs_callsign[10] = "PFS3-12";
char g_broadcast_callsign[10] = "PFS3-11";

/* Local variables */
pthread_t tnc_listen_pthread;
int g_run_self_test = false;

int main(int argc, char *argv[]) {

	printf("PACSAT In-orbit Server\n");
	printf("Build: %s\n", VERSION);

	int rc = EXIT_SUCCESS;

	rc = tnc_connect("127.0.0.1", AGW_PORT);
	if (rc != EXIT_SUCCESS) {
		error_print("\n Error : Could not connect to TNC \n");
		exit(EXIT_FAILURE);
	}

	rc = tnc_start_monitoring('k'); // k monitors raw frames, m monitors normal frames
//	rc = tnc_start_monitoring('m');
	if (rc != EXIT_SUCCESS) {
		error_print("\n Error : Could not monitor TNC \n");
		exit(EXIT_FAILURE);
	}

	if (g_run_self_test) {
		debug_print("Running Self Tests..\n");
		rc = test_pacsat_header();
		if (rc != EXIT_SUCCESS) exit(rc);
		rc = test_pacsat_dir();
		if (rc != EXIT_SUCCESS) exit(rc);
		rc = test_pb_list();
		if (rc != EXIT_SUCCESS) exit(rc);
		rc = test_pb();
		if (rc != EXIT_SUCCESS) exit(rc);

		debug_print("ALL TESTS PASSED\n");
		exit (rc);
	}

	/**
	 * Register the callsign that will accept connection requests.
	 */
	rc = tnc_register_callsign(g_bbs_callsign);
	if (rc != EXIT_SUCCESS) {
		error_print("\n Error : Could not register callsign with TNC \n");
		exit(EXIT_FAILURE);
	}

	/**
	 * Start a thread to listen to the TNC.  This will write all received frames into
	 * a circular buffer.  This thread runs in the background and is always ready to
	 * receive data from the TNC.
	 *
	 * The receive loop reads frames from the buffer and processes
	 * them when we have time.
	 */
	printf("Start listen thread.\n");
	char *name = "TNC Listen Thread";
	rc = pthread_create( &tnc_listen_pthread, NULL, tnc_listen_process, (void*) name);
	if (rc != EXIT_SUCCESS) {
		error_print("FATAL. Could not start the TNC listen thread.\n");
		exit(rc);
	}

	/* Initialize the directory */
	debug_print("LOAD DIR\n");
	if (dir_init("/tmp/test_dir") != EXIT_SUCCESS) { error_print("** Could not initialize the dir\n"); return EXIT_FAILURE; }
	dir_load();

//	char command[] = "PB Empty.";

	/**
	 * RECEIVE LOOP
	 * Each time there is a new frame available in the receive buffer, process it.
	 * We expect only these types of frames:
	 *
	 * DIR REQUEST
	 * New DIR requests are added to the Pacsat Broadcast (PB) queue unless it is full
	 *
	 * FILE REQUEST
	 * New FILE requests are added to the Pacsat Broadcast (PB) queue unless it is full
	 *
	 * CONNECTION REQUEST
	 * This launches a state machine to handle the connection request and the
	 * messages.  A state machine is created for each connection request.
	 *
	 */
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

		pb_next_action();

		usleep(1000); // sleep 1000uS

	}

	/* For testing wait until the TNC thread returns.  Otherwise the program just ends.
	 * TODO - catch a signal and exit the TNC thread cleanly. */

	pthread_join(tnc_listen_pthread, NULL);
	exit(EXIT_SUCCESS);

}

/* TODO - move this to FTL0 */
void connection_received(char *from_callsign, char *to_callsign, int incomming, char * data) {
	debug_print("HANDLE CONNECTION FOR FILE UPLOAD\n");
	char loggedin[] = {0x00,0x82,0x86,0x64,0x86,0xB4,0x40,0xE0,0xA0,0x8C,0xA6,0x66,0x40,0x40,0x79,0x00,
			0xF0,0x05,0x02,0x34,0xC4,0xB9,0x5A,0x04};
	send_raw_packet('K', "PFS3-12", "AC2CZ", 0xf0, loggedin, sizeof(loggedin));

	char go[] = {0x00,0x82,0x86,0x64,0x86,0xB4,0x40,0xE0,0xA0,0x8C,0xA6,0x66,0x40,0x40,0x79,0x22,
			0xF0,0x08,0x04,0x4E,0x03,0x00,0x00,0x00,0x00,0x00,0x00};
	send_raw_packet('K', "PFS3-12", "AC2CZ", 0xf0, go, sizeof(go));

	//Disconnect
	//header.data_kind = 'd';
	//header.data_len = 0;
	//int err = send(sockfd, (char*)(&header), sizeof(header), MSG_NOSIGNAL);
	//if (err == -1) {
	//	printf ("Socket Send error with header, Terminating.\n");
	//	exit (1);
	//}
}

