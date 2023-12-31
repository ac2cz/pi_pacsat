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
#include <signal.h>

/* Program Include files */
#include "config.h"
#include "common_config.h"
#include "agw_tnc.h"
#include "str_util.h"
#include "pacsat_header.h"
#include "pacsat_dir.h"
#include "pacsat_broadcast.h"
#include "ftl0.h"


/* Forward declarations */
//void process_frames_queued(char * data, int len);
void help(void);
void signal_exit (int sig);
void signal_load_config (int sig);
void process_frames_queued(unsigned char * data, int len);

/*
 *  GLOBAL VARIABLES defined here.  They are declared in config.h
 *  These are the default values.  Many can be updated with a value
 *  in pacsat.config or can be overridden on the command line.
 *
 */
int g_verbose = false;

/* These global variables are in the config file */
int g_bit_rate = 1200;
char g_bbs_callsign[10] = "PFS3-12";
char g_broadcast_callsign[10] = "PFS3-11";
char g_digi_callsign[10] = "PFS3-1";
int g_max_frames_in_tx_buffer = 2;
int g_pb_status_period_in_seconds = 30;
int g_pb_max_period_for_client_in_seconds = 36000; // This is 10 mins in the spec 10*60*60 seconds
int g_uplink_status_period_in_seconds = 30;
int g_uplink_max_period_for_client_in_seconds = 36000; // This is 10 mins in the spec 10*60*60 seconds
int g_serial_fd = -1;

/* Local variables */
pthread_t tnc_listen_pthread;
int g_run_self_test = false;
int frame_queue_status_known = false;
char config_file_name[MAX_FILE_PATH_LEN] = "pi_pacsat.config";
char dir_path[MAX_FILE_PATH_LEN] = "./pacsat";

/**
 * Print this help if the -h or --help command line options are used
 */
void help(void) {
	printf(
			"Usage: pacsat [OPTION]... \n"
			"-h,--help                        help\n"
			"-c,--config                      use config file specified\n"
			"-d,--dir                         use this data directory, rather than default\n"
			"-t,--test                        Run self test functions and exit\n"
			"-v,--verbose                     print additional status and progress messages\n"
	);
	exit(EXIT_SUCCESS);
}

void signal_exit (int sig) {
	debug_print (" Signal received, exiting ...\n");
	// TODO - unregister the callsign and close connection to AGW

	exit (0);
}

void signal_load_config (int sig) {
	error_print (" Signal received, updating config not yet implemented...\n");
	// TODO SIHUP should reload the config perhaps
}

int main(int argc, char *argv[]) {
	// TODO - use POSIX sigaction rather than signal because it is more reliable
	signal (SIGQUIT, signal_exit);
	signal (SIGTERM, signal_exit);
	signal (SIGHUP, signal_load_config);
	signal (SIGINT, signal_exit);

	struct option long_option[] = {
			{"help", no_argument, NULL, 'h'},
			{"dir", required_argument, NULL, 'd'},
			{"config", required_argument, NULL, 'c'},
			{"test", no_argument, NULL, 't'},
			{"verbose", no_argument, NULL, 'v'},
			{NULL, 0, NULL, 0},
	};

	int more_help = false;
	strlcpy(config_file_name, "pacsat.config", sizeof(config_file_name));

	while (1) {
		int c;
		if ((c = getopt_long(argc, argv, "htvc:d:", long_option, NULL)) < 0)
			break;
		switch (c) {
		case 'h': // help
			more_help = true;
			break;
		case 't': // self test
			g_run_self_test = true;
			break;
		case 'v': // verbose
			g_verbose = true;
			break;
		case 'c': // config file name
			strlcpy(config_file_name, optarg, sizeof(config_file_name));
			break;
		case 'd': // directory
			strlcpy(dir_path, optarg, sizeof(dir_path));
			break;
		}
	}

	if (more_help) {
		help();
		return 0;
	}

	printf("PI-ARISS In-orbit File Server\n");
	printf("Build: %s\n", VERSION);

	int rc = EXIT_SUCCESS;

	/* Load configuration from the config file */
	load_config(config_file_name);

	rc = tnc_connect("127.0.0.1", AGW_PORT, g_bit_rate, g_max_frames_in_tx_buffer);
	if (rc != EXIT_SUCCESS) {
		error_print("\n Error : Could not connect to TNC on port: %d\n",IORS_PORT);
		exit(EXIT_FAILURE);
	}

	rc = tnc_start_monitoring('k'); // k monitors raw frames, required to process UI frames
	rc = tnc_start_monitoring('m'); // monitors connected frames, also required to monitor T frames to manage the TX frame queue
	if (rc != EXIT_SUCCESS) {
		error_print("\n Error : Could not monitor TNC \n");
		exit(EXIT_FAILURE);
	}

	if (g_run_self_test) {
		debug_print("Running Self Tests..\n");
		rc = test_ftl0_frame();
		if (rc != EXIT_SUCCESS) exit(rc);
		rc = test_ftl0_list();
		if (rc != EXIT_SUCCESS) exit(rc);

		rc = test_ftl0_action();
		if (rc != EXIT_SUCCESS) exit(rc);

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
	char *name = "TNC Listen Thread";
	rc = pthread_create( &tnc_listen_pthread, NULL, tnc_listen_process, (void*) name);
	if (rc != EXIT_SUCCESS) {
		error_print("FATAL. Could not start the TNC listen thread.\n");
		exit(rc);
	}

	/* Initialize the directory */
	if (dir_init(dir_path) != EXIT_SUCCESS) { error_print("** Could not initialize the dir\n"); return EXIT_FAILURE; }
	dir_load();

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
				debug_print("Set BBS Callsign: %s:\n",frame.header->call_from);
				break;
			case 'T': // Response to sending a UI frame
				/* The T frame is a confirm that a frame was sent.  We use this event to query the TNC and ask how many
				 * frames are outstanding.  Note that this process has a delay, so we actually increase the frame counter
				 * whenever we send a UI frame. But the received y frame below will then set it to the right value.  We
				 * can still get a bit ahead of ourselves and end up with more frames queued than expected. */
				tnc_frames_queued();
				break;
			case 'y': // Response to query of number of frames outstanding -- but this does not work with DireWolf
				process_frames_queued (frame.data, frame.header->data_len);
				break;
			case 'S': // Supervisory frame.  Only received if monitoring with 'm'.  Only needed for debugging
				break;
			case 'K': // Monitored frame
//				debug_print("FRM:%d:",frame_num);
//				print_header(frame.header);
//				print_data(frame.data, frame.header->data_len);
//				debug_print("| %d bytes\n", frame.header->data_len);

				/* Only send Broadcast UI frames to the PB */
				if (strncasecmp(frame.header->call_to, g_broadcast_callsign, MAX_CALLSIGN_LEN) == 0)
					pb_process_frame (frame.header->call_from, frame.header->call_to, frame.data, frame.header->data_len);
				break;
			case 'C': // Connected to a station
				debug_print("CON:%d:",frame_num);
				print_header(frame.header);
				print_data(frame.data, frame.header->data_len);
				debug_print("\n");

				if (strncmp((char *)frame.data, "*** CONNECTED To Station", 24) == 0) {
					// Incoming: Other station initiated the connect request.
					ftl0_connection_received (frame.header->call_from, frame.header->call_to, frame.header->portx, 1, frame.data);
				}
				else if (strncmp((char *)frame.data, "*** CONNECTED With Station", 26) == 0) {
					// Outgoing: Other station accepted my connect request.
					ftl0_connection_received (frame.header->call_from, frame.header->call_to, frame.header->portx, 0, frame.data);
				}
				break;

			case 'D': // Data from a connected station
				// TODO - we might want to block signals here so we don't exit in the middle of a file write
				debug_print("DATA:%d:",frame_num);
				print_header(frame.header);
				print_data(frame.data, frame.header->data_len);
				debug_print("\n");
				ftl0_process_data(frame.header->call_from, frame.header->call_to, frame.header->portx, frame.data, frame.header->data_len);
				break;

			case 'd': // Disconnect from the TNC
				debug_print("*** DISC:%d:",frame_num);
				print_header(frame.header);
				print_data(frame.data, frame.header->data_len);
				debug_print("\n");
				ftl0_disconnected(frame.header->call_from, frame.header->call_to, frame.data, frame.header->data_len);
				break;
			}
		} else {
			usleep(10000); // sleep 10ms
		}

		pb_next_action();
		ftl0_next_action();

	}



	/* Wait until the TNC thread returns.  Otherwise the program just ends.
	 * Use a signal to end the program externally. */

	pthread_join(tnc_listen_pthread, NULL);
	exit(EXIT_SUCCESS);
}

void process_frames_queued(unsigned char * data, int len) {
	uint32_t *num = (uint32_t *)data;
	g_common_frames_queued = *num;
	frame_queue_status_known = true;
//	debug_print("***** Received y: %d\n", g_frames_queued);
}



