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
#define MAX_IN_PROCESS_FILE_UPLOADS 10
#define TIMER_T3_PERIOD_IN_SECONDS 30 // this is 1/10th the Direwolf timeout of 300s
#define BBSTAT "BBSTAT"
#define BBCOM "BBCOM"

#define FTL0_STATE_SHUT 0
#define FTL0_STATE_OPEN 1
#define FTL0_STATE_COMMAND 2

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
	UL_NAK_RESP,			/* 7 */
	/* Note that there are commands defined from 8 - 17 for legacy dir and download commands */
	AUTH_UPLOAD_CMD = 20,       /* Authenticated upload cmd */
	AUTH_DATA_END = 21
}
PACKET_TYPE;

#define MAX_PACKET_ID 21

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

typedef struct {
	uint32_t dateTime;
	uint32_t continue_file_no;
	uint32_t file_length;
	uint8_t AuthenticationVector[32];
} FTL0_AUTH_UPLOAD_CMD;

typedef struct {
	uint32_t dateTime;
	uint16_t header_check;
	uint16_t body_check;
	uint8_t AuthenticationVector[32];
} FTL0_AUTH_DATA_END;

/* This stores the details of an in process file upload */
typedef struct InProcessFileUpload {
    char callsign[MAX_CALLSIGN_LEN]; /* The callsign of the stations that initiated the upload */
    uint32_t file_id; /* The file id that was allocated by the dir.  A standard function calculates the tmp file name on disk */
    uint32_t length;  /* The promised length of the file given by the station when it requested the upload */
    uint32_t offset;  /* The offset at the end of the latest block uploaded */
    uint32_t request_time; /* The date/time that this upload was requested */
} InProcessFileUpload_t;

int ftl0_connection_received(char *from_callsign, char *to_callsign, int channel, int incomming, unsigned char * data);
int ftl0_process_data(char *from_callsign, char *to_callsign, int channel, unsigned char *data, int len);
int ftl0_disconnected(char *from_callsign, char *to_callsign, unsigned char *data, int len);
int ftl0_next_action();

int ftl0_get_file_upload_record(uint32_t file_id, InProcessFileUpload_t * file_upload_record);
int ftl0_set_file_upload_record(InProcessFileUpload_t * file_upload_record);
int ftl0_get_space_reserved_by_upload_table();
int ftl0_update_file_upload_record(InProcessFileUpload_t * file_upload_record);
int ftl0_load_upload_table();
int ftl0_save_upload_table();
void ftl0_maintenance(time_t now, char *upload_folder);

int test_ftl0_upload_table();
int test_ftl0_frame();
int test_ftl0_list();
int test_ftl0_action();

#endif /* FTL0_H_ */
