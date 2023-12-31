/*
 * config.c
 *
 *  Created on: May 17, 2022
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
 *  Load user configuration variables from a file.  This should hold all of the values
 *  that might change from one environment to the next.  e.g. different gains may
 *  be needed for different sound cards.
 *
 */

#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "str_util.h"

void load_config(char *filename) {
	char *key;
	char *value;
	char *search = "=";
	debug_print("Loading config from: %s:\n", filename);
	FILE *file = fopen ( filename, "r" );
	if ( file != NULL ) {
		char line [ MAX_CONFIG_LINE_LENGTH ]; /* or other suitable maximum line size */
		while ( fgets ( line, sizeof line, file ) != NULL ) /* read a line */ {

			/* Token will point to the part before the =
			 * Using strtok safe here because we do not have multiple delimiters and
			 * no other threads started at this time. */
			key = strtok(line, search);

			// Token will point to the part after the =.
			value = strtok(NULL, search);
			if (value != NULL) { /* Ignore line with no key value pair */;

				debug_print(" %s",key);
				value[strcspn(value,"\n")] = 0; // Move the nul termination to get rid of the new line
				debug_print(" = %s\n",value);
				if (strcmp(key, BIT_RATE) == 0) {
					int rate = atoi(value);
					g_bit_rate = rate;
				} else if (strcmp(key, BBS_CALLSIGN) == 0) {
					strlcpy(g_bbs_callsign, value,sizeof(g_bbs_callsign));
				} else if (strcmp(key, BROADCST_CALLSIGN) == 0) {
					strlcpy(g_broadcast_callsign, value,sizeof(g_broadcast_callsign));
				} else if (strcmp(key, DIGI_CALLSIGN) == 0) {
					strlcpy(g_digi_callsign, value,sizeof(g_digi_callsign));
				} else if (strcmp(key, MAX_FRAMES_IN_TX_BUFFER) == 0) {
					int n = atoi(value);
					g_max_frames_in_tx_buffer = n;
				} else if (strcmp(key, PB_STATUS_PERIOD_IN_SECONDS) == 0) {
					int n = atoi(value);
					g_pb_status_period_in_seconds = n;
				} else if (strcmp(key, PB_MAX_PERIOD_FOR_CLIENT_IN_SECONDS) == 0) {
					int n = atoi(value);
					g_pb_max_period_for_client_in_seconds = n;
				} else if (strcmp(key, UPLINK_STATUS_PERIOD_IN_SECONDS) == 0) {
					int n = atoi(value);
					g_uplink_status_period_in_seconds = n;
				} else if (strcmp(key, UPLINK_MAX_PERIOD_FOR_CLIENT_IN_SECONDS) == 0) {
					int n = atoi(value);
					g_uplink_max_period_for_client_in_seconds = n;

				} else {
					error_print("Unknown key in %s file: %s\n",filename, key);
				}
			}
		}
		fclose ( file );
	} else {
		error_print("FATAL..  Could not load config file: %s\n", filename);
		exit(1);
	}
}

