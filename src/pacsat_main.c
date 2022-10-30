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
#include "ftl0.h"

/* Forward declarations */
void process_frames_queued(char * data, int len);
void connection_received(char *from_callsign, char *to_callsign, int incomming, char * data);

/*
 *  GLOBAL VARIABLES defined here.  They are declared in config.h
 *  These are the default values.  Many can be updated with a value
 *  in pacsat.config or can be overridden on the command line.
 *
 */
int g_verbose = false;
int g_bit_rate = 1200;
char g_bbs_callsign[10] = "PFS3-12";
char g_broadcast_callsign[10] = "PFS3-11";
int g_frames_queued = 0;

/* Local variables */
pthread_t tnc_listen_pthread;
int g_run_self_test = false;
int frame_queue_status_known = false;

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
		rc = test_pfh_checksum();
		if (rc != EXIT_SUCCESS) exit(rc);
		rc = test_pacsat_header();
		if (rc != EXIT_SUCCESS) exit(rc);
		rc = test_pacsat_dir_one();
		if (rc != EXIT_SUCCESS) exit(rc);

		rc = test_pacsat_dir();
		if (rc != EXIT_SUCCESS) exit(rc);
		rc = test_pb_list();
		if (rc != EXIT_SUCCESS) exit(rc);
		rc = test_pb();
		if (rc != EXIT_SUCCESS) exit(rc);
		rc = test_pb_file();
		if (rc != EXIT_SUCCESS) exit(rc);
		rc = test_pb_file_holes();
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
	if (dir_init("./dir") != EXIT_SUCCESS) { error_print("** Could not initialize the dir\n"); return EXIT_FAILURE; }
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
	 * This launches an uplink state machine to handle the connection request and the
	 * messages.  An uplink state machine is created for each connection request.
	 *
	 * CONNECTED DATA
	 * Data that is passed to the Uplink State Machine.
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
			case 'y': // Response to query of number of frames outstanding
				process_frames_queued (frame.data, frame.header->data_len);
				break;
			case 'K': // Monitored frame
				debug_print("FRM:%d:",frame_num);
				print_header(frame.header);
				print_data(frame.data, frame.header->data_len);
				debug_print("| %d bytes\n", frame.header->data_len);

				pb_process_frame (frame.header->call_from, frame.header->call_to, frame.data, frame.header->data_len);
				break;
			case 'C': // Connected to a station
				debug_print("CON:%d:",frame_num);
				print_header(frame.header);
				print_data(frame.data, frame.header->data_len);
				debug_print("\n");

				if (strncmp(frame.data, "*** CONNECTED To Station", 24) == 0) {
					// Incoming: Other station initiated the connect request.
					ftl0_connection_received (frame.header->call_from, frame.header->call_to, 1, frame.data);
				}
				else if (strncmp(frame.data, "*** CONNECTED With Station", 26) == 0) {
					// Outgoing: Other station accepted my connect request.
					ftl0_connection_received (frame.header->call_from, frame.header->call_to, 0, frame.data);
				}/* If we removed a station then we don't want/need to increment the current station pointer */
				return EXIT_SUCCESS;
				break;

			case 'D': // Data from a connected station
				debug_print("DATA:%d:",frame_num);
				print_header(frame.header);
				print_data(frame.data, frame.header->data_len);
				debug_print("\n");
				ftl0_process_frame (frame.header->call_from, frame.header->call_to, frame.data, frame.header->data_len);
				break;
			}
		}

		/* Don't take the next action until we know the state of the TNC frame queue but NOTE that
		 * this does not seem to work because DireWolf takes a long time to return the status.  In
		 * that time we can add 100s of frames to the queue.  Instead we currently delay the sending
		 * of frames in agw_tnc.c*/

		if (frame_queue_status_known == true) {
			pb_next_action();
			frame_queue_status_known = false;
		}
		tnc_frames_queued();

		usleep(1000); // sleep 1ms

	}

	/* For testing wait until the TNC thread returns.  Otherwise the program just ends.
	 * TODO - catch a signal and exit the TNC thread cleanly. */

	pthread_join(tnc_listen_pthread, NULL);
	exit(EXIT_SUCCESS);

}

void process_frames_queued(char * data, int len) {
	uint32_t *num = (uint32_t *)data;
	g_frames_queued = *num;
	frame_queue_status_known = true;
	//debug_print("Received y: %d\n", g_frames_queued);
}



