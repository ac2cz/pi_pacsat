/*
 * pacsat_header.h
 *
 *  Created on: Oct 7, 2022
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

#ifndef PACSAT_HEADER_H_
#define PACSAT_HEADER_H_

#include <stdint.h>

// Mandatory Header
#define  FILE_ID 0x01
#define  FILE_NAME 0x02
#define  FILE_EXT 0x03
#define  FILE_SIZE 0x04
#define  CREATE_TIME 0x05
#define  LAST_MOD_TIME 0x06
#define  SEU_FLAG 0x07
#define  FILE_TYPE 0x08
#define  BODY_CHECKSUM 0x09
#define  HEADER_CHECKSUM 0x0a
#define  BODY_OFFSET 0x0b

// Extended Header
#define  SOURCE 0x10
#define  AX25_UPLOADER 0x11
#define  UPLOAD_TIME 0x12 /* Note that this is a Mandatory item on all files */
#define  DOWNLOAD_COUNT 0x13
#define  DESTINATION 0x14
#define  AX25_DOWNLOADER 0x15
#define  DOWNLOAD_TIME 0x16
#define  EXPIRE_TIME 0x17
#define  PRIORITY 0x18

// Optional Header
#define  COMPRESSION_TYPE 0x19
#define  BBS_MSG_TYPE 0x20
#define  BULLETIN_ID_NUMBER 0x21
#define  TITLE 0x22
#define  KEYWORDS 0x23
#define  FILE_DESCRIPTION 0x24
#define  COMPRESSION_DESCRIPTION 0x25
#define  USER_FILE_NAME 0x26

// Compression types
#define  BODY_NOT_COMPRESSED 0x00
#define  BODY_COMPRESSED_PKARC 0x01
#define  BODY_COMPRESSED_PKZIP 0x02
#define  BODY_COMPRESSED_GZIP 0x03

#define UNCOMPRESSED_FILE_SIZE_LIMIT 200 /* Compress files over this size before header added */

#define PFH_TYPE_ASCII 0
//#define PFH_TYPE_WOD 3
#define PFH_TYPE_AL 223
#define PFH_TYPE_BL 202
#define PFH_TYPE_WL 203 // WOD Log
#define PFH_TYPE_IMAGES 211
#define PFH_TYPE_SEN_WOD 240 // SENSOR WOD Log

// These offsets are to the start of the field, i.e. they point to the ID number not the data.
#define FILE_ID_BYTE_POS 2
#define UPLOAD_TIME_BYTE_POS_EX_SOURCE_LEN 82
#define FILE_SIZE_BYTE_POS 26
#define BODY_OFFSET_BYTE_POS 65
#define HEADER_CHECKSUM_BYTE_POS 60

#define MAX_PFH_LENGTH 2048

#define PSF_FILE_EXT ".act"
#define PSF_FILE_TMP ".tmp"

#define PFH_NUM_OF_SPARE_FIELDS 5
#define PFH_SHORT_CHAR_FIELD_LEN 33
#define PFH_LONG_CHAR_FIELD_LEN 65

typedef struct {
  /* required Header Information */
  uint32_t fileId;              /* 0x01 */
  char          fileName[9];         /* 0x02 */
  char          fileExt[4];          /* 0x03 */
  uint32_t fileSize;            /* 0x04 */
  uint32_t createTime;          /* 0x05 */
  uint32_t modifiedTime;        /* 0x06 */
  unsigned char SEUflag;             /* 0x07 */
  unsigned char fileType;            /* 0x08 */
  unsigned short int  bodyCRC;             /* 0x09 */
  unsigned short int  headerCRC;           /* 0x0A */
  unsigned short int  bodyOffset;          /* 0x0B */

  /* Extended Header Information */
  char          source[PFH_SHORT_CHAR_FIELD_LEN];          /* 0x10 */
  char          source_length;       /* This is the actual length of the source field on disk, which may have been truncated when parsed */
  char          uploader[7];         /* 0x11 */
  uint32_t uploadTime;          /* 0x12 */   /* Note that this is a Mandatory item on all files */
  unsigned char downloadCount;       /* 0x13 */
  char          destination[PFH_SHORT_CHAR_FIELD_LEN];     /* 0x14 */
  char          downloader[7];       /* 0x15 */
  uint32_t downloadTime;        /* 0x16 */
  uint32_t expireTime;          /* 0x17 */
  unsigned char priority;            /* 0x18 */

  /* Optional Header Information */
  unsigned char compression;         /* 0x19 */
  char          BBSMessageType;      /* 0x20 */
  char          BID[PFH_SHORT_CHAR_FIELD_LEN];             /* 0x21 */
  char          title[PFH_LONG_CHAR_FIELD_LEN];           /* 0x22 */
  char          keyWords[PFH_SHORT_CHAR_FIELD_LEN];        /* 0x23 */
  char          file_description[PFH_SHORT_CHAR_FIELD_LEN];     /* 0x24 */
  char          compressionDesc[PFH_SHORT_CHAR_FIELD_LEN]; /* 0x25 */
  char          userFileName[PFH_SHORT_CHAR_FIELD_LEN];    /* 0x26 */

  int			other_id[PFH_NUM_OF_SPARE_FIELDS]; /* 0x42 - 0x44 or others*/
  char          other_data[PFH_NUM_OF_SPARE_FIELDS][PFH_SHORT_CHAR_FIELD_LEN];    /* 0x42 - 0x44 or others*/


}
HEADER;

void pfh_get_8_3_filename(HEADER *hdr, char *dir_name, char *filename, int max_len);
void pfh_get_user_filename(HEADER *hdr,  char *dir_name, char *filename, int max_len);
HEADER *pfh_new_header();
HEADER * pfh_extract_header(unsigned char *buffer, int nBytes, int *size, int *crc_passed);
int pfh_add_keyword(HEADER *pfh, char *key);
int pfh_remove_keyword(HEADER *pfh, char *key);
int pfh_contains_keyword(HEADER *pfh, char *key);
int pfh_extract_file(HEADER *pfh, char *dest_folder);
int pfh_extract_file_and_update_keywords(HEADER *pfh, char *dest_folder, int update_keywords_and_expiry);
int pfh_update_pacsat_header(HEADER *pfh, char *dir_folder);
HEADER * pfh_load_from_file(char *filename);
void pfh_debug_print(HEADER *pfh);
unsigned char * pfh_store_short(unsigned char *buffer, unsigned short n);
unsigned char * pfh_store_int(unsigned char *buffer, unsigned int n);
HEADER * pfh_make_internal_header(time_t now, uint8_t file_type, unsigned int id, char *filename,
		char *source, char *destination, char *title, char *user_filename, time_t update_time,
		int expire_time, char compression_type);
int pfh_make_internal_file(HEADER *pfh, char *dir_folder, char *body_filename);

int test_pfh_make_pacsat_file(HEADER *pfh, char *dir_folder);
int test_pacsat_header();
int write_test_msg(char *dir_folder, char *pfh_filename, char *contents, int length);
int test_pfh_checksum() ;
HEADER * make_test_header(unsigned int id, char *filename, char *source, char *destination, char *title, char *user_filename) ;
int test_pacsat_header_disk_access();

#endif /* PACSAT_HEADER_H_ */
