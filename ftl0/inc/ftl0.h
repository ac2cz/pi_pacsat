/*
 * ftl0.h
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

#ifndef FTL0_H_
#define FTL0_H_

#define MAX_UPLINK_LIST_LENGTH 4
#define BBSTAT "BBSTAT"
#define BBCOM "BBCOM"

#define FTL0_PFH_BIT 2
#define FTL0_VERSION_BIT1 0
#define FTL0_VERSION_BIT2 1

// TODO - space required should be in the config file
#define UPLOAD_SPACE_THRESHOLD 100000000 // Space that must be available in bytes

typedef enum
{
	UL_UNINIT,		/* 0 */
	UL_CMD_WAIT,
	UL_CMD_OK,
	UL_DATA_RX,
	UL_ABORT
}
UPLINK_STATE;

typedef enum
{
	DATA,				/* 0 */
	DATA_END,
	LOGIN_RESP,
	UPLOAD_CMD,
	UL_GO_RESP,
	UL_ERROR_RESP,			/* 5 */
	UL_ACK_RESP,
	UL_NAK_RESP			/* 7 */
}
PACKET_TYPE;

#define MAX_PACKET_ID 7

typedef enum
{
	ER_NONE,			/* 0 */
	ER_ILL_FORMED_CMD,
	ER_BAD_CONTINUE,
	ER_SERVER_FSYS,
	ER_NO_SUCH_FILE_NUMBER,
	ER_SELECTION_EMPTY_1,		/* 5 */
	ER_MANDATORY_FIELD_MISSING,
	ER_NO_PFH,
	ER_POORLY_FORMED_SEL,
	ER_ALREADY_LOCKED,
	ER_NO_SUCH_DESTINATION,		/* 10 */
	ER_SELECTION_EMPTY_2,
	ER_FILE_COMPLETE,
	ER_NO_ROOM,
	ER_BAD_HEADER,
	ER_HEADER_CHECK,		/* 15 */
	ER_BODY_CHECK			/* 16 */
}
ERROR_CODES;

/* The server sends these packets */
typedef struct {
	uint32_t login_time;
	unsigned char login_flags;
} FTL0_LOGIN_DATA;

typedef struct {
	uint32_t server_file_no;
	uint32_t byte_offset;
} FTL0_UL_GO_DATA;

/* The client sends these packets */
typedef struct {
	uint32_t continue_file_no;
	uint32_t file_length;
} FTL0_UPLOAD_CMD;

int ftl0_connection_received(char *from_callsign, char *to_callsign, int channel, int incomming, unsigned char * data);
int ftl0_process_data(char *from_callsign, char *to_callsign, int channel, unsigned char *data, int len);
int ftl0_disconnected(char *from_callsign, char *to_callsign, unsigned char *data, int len);
int ftl0_next_action();
void ftl0_make_tmp_filename(int file_id, char *dir_name, char *filename, int max_len);

int test_ftl0_frame();
int test_ftl0_list();
int test_ftl0_action();

#endif /* FTL0_H_ */
