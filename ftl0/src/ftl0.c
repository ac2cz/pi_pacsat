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

/* Program Include files */
#include "config.h"
#include "agw_tnc.h"
#include "str_util.h"
#include "pacsat_header.h"
#include "pacsat_dir.h"
#include "ftl0.h"

void ftl0_connection_received(char *from_callsign, char *to_callsign, int incomming, char * data) {
	debug_print("HANDLE CONNECTION FOR FILE UPLOAD\n");
	unsigned char loggedin[] = {0x00,0x82,0x86,0x64,0x86,0xB4,0x40,0xE0,0xA0,0x8C,0xA6,0x66,0x40,0x40,0x79,0x00,
			0xF0,0x05,0x02,0x34,0xC4,0xB9,0x5A,0x04};
	send_raw_packet("PFS3-12", "AC2CZ", 0xf0, loggedin, sizeof(loggedin));

	unsigned char go[] = {0x00,0x82,0x86,0x64,0x86,0xB4,0x40,0xE0,0xA0,0x8C,0xA6,0x66,0x40,0x40,0x79,0x22,
			0xF0,0x08,0x04,0x4E,0x03,0x00,0x00,0x00,0x00,0x00,0x00};
	send_raw_packet("PFS3-12", "AC2CZ", 0xf0, go, sizeof(go));

	//Disconnect
	//header.data_kind = 'd';
	//header.data_len = 0;
	//int err = send(sockfd, (char*)(&header), sizeof(header), MSG_NOSIGNAL);
	//if (err == -1) {
	//	printf ("Socket Send error with header, Terminating.\n");
	//	exit (1);
	//}
}

void ftl0_process_frame(char *from_callsign, char *to_callsign, char *data, int len) {
	if (strncasecmp(to_callsign, g_broadcast_callsign, 7) == 0) {
		// this was sent to the Broadcast Callsign
		debug_print("Broadcast Request - Ignored\n");
	}
	if (strncasecmp(to_callsign, g_bbs_callsign, 7) == 0) {
		// this was sent to the Broadcast Callsign

//		struct t_broadcast_request_header *broadcast_request_header;
//		broadcast_request_header = (struct t_broadcast_request_header *)data;
//		debug_print("FTL0 Data: pid: %02x \n", broadcast_request_header->pid & 0xff);
//		if ((broadcast_request_header->pid & 0xff) == PID_DIRECTORY) {
//			pb_handle_dir_request(from_callsign, data, len);
//		}
//		if ((broadcast_request_header->pid & 0xff) == PID_FILE) {
//			// File Request
//			pb_handle_file_request(from_callsign, data, len);
//		}
	}
}
