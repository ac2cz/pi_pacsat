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

// TODO - this should be in the CONFIG file
#define PB_STATUS_PERIOD_IN_SECONDS 30
#define PB_MAX_PERIOD_FOR_CLIENT_IN_SECONDS 36000 // This is 10 mins in the spec 10*60*60 seconds
#define MAX_PB_LENGTH 10 /* This is the maximum number of stations that can be on the PB at one time */
#define PB_DIR_REQUEST_TYPE 1
#define PB_FILE_REQUEST_TYPE 2

/* Error numbers sent in response to Broadcast requests */
#define PB_ERR_TEMPORARY 1
#define PB_ERR_FILE_NOT_AVAILABLE 2
#define PB_ERR_FILE_NOT_DOWNLOADABLE 3
#define PB_ERR_FILE_INVALID_PACKET 5

#define EXIT_LAST_CHUNK_SENT 2 /* This exit code is used when we have sent the last chunk of a file */

#define MAX_REQUEST_PACKETS 10 /* The maximum number of Dir Headers or File segments that will be sent in response to a request */
#define MAX_BROADCAST_LENGTH 256 /* This was the limit on historical Pacsats. Can we make it longer? */

#define PBLIST "PBLIST" // destination for PB Status when open
#define PBFULL "PBFULL" // destination for PB status when list is full
#define PBSHUT "PBSHUT" // destination for PB status when it is closed
#define QST "QST-1" // destination for broadcast dir and file frames

#define L_BIT 0
#define E_BIT 5
#define N_BIT 6

#define PB_START_SENDING_FILE 0b00
#define PB_STOP_SENDING_FILE 0b01
#define PB_FILE_HOLE_LIST 0b10

#define PB_FILE_DEFAULT_BLOCK_SIZE 0xF4

/**
 *  The Server Sends frames with these headers
 */

struct t_file_header {
     unsigned char flags;
     uint32_t file_id;
     unsigned char file_type;
     unsigned int offset : 24;
} __attribute__ ((__packed__));
typedef struct t_file_header PB_FILE_HEADER;

struct t_dir_header { // sent by Pacsat
	unsigned char flags;
	uint32_t file_id;
	uint32_t offset;
	uint32_t t_old;
	uint32_t t_new;
} __attribute__ ((__packed__));
typedef struct t_dir_header PB_DIR_HEADER;


/**
 * The client sends frames with these headers, which we need to parse
 */

struct t_file_req_header {
	char flags;
	uint32_t file_id;
	uint16_t block_size;
} __attribute__ ((__packed__));
typedef struct t_file_req_header FILE_REQ_HEADER;

struct t_file_pair {
     unsigned int offset : 24;
     uint16_t length;
     } __attribute__ ((__packed__));
typedef struct t_file_pair FILE_DATE_PAIR;

struct t_dir_req_header { // sent by client
	unsigned char flags;
	uint16_t block_size;
} __attribute__ ((__packed__));
typedef struct t_dir_req_header DIR_REQ_HEADER;

struct t_dir_pair {
	uint32_t start;
	uint32_t end;
} __attribute__ ((__packed__));
typedef struct t_dir_pair DIR_DATE_PAIR;

struct t_broadcast_request_header {
	unsigned char flag;
	unsigned char to_callsign[7];
	unsigned char from_callsign[7];
	unsigned char control_byte;
	unsigned char pid;
} __attribute__ ((__packed__));
typedef struct t_broadcast_request_header AX25_HEADER;

int pb_next_action();
void pb_process_frame(char *from_callsign, char *to_callsign, char *data, int len);
int test_pb();
int test_pb_list();
int test_pb_file();
int test_pb_file_holes();

#endif /* PACSAT_BROADCAST_H_ */
