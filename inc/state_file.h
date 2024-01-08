/*
 * state_file.h
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
 */

#ifndef STATE_FILE_H_
#define STATE_FILE_H_

#include "common_config.h"

/* Define paramaters for state file */
#define STATE_PB_OPEN "pb_open"
#define STATE_UPLINK_OPEN "uplink_open"
#define PB_STATUS_PERIOD_IN_SECONDS "pb_status_period_in_seconds"
#define PB_MAX_PERIOD_FOR_CLIENT_IN_SECONDS "pb_max_period_for_client_in_seconds"
#define UPLINK_STATUS_PERIOD_IN_SECONDS "uplink_status_period_in_seconds"
#define UPLINK_MAX_PERIOD_FOR_CLIENT_IN_SECONDS "uplink_max_period_for_client_in_seconds"
#define DIR_MAX_FILE_AGE_IN_SECONDS "dir_max_file_age_in_seconds"
#define DIR_MAINTENANCE_IN_SECONDS "dir_maintenance_period_in_seconds"
#define FTL0_MAX_FILE_SIZE "ftl0_max_file_size"

extern int g_state_pb_open;
extern int g_state_uplink_open;
extern int g_pb_status_period_in_seconds;
extern int g_pb_max_period_for_client_in_seconds;
extern int g_uplink_status_period_in_seconds;
extern int g_uplink_max_period_for_client_in_seconds;
extern int g_dir_max_file_age_in_seconds;
extern int g_dir_maintenance_period_in_seconds;
extern int g_ftl0_max_file_size;

void load_state(char *filename);
void save_state();


#endif /* STATE_FILE_H_ */
