/*
 * state_file.c
 *
 *  Created on: Jan 1, 2024
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
 *  Load user state variables from a file.
 *
 */

#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "state_file.h"
#include "str_util.h"

/* Forward functions */
void save_int_key_value(char * key, int val, FILE *file);

static char filename[MAX_FILE_PATH_LEN];

void load_state(char *filepath) {
	strlcpy(filename, filepath, sizeof(filename));
	char *key;
	char *value;
	char *search = "=";
	debug_print("Loading state from: %s:\n", filename);
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
				if (strcmp(key, STATE_PB_OPEN) == 0) {
					g_state_pb_open = atoi(value);
				} else if (strcmp(key, STATE_UPLINK_OPEN) == 0) {
					g_state_uplink_open = atoi(value);
				} else if (strcmp(key, PB_STATUS_PERIOD_IN_SECONDS) == 0) {
					g_pb_status_period_in_seconds = atoi(value);
				} else if (strcmp(key, PB_MAX_PERIOD_FOR_CLIENT_IN_SECONDS) == 0) {
					g_pb_max_period_for_client_in_seconds = atoi(value);
				} else if (strcmp(key, UPLINK_STATUS_PERIOD_IN_SECONDS) == 0) {
					g_uplink_status_period_in_seconds = atoi(value);
				} else if (strcmp(key, UPLINK_MAX_PERIOD_FOR_CLIENT_IN_SECONDS) == 0) {
					g_uplink_max_period_for_client_in_seconds = atoi(value);
				} else if (strcmp(key, DIR_MAX_FILE_AGE_IN_SECONDS) == 0) {
					g_dir_max_file_age_in_seconds = atoi(value);
				} else if (strcmp(key, DIR_MAINTENANCE_IN_SECONDS) == 0) {
					g_dir_maintenance_period_in_seconds = atoi(value);
				} else if (strcmp(key, FTL0_MAX_FILE_SIZE) == 0) {
					g_ftl0_max_file_size = atoi(value);
				} else {
					error_print("Unknown key in state file: %s : %s\n",filename, key);
				}
			}
		}
		fclose ( file );
	} else {
		debug_print("Could not load state file: %s\n", filename);
	}
}

void save_state() {
	debug_print("Saving state to: %s:\n", filename);
	FILE *file = fopen ( filename, "w" );
	if ( file != NULL ) {
		save_int_key_value(STATE_PB_OPEN, g_state_pb_open, file);
		save_int_key_value(STATE_UPLINK_OPEN, g_state_uplink_open, file);
		save_int_key_value(PB_STATUS_PERIOD_IN_SECONDS, g_pb_status_period_in_seconds, file);
		save_int_key_value(PB_MAX_PERIOD_FOR_CLIENT_IN_SECONDS, g_pb_max_period_for_client_in_seconds, file);
		save_int_key_value(UPLINK_STATUS_PERIOD_IN_SECONDS, g_uplink_status_period_in_seconds, file);
		save_int_key_value(UPLINK_MAX_PERIOD_FOR_CLIENT_IN_SECONDS, g_uplink_max_period_for_client_in_seconds, file);
		save_int_key_value(DIR_MAX_FILE_AGE_IN_SECONDS, g_dir_max_file_age_in_seconds, file);
		save_int_key_value(DIR_MAINTENANCE_IN_SECONDS, g_dir_maintenance_period_in_seconds, file);
		save_int_key_value(FTL0_MAX_FILE_SIZE, g_ftl0_max_file_size, file);
	}
	fclose(file);
}

void save_int_key_value(char * key, int val, FILE *file) {
	char buf[MAX_CONFIG_LINE_LENGTH];
	strlcpy(buf, key, sizeof(buf));
	strlcat(buf, "=", sizeof(buf));
	char int_str[25];
	snprintf(int_str, 25, "%d\n",val);
	strlcat(buf, int_str, sizeof(buf));
	fputs(buf, file);
}
