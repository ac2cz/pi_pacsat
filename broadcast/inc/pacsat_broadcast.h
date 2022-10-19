/*
 * pacsat_broadcast.h
 *
 *  Created on: Oct 13, 2022
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


#ifndef PACSAT_BROADCAST_H_
#define PACSAT_BROADCAST_H_

#include <stdint.h>

#define PID_FILE		0xBB
#define PID_DIRECTORY	0xBD
#define PID_NO_PROTOCOL	0xF0

#define PB_STATUS_PERIOD_IN_SECONDS 60
#define MAX_PB_LENGTH 10 /* This is the maximum number of stations that can be on the PB at one time */
#define PB_DIR_REQUEST_TYPE 1
#define PB_FILE_REQUEST_TYPE 2

#define MAX_REQUEST_PACKETS 10 /* The maximum number of Dir Headers or File segments that will be sent in response to a request */
#define MAX_BROADCAST_LENGTH 256 /* This was the limit on historical Pacsats. Can we make it longer? */

#define PBLIST "PBLIST" // destination for PB Status when open
#define PBFULL "PBFULL" // destination for PB status when list is full
#define PBSHUT "PBSHUT" // destination for PB status when it is closed
#define QST "QST-1" // destination for broadcast dir and file frames

#define E_BIT 5
#define N_BIT 6

struct FILE_HEADER {
     unsigned char flags;
     uint32_t file_id;
     unsigned char file_type;
     unsigned int offset : 16;
     unsigned char offset_msb;
} __attribute__ ((__packed__));

#define PB_DIR_HEADER_SIZE 17
struct t_dir_header { // sent by Pacsat
	unsigned char flags;
	uint32_t file_id;
	uint32_t offset;
	uint32_t t_old;
	uint32_t t_new;
} __attribute__ ((__packed__));
typedef struct t_dir_header DIR_HEADER;

#define DIR_REQUEST_HEADER_SIZE 3
struct t_dir_req_header { // sent by client
	unsigned char flags;
	unsigned int block_size : 16;
};
typedef struct t_dir_req_header DIR_REQ_HEADER;

struct t_pair {
	uint32_t start;
	uint32_t end;
} __attribute__ ((__packed__));
typedef struct t_pair DATE_PAIR;

#define BROADCAST_REQUEST_HEADER_SIZE 17
struct t_broadcast_request_header {
	unsigned char flag;
	unsigned char to_callsign[7];
	unsigned char from_callsign[7];
	unsigned char control_byte;
	unsigned char pid;
} __attribute__ ((__packed__));

int pb_next_action();
void process_monitored_frame(char *from_callsign, char *to_callsign, char *data, int len);
int test_pb();
int test_pb_list();

#endif /* PACSAT_BROADCAST_H_ */
