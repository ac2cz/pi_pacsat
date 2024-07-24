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
#include "state_file.h"
#include "iors_command.h"
#include "agw_tnc.h"
#include "str_util.h"
#include "pacsat_header.h"
#include "pacsat_dir.h"
#include "pacsat_broadcast.h"
#include "ftl0.h"
#include "iors_log.h"


/* Forward declarations */
//void process_frames_queued(char * data, int len);
void help(void);
void signal_exit (int sig);
void signal_load_config (int sig);

/*
 *  GLOBAL VARIABLES defined here.  They are declared in config.h
 *  These are the default values.  Many can be updated with a value
 *  in pacsat.config or can be overridden on the command line.
 *
 */
int g_verbose = false;
char g_log_filename[MAX_FILE_PATH_LEN];

/* These global variables are in the config file */
int g_bit_rate = 1200;
char g_bbs_callsign[10] = "PFS3-12";
char g_broadcast_callsign[10] = "PFS3-11";
char g_digi_callsign[10] = "PFS3-1";
int g_max_frames_in_tx_buffer = 2;
int g_serial_fd = -1;
char g_iors_last_command_time_path[MAX_FILE_PATH_LEN] = "pacsat_last_command_time.dat";
char g_upload_table_path[MAX_FILE_PATH_LEN] = "pacsat_upload_table.dat";

/* These global variables are in the state file and are resaved when changed.  These default values are
 * overwritten when the state file is loaded */
int g_state_pb_open = false;
int g_state_uplink_open = FTL0_STATE_SHUT;
int g_pb_status_period_in_seconds = 30;
int g_pb_max_period_for_client_in_seconds = 600; // This is 10 mins in the spec 10*60 seconds
int g_uplink_status_period_in_seconds = 30;
int g_uplink_max_period_for_client_in_seconds = 600; // This is 10 mins in the spec 10*60 seconds
int g_dir_max_file_age_in_seconds = 4320000; // 50 Days or 50 * 24 * 60 * 60 seconds
int g_dir_maintenance_period_in_seconds = 5; // check one node after this delay
int g_ftl0_maintenance_period_in_seconds = 60; // check after this delay
int g_file_queue_check_period_in_seconds = 5; // check after this delay
int g_state_pacsat_log_level = INFO_LOG;

int g_dir_next_file_number = 1; // this is updated from the state file and then when the dir is loaded
int g_ftl0_max_file_size = 153600; // 150k max file size
int g_ftl0_max_upload_age_in_seconds = 5 * 24 * 60 * 60; // 5 days

/* Local variables */
pthread_t tnc_listen_pthread;
int g_run_self_test = false;
int frame_queue_status_known = false;
char config_file_name[MAX_FILE_PATH_LEN] = "pi_pacsat.config";
char data_folder_path[MAX_FILE_PATH_LEN] = "./pacsat";
time_t last_dir_maint_time;
time_t last_ftl0_maint_time;
time_t last_file_queue_check_time;


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
	log_alog1(INFO_LOG, g_log_filename, ALOG_FS_SHUTDOWN, 0);
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
		case 'd': // data folder
			strlcpy(data_folder_path, optarg, sizeof(data_folder_path));
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
	load_state("pacsat.state");

	char log_path[MAX_FILE_PATH_LEN];
	//make_dir_path(get_folder_str(FolderLog), data_folder_path, data_folder_path, log_path);
	strlcpy(log_path, data_folder_path,MAX_FILE_PATH_LEN);
	strlcat(log_path,"/",MAX_FILE_PATH_LEN);
	strlcat(log_path,get_folder_str(FolderLog),MAX_FILE_PATH_LEN);

	log_init(get_log_name_str(LOG_NAME), log_path, g_log_filename, false); /* Pass roll logs at startup as false as only restarting iors_control can roll the logs */
	log_set_level(g_state_pacsat_log_level);
	log_alog1(INFO_LOG, g_log_filename, ALOG_FS_STARTUP, 0);

	rc = tnc_connect("127.0.0.1", AGW_PORT, g_bit_rate, g_max_frames_in_tx_buffer);
	if (rc != EXIT_SUCCESS) {
		error_print("\n Error : Could not connect to TNC on port: %d\n",IORS_PORT);
		log_err(g_log_filename, IORS_ERR_FS_TNC_FAILURE);
		log_alog1(INFO_LOG, g_log_filename, ALOG_FS_SHUTDOWN, EXIT_FAILURE);
		exit(EXIT_FAILURE);
	}

	rc = tnc_start_monitoring('k'); // k monitors raw frames, required to process UI frames
	rc = tnc_start_monitoring('m'); // monitors connected frames, also required to monitor T frames to manage the TX frame queue
	if (rc != EXIT_SUCCESS) {
		error_print("\n Error : Could not monitor TNC \n");
		log_err(g_log_filename, IORS_ERR_FS_TNC_FAILURE);
		log_alog1(INFO_LOG, g_log_filename, ALOG_FS_SHUTDOWN, EXIT_FAILURE);
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
		rc = test_pacsat_header_disk_access();
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

		rc = test_ftl0_upload_table();
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
		//TODO - split call and ssid!
		log_alog2(ERR_LOG, g_log_filename, ALOG_IORS_ERR, g_bbs_callsign, 0, IORS_ERR_FS_TNC_FAILURE);
		log_alog1(INFO_LOG, g_log_filename, ALOG_FS_SHUTDOWN, EXIT_FAILURE);
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
		log_err(g_log_filename, IORS_ERR_TNC_FAILURE);
		log_alog1(INFO_LOG, g_log_filename, ALOG_FS_SHUTDOWN, rc);
		exit(rc);
	}

	/* Initialize the directory */
	if (dir_init(data_folder_path) != EXIT_SUCCESS) { error_print("** Could not initialize the dir\n"); return EXIT_FAILURE; }
	dir_load();
	init_commanding(g_iors_last_command_time_path);
	ftl0_load_upload_table();

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
				/* The T frame is a confirm that a frame was sent.  We use this event to decrement how many
				 * frames are outstanding.  We actually increase the frame counter whenever we send a UI frame.
				 * We can still get a bit ahead of ourselves and end up with more frames queued than expected. */
				if (g_common_frames_queued)
					g_common_frames_queued--;

//				tnc_frames_queued();
				break;
			case 'y': // Response to query of number of frames outstanding -- but this does not work with DireWolf
				//process_frames_queued (frame.data, frame.header->data_len);
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
//				debug_print("CON:%d:",frame_num);
//				print_header(frame.header);
//				print_data(frame.data, frame.header->data_len);
//				debug_print("\n");

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
//				debug_print("DATA:%d:",frame_num);
//				print_header(frame.header);
//				print_data(frame.data, frame.header->data_len);
//				debug_print("\n");
				ftl0_process_data(frame.header->call_from, frame.header->call_to, frame.header->portx, frame.data, frame.header->data_len);
				break;

			case 'd': // Disconnect from the TNC
				debug_print("*** DISC from other TNC:%d:",frame_num);
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

		uint32_t now = time(0);

		if (last_dir_maint_time == 0) last_dir_maint_time = now; // Initialize at startup
		if ((now - last_dir_maint_time) > g_dir_maintenance_period_in_seconds) {
			last_dir_maint_time = now;
			dir_maintenance(now);
		}
		if (last_ftl0_maint_time == 0) last_ftl0_maint_time = now; // Initialize at startup
		if ((now - last_ftl0_maint_time) > g_ftl0_maintenance_period_in_seconds) {
			last_ftl0_maint_time = now;
			char *path = get_upload_folder();
			ftl0_maintenance(now, path);
		}
		if (last_file_queue_check_time == 0) last_file_queue_check_time = now; // Initialize at startup
		if ((now - last_file_queue_check_time) > g_file_queue_check_period_in_seconds) {
			last_file_queue_check_time = now;
			dir_file_queue_check(now, get_wod_folder(), PFH_TYPE_WL, "WOD");
			dir_file_queue_check(now, get_log_folder(), PFH_TYPE_AL, "LOG");
		}
	}


	/* Wait until the TNC thread returns.  Otherwise the program just ends.
	 * Use a signal to end the program externally. */

	pthread_join(tnc_listen_pthread, NULL);
	log_alog1(INFO_LOG, g_log_filename, ALOG_FS_SHUTDOWN, EXIT_SUCCESS);
	exit(EXIT_SUCCESS);
}

//void process_frames_queued(unsigned char * data, int len) {
//	uint32_t *num = (uint32_t *)data;
//	g_common_frames_queued = *num;
//	frame_queue_status_known = true;
//	//debug_print("***** Received y: %d\n", g_common_frames_queued);
//}



