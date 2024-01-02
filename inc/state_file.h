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

extern int g_state_pb_open;
extern int g_state_uplink_open;

void load_state(char *filename);
void save_state();


#endif /* STATE_FILE_H_ */
