/*
 * config.h
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
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#include "common_config.h"

#define VERSION __DATE__ " ARISS FS - Version 1.0a"

/* These global variables are not in the config file */
extern int g_run_self_test;    /* true when the self test is running */
extern int g_verbose;          /* print verbose output when set */
extern char g_log_filename[MAX_FILE_PATH_LEN];
/* Frames are queued by the TNC until they are transmitted.  Only hold this many before pausing
 * the broadcasts. If set too high then stations wait to receive OK confirms or for their own
 * broadcast to start */
extern int g_serial_fd; // file handle for the serial port for Rig control

/* Define paramaters for config file */
#define MAX_CONFIG_LINE_LENGTH 128
#define BIT_RATE "bit_rate"
#define BBS_CALLSIGN "bbs_callsign"
#define BROADCST_CALLSIGN "broadcast_callsign"
#define DIGI_CALLSIGN "digi_callsign"
#define MAX_FRAMES_IN_TX_BUFFER "max_frames_in_tx_buffer"
#define CONFIG_UPLOAD_TABLE_PATH "upload_table_path"

extern int g_bit_rate;		   /* the bit rate of the TNC - 1200 4800 9600 - this is only used to calculate delays.  Change actual value in DireWolf) */
extern char g_bbs_callsign[MAX_CALLSIGN_LEN];
extern char g_broadcast_callsign[MAX_CALLSIGN_LEN];
extern char g_digi_callsign[MAX_CALLSIGN_LEN];
extern int g_max_frames_in_tx_buffer;
extern char g_upload_table_path[MAX_FILE_PATH_LEN];

void load_config(char *filename);

#endif /* CONFIG_H_ */
