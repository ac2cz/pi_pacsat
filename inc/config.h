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

#include "debug.h"

#define VERSION __DATE__ " - Version 0.4"
#define DEBUG 1
#define true 1
#define false 0
#define AX25_MAX_DATA_LEN 2048
#define AGW_PORT 8000

extern int g_verbose;          /* set from command line switch or from the cmd console */

extern char g_bbs_callsign[10];
extern char g_broadcast_callsign[10];
extern char g_digi_callsign[10];

#endif /* CONFIG_H_ */
