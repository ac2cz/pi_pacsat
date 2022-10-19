/*
 * pacsat_header.c
 *
 * Based on header.c by John Melton (G0ORX/N6LYT)
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
 * The Pacsatfile header format is descibed in the Pacsat File Definition:
 * https://www.g0kla.com/pacsat/fhead.txt
 *
 * All values in the PFH are stored in little endian format.  Given our CPU
 * for the Pacsat is little endian, no byte manipulation is needed.  This
 * may not be the case on the ground where the file is received.
 *
 * The PFH consists of the following:
 *  Mandatory Header - these fields are always present
 *  Extended Header - these fields are present on all messages
 *  Optional Header - present if needed
 *
 *  The header must start with 0xaa55
 *
 *  Thus, there are 3 forms of PACSAT file header:
 *
 *    <0xaa><0x55><Mandatory hdr><Hdr end>
 *    <0xaa><0x55><Mandatory hdr><Extended hdr><Hdr end>
 *    <0xaa><0x55><Mandatory hdr><Extended hdr>[<Optional    Items> . . . ]<Hdr end>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <string.h>

#include "config.h"
#include "pacsat_header.h"
#include "str_util.h"

/* Forward declarations */
void header_copy_to_str(unsigned char *, int, char *, int);
int pfh_save_pacsatfile(unsigned char * header, int header_len, char *filename, char *body_filename);
unsigned char * pfh_store_short(unsigned char *buffer, unsigned short n);
unsigned char * pfh_store_int(unsigned char *buffer, unsigned int n);
unsigned char * pfh_store_char_field(unsigned char *buffer, unsigned short int id, unsigned char val);
unsigned char * pfh_store_short_int_field(unsigned char *buffer, unsigned short int id, unsigned short int val);
unsigned char * pfh_store_int_field(unsigned char *buffer, unsigned short int id, unsigned int val);
unsigned char * pfh_store_str_field(unsigned char *buffer, unsigned short id, unsigned char len, char* str);
unsigned char * add_mandatory_header(unsigned char *p, HEADER *pfh);
unsigned char * add_extended_header(unsigned char *p, HEADER *pfh);
unsigned char * add_optional_header(unsigned char *p, HEADER *pfh);

/**
 * pfh_new_header()
 *
 * Allocate space for a new Pacsat File Header structure and initialize all of the
 * values.  The caller is responsible for freeing the storage later.
 *
 * Returns: A pointer to the allocated header.
 */
HEADER *pfh_new_header() {
	HEADER  *hdr;

	if ((hdr = (HEADER *)malloc(sizeof(HEADER))) != NULL) {
		hdr->fileId             = 0;
		hdr->fileName[0]        = '\0';
		hdr->fileExt[0]         = '\0';
		hdr->fileSize           = 0;
		hdr->createTime         = 0;
		hdr->modifiedTime       = 0;
		hdr->SEUflag            = 0;
		hdr->fileType           = 0;
		hdr->bodyCRC            = 0;
		hdr->headerCRC          = 0;
		hdr->bodyOffset         = 0;

		hdr->source[0]          = '\0';
		hdr->uploader[0]        = '\0';
		hdr->uploadTime         = 0;
		hdr->downloadCount      = 0;
		hdr->destination[0]     = '\0';
		hdr->downloader[0]      = '\0';
		hdr->downloadTime       = 0;
		hdr->expireTime         = 0;
		hdr->priority           = 0;
		hdr->compression        = 0;
		hdr->BBSMessageType     = ' ';
		hdr->BID[0]             = '\0';
		hdr->title[0]           = '\0';
		hdr->keyWords[0]        = '\0';
		hdr->file_description[0]     = '\0';
		hdr->compressionDesc[0] = '\0';
		hdr->userFileName[0]    = '\0';
		return hdr;
	}
	return NULL;
}

// TODO - should pass in a max length and use strlcpy etc to avoid buffer overruns
void get_filename(HEADER *hdr, char *dir_name, char *filename) {
	strcpy(filename, dir_name);
	strcat(filename, "/");
	strcat(filename, hdr->fileName);
	strcat(filename, ".");
	strcat(filename, PSF_FILE_EXT);
}

void get_user_filename(HEADER *hdr,  char *dir_name, char *filename) {
	strcpy(filename, dir_name);
	strcat(filename, "/");
	strcat(filename, hdr->userFileName);
}

/**
 * pfh_extract_header()
 *
 * Extract the header from a byte buffer
 * This allocates the memory for a new header and returns it.  The caller must
 * later free the memory.
 *
 * Returns: A pointer to the allocated header.
 */
HEADER * pfh_extract_header(unsigned char *buffer, int nBytes, int *size) {
	int i = 0;
	int bMore = 0;
	unsigned id;
	unsigned char length;
	HEADER  *hdr;

	hdr = pfh_new_header();
	if (hdr != NULL ){

		if (buffer[0] != 0xAA || buffer[1] != 0x55)
		{
			free((char *)hdr);
			return (HEADER *)0;
		}

		bMore = 1;

		i = 2; /* skip over 0xAA 0x55 */

		while (bMore && i < nBytes)
		{
			id = buffer[i++];
			id += buffer[i++] << 8;
			length = buffer[i++];

			//debug_print("ExtractHeader: id:%X length:%d ", id, length);

			switch (id)
			{
			case 0x00:
				bMore = 0;
				break;
			case FILE_ID:
				hdr->fileId = *(unsigned int *)&buffer[i];
				break;
			case FILE_NAME:
				header_copy_to_str(&buffer[i], length, hdr->fileName, 9);
				break;
			case FILE_EXT:
				header_copy_to_str(&buffer[i], length, hdr->fileExt, 3);
				break;
			case FILE_SIZE:
				hdr->fileSize = *(unsigned int *)&buffer[i];
				break;
			case CREATE_TIME:
				hdr->createTime = *(unsigned int *)&buffer[i];
				//ConvertTime(&hdr->createTime);
				break;
			case LAST_MOD_TIME:
				hdr->modifiedTime = *(unsigned int *)&buffer[i];
				//ConvertTime(&hdr->modifiedTime);
				break;
			case SEU_FLAG:
				hdr->SEUflag = buffer[i];
				break;
			case FILE_TYPE:
				hdr->fileType = buffer[i];
				break;
			case BODY_CHECKSUM:
				hdr->bodyCRC = *(unsigned short *)&buffer[i];
				break;
			case HEADER_CHECKSUM:
				hdr->headerCRC = *(unsigned short *)&buffer[i];
				break;
			case BODY_OFFSET:
				hdr->bodyOffset = *(unsigned short *)&buffer[i];
				break;
			case SOURCE:
				header_copy_to_str(&buffer[i], length, hdr->source, 32);
				break;
			case AX25_UPLOADER:
				header_copy_to_str(&buffer[i], length, hdr->uploader, 6);
				break;
			case UPLOAD_TIME:
				hdr->uploadTime = *(unsigned int *)&buffer[i];
				//ConvertTime(&hdr->uploadTime);
				break;
			case DOWNLOAD_COUNT:
				hdr->downloadCount = buffer[i];
				break;
			case DESTINATION:
				header_copy_to_str(&buffer[i], length, hdr->destination, 32);
				break;
			case AX25_DOWNLOADER:
				header_copy_to_str(&buffer[i], length, hdr->downloader, 6);
				break;
			case DOWNLOAD_TIME:
				hdr->downloadTime = *(unsigned int *)&buffer[i];
				//ConvertTime(&hdr->downloadTime);
				break;
			case EXPIRE_TIME:
				hdr->expireTime = *(unsigned int *)&buffer[i];
				//ConvertTime(&hdr->expireTime);
				break;
			case PRIORITY:
				hdr->priority = buffer[i];
				break;
			case COMPRESSION_TYPE:
				hdr->compression = buffer[i];
				break;
			case BBS_MSG_TYPE:
				hdr->BBSMessageType = buffer[i];
				break;
			case BULLETIN_ID_NUMBER:
				header_copy_to_str(&buffer[i], length, hdr->BID, 32);
				break;
			case TITLE:
				header_copy_to_str(&buffer[i], length, hdr->title, 64);
				break;
			case KEYWORDS:
				header_copy_to_str(&buffer[i], length, hdr->keyWords, 32);
				break;
			case FILE_DESCRIPTION:
				header_copy_to_str(&buffer[i], length, hdr->file_description, 32);
				break;
			case COMPRESSION_DESCRIPTION:
				header_copy_to_str(&buffer[i], length, hdr->compressionDesc, 32);
				break;
			case USER_FILE_NAME:
				header_copy_to_str(&buffer[i], length, hdr->userFileName, 32);
				break;
			default:
				debug_print("** Unknown header item ** ");

			for (int n=0; n<length; n++) {
				if (isprint(buffer[i+n]))
					debug_print("%c",buffer[i+n]);
			}
			debug_print(" |");
			for (int n=0; n<length; n++)
				debug_print(" %02X",buffer[i+n]);
			debug_print("\n");

				break;
			}

			i+=length;
		}
	}

	/* let the user know the size */
	*size = i;

	/* see if we ran out of space */
	if (bMore)
	{
		free((char *)hdr);
		return NULL;
	}

	return hdr;
}

/**
 * pfh_make_pacsat_file()
 *
 * Create a new Pacsat File based on a Header and body file.  Save the new file
 * into out_filename
 *
 * Returns: EXIT SUCCESS or EXIT_FAILURE
 */
int pfh_make_pacsat_file(HEADER *pfh, char *dir_folder) {
	if (pfh == NULL) return EXIT_FAILURE;

	char body_filename[MAX_FILE_PATH_LEN];
	char out_filename[MAX_FILE_PATH_LEN];
	get_user_filename(pfh, dir_folder, body_filename);
	get_filename(pfh,dir_folder, out_filename);

	/* Clean up data values.  These are usually callsigns.
	 * The spec says source and destination can be mixed case, but typically they
	 * are in upper case */
	for (int a=0; pfh->source[a]!=0; a++)
		pfh->source[a]=toupper(pfh->source[a]);
	for (int a=0; pfh->destination[a]!=0; a++)
		pfh->destination[a]=toupper(pfh->destination[a]);
	for (int a=0; pfh->uploader[a]!=0; a++)
		pfh->uploader[a]=toupper(pfh->uploader[a]);
	for (int a=0; pfh->downloader[a]!=0; a++)
		pfh->downloader[a]=toupper(pfh->downloader[a]);

	/* Measure body_size and calculate body_checksum */
	unsigned short int body_checksum = 0;
	unsigned int body_size = 0;

	FILE *infile = fopen(body_filename, "rb");
	if (infile == NULL) return EXIT_FAILURE;

	int ch=fgetc(infile);

	while (ch!=EOF) {
		body_checksum+=(unsigned short)ch;
		body_size++;
		ch=fgetc(infile);
	}
	fclose(infile);
	pfh->bodyCRC = body_checksum;

	/* Build Pacsat File Header */
	unsigned char buffer[1024];
	unsigned char *p = &buffer[0];

	/* All PFHs start with 0xaa55 */
	*p++ = 0xaa;
	*p++ = 0x55;

	p = add_mandatory_header(p, pfh);
	p = add_extended_header(p, pfh);
	p = add_optional_header(p, pfh);

	/* End the PFH */
	*p++  = 0x00;
	*p++ = 0x00;
	*p++ = 0x00;

	/* update some items in the mandatory header.  They are at a fixed offset because
	 * all the items are mandatory
	 */
	pfh->bodyOffset = p-&buffer[0];
	pfh->fileSize = pfh->bodyOffset + body_size;
	//debug_print("Body Offset: %02x\n",pfh->bodyOffset);
	//debug_print("File Size: %04x\n",pfh->fileSize);
	pfh_store_int_field(&buffer[FILE_SIZE_BYTE_POS] , FILE_SIZE, pfh->fileSize);
	pfh_store_short_int_field(&buffer[BODY_OFFSET_BYTE_POS] , BODY_OFFSET, pfh->bodyOffset);

	int rc = pfh_save_pacsatfile(buffer, pfh->bodyOffset, out_filename, body_filename);

	return rc;
}

/**
 * pfh_load_from_file()
 *
 * Extract the header from a file on disk
 * The filename needs to be the full path to the file
 * This allocates the memory for a new header and returns it.  The caller must
 * later free the memory.
 *
 * Returns: A pointer to the header or NULL if something went wrong
 */
HEADER * pfh_load_from_file(char *filename) {
	HEADER * pfh;
	FILE * f = fopen(filename, "r");
	if (f == NULL) {
		return NULL;
	}
	unsigned char buffer[1024]; // needs to be bigger than largest header but does not need to be the whole file
	int num = fread(buffer, sizeof(char), 1024, f);
	if (num == 0) {
		fclose(f);
		return NULL; // nothing was read
	}
	int size;
	pfh = pfh_extract_header(buffer, 1025, &size);
	//debug_print("Read: %d Header size: %d\n",num, size);
	fclose(f);
	return pfh;
}


/**
 * header_copy_to_str()
 *
 * Copy length bytes, but at most maxbytes, from a header into a string.  Terminate the string.
 *
 */
void header_copy_to_str(unsigned char *header, int length, char *destination, int maxbytes) {
	if (length > maxbytes) length = maxbytes;

	while (length > 0)
	{
		*destination++ = *header++;
		length--;
	}

	*destination = '\0';
}

/**
 * pfh_debug_print()
 *
 * Print key items from the pacsat header for debugging.  Prints nothing
 * if DEBUG is set to 0
 *
 */
void pfh_debug_print(HEADER *pfh) {
	if (pfh == NULL) return;
	debug_print("PFH: File:%04x %s.%s ", (int)pfh->fileId, pfh->fileName, pfh->fileExt);

	debug_print("Source:%s ", pfh->source);
	debug_print("Dest:%s ", pfh->destination);

	char buf[30];
	time_t now = pfh->createTime;
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
	debug_print("Cr:%s ", buf);
	now = pfh->uploadTime;
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
	debug_print("Up:%s ", buf);
	debug_print(" Contains:%s\n", pfh->userFileName);
}

/**
 * pfh_save_pacsatfile()
 *
 * Create a Pacsat file based on the PFH byte steam specified with header and the file specified
 * by body_filename.  The resulting pacsat file is saved in filename
 *
 * Returns: EXIT_SUCCESS or EXIT_FAILURE
 *
 */
int pfh_save_pacsatfile(unsigned char * header, int header_len, char *filename, char *body_filename) {

	FILE * outfile = fopen(filename, "wb");
	if (outfile == NULL) return EXIT_FAILURE;

	/* Save the header bytes */
	for (int i=0; i<header_len; i++) {
		int c = fputc(header[i],outfile);
		if (c == EOF) {
			fclose(outfile);
			return EXIT_FAILURE; // we could not write to the file
		}
	}

	/* Add the file contents */
	FILE * infile=fopen(body_filename,"rb");
	if (infile == NULL) {
		fclose(outfile);
		return EXIT_FAILURE;
	}
	int ch=fgetc(infile);
	if (ch == EOF) {
		fclose(infile);
		fclose(outfile);
		return EXIT_FAILURE; // we could not read from to the infile
	}
	while (ch!=EOF) {
		int c = fputc((unsigned int)ch,outfile);
		if (c == EOF) {
			fclose(infile);
			fclose(outfile);
			return EXIT_FAILURE; // we could not write to the file
		}
		ch=fgetc(infile);
	}

	fclose(infile);
	fclose(outfile);

	return EXIT_SUCCESS;
}


unsigned char * pfh_store_short(unsigned char *buffer, unsigned short n) {
	buffer[1] = (n >> 8) & 0xFF;
	buffer[0] = n & 0xFF;
	return &buffer[2];
}

/**
 * Store a little endian 4 byte int into the pacsat header
 * at the position of the passed pointer
 */
unsigned char * pfh_store_int(unsigned char *buffer, unsigned int n) {
	buffer[3] = (n >> 24) & 0xFF;
	buffer[2] = (n >> 16) & 0xFF;
	buffer[1] = (n >> 8) & 0xFF;
	buffer[0] = n & 0xFF;
	return &buffer[4];
}

unsigned char * pfh_store_char_field(unsigned char *buffer, unsigned short int id, unsigned char val) {
	buffer = pfh_store_short(buffer,id);
	*buffer++ = 0x01;
	*buffer++ = val;
	return buffer;
}

unsigned char * pfh_store_short_int_field(unsigned char *buffer, unsigned short int id, unsigned short int val) {
	buffer = pfh_store_short(buffer,id);
	*buffer++ = 0x02;
	buffer = pfh_store_short(buffer, val);
	return buffer;
}

unsigned char * pfh_store_int_field(unsigned char *buffer, unsigned short int id, unsigned int val) {
	buffer = pfh_store_short(buffer,id);
	*buffer++ = 0x04;
	buffer = pfh_store_int(buffer, val);
	return buffer;
}

unsigned char * pfh_store_str_field(unsigned char *buffer, unsigned short id, unsigned char len, char* str) {
	buffer = pfh_store_short(buffer,id);
	*buffer++ = len;
	for (int i=0; i < len; i++) {
		buffer[i] = str[i];
	}
	return buffer + len;
}

unsigned char * add_mandatory_header(unsigned char *p, HEADER *pfh) {
	/* Mandatory header */
	p = pfh_store_int_field(p, FILE_ID, pfh->fileId);
	p = pfh_store_str_field(p, FILE_NAME, 9, pfh->fileName);
	p = pfh_store_str_field(p, FILE_EXT, 3, pfh->fileExt);
	p = pfh_store_int_field(p, FILE_SIZE, pfh->fileSize); // this is the size of the header and file, so populated at end
	p = pfh_store_int_field(p, CREATE_TIME, pfh->createTime);
	p = pfh_store_int_field(p, LAST_MOD_TIME, pfh->modifiedTime);
	p = pfh_store_char_field(p, SEU_FLAG, pfh->SEUflag);
	p = pfh_store_char_field(p, FILE_TYPE, pfh->fileType);
	p = pfh_store_short_int_field(p, BODY_CHECKSUM, pfh->bodyCRC);
	p = pfh_store_short_int_field(p, HEADER_CHECKSUM, pfh->headerCRC);  //this is blank until header fully populated
	p = pfh_store_short_int_field(p, BODY_OFFSET, pfh->bodyOffset); // size of header so populated at end
	return p;
}

unsigned char * add_extended_header(unsigned char *p, HEADER *pfh) {
	/* Extended Header - all message files have this
	 * If a Extended Header is present, it must immediately follow the final item in
	 * the Mandatory Header.
	 *
	 * If any Extended Header item is present, all must be present.
	 *
	 * Extended Header items must be present in order of ascending value of <id>,
	 * with the exception that multiple destinations are represented by multiple
	 * occurrences of items 0x14, 0x15, and 0x16.
	 */
	p = pfh_store_str_field(p, SOURCE, strlen(pfh->source), pfh->source);
	p = pfh_store_str_field(p, AX25_UPLOADER, 7, pfh->uploader);
	p = pfh_store_int_field(p, UPLOAD_TIME, pfh->uploadTime);
	p = pfh_store_char_field(p, DOWNLOAD_COUNT, pfh->downloadCount);
	p = pfh_store_str_field(p, DESTINATION, strlen(pfh->destination), pfh->destination);
	p = pfh_store_str_field(p, AX25_DOWNLOADER, 7, pfh->downloader);
	p = pfh_store_int_field(p, DOWNLOAD_TIME, pfh->downloadTime);
	p = pfh_store_int_field(p, EXPIRE_TIME, pfh->expireTime);
	p = pfh_store_char_field(p, PRIORITY, pfh->priority);
	return p;
}

unsigned char * add_optional_header(unsigned char *p, HEADER *pfh) {
	/* Optional Header items
	 * The Mandatory Header and Extended Header may be followed by any number of
	 * Optional Header items.  It is intended that any expansion of the PFH defini-
	 * tion will involve only addition of Optional Items
	 *
	 * Optional Header items need not be presented in increasing order of <id>.
	 */
	if (pfh->BBSMessageType != 0)
		p = pfh_store_char_field(p, BBS_MSG_TYPE, pfh->BBSMessageType);
	if (pfh->BID[0] != 0)
		p = pfh_store_str_field(p, BULLETIN_ID_NUMBER, strlen(pfh->BID), pfh->BID);
	if (pfh->compression != BODY_NOT_COMPRESSED)
		p = pfh_store_char_field(p, COMPRESSION_TYPE, pfh->compression);
	if (pfh->title[0] != 0)
		p = pfh_store_str_field(p, TITLE, strlen(pfh->title), pfh->title);
	if (pfh->keyWords[0] != 0)
		p = pfh_store_str_field(p, KEYWORDS, strlen(pfh->keyWords), pfh->keyWords);
	if (pfh->file_description[0] != 0)
		p = pfh_store_str_field(p, FILE_DESCRIPTION, strlen(pfh->file_description), pfh->file_description);
	if (pfh->compressionDesc[0] != 0)
		p = pfh_store_str_field(p, COMPRESSION_DESCRIPTION, strlen(pfh->compressionDesc), pfh->compressionDesc);
	if (pfh->userFileName[0] != 0)
			p = pfh_store_str_field(p, USER_FILE_NAME, strlen(pfh->userFileName), pfh->userFileName);
	return p;
}

/*
 * SELF TESTS FOLLOW
 */

void pfh_populate_test_header(HEADER *pfh, char *user_file_name) {
	if (pfh != NULL) {
		/* Required Header Information */
		pfh->fileId = 0x1234;
		strlcpy(pfh->fileName,"1234", sizeof(pfh->fileName));
		strlcpy(pfh->fileExt,PSF_FILE_EXT, sizeof(pfh->fileExt));

		time_t now = time(0); // Get the system time in seconds since the epoch
		//printf("now: %d\n",now);
		pfh->createTime = now;
		pfh->modifiedTime = now;
		pfh->SEUflag = 1;
		pfh->fileType = PFH_TYPE_ASCII;

		/* Extended Header Information */
		strlcpy(pfh->source,"g0kla@iss.in.orbit", sizeof(pfh->source));
		strlcpy(pfh->uploader,"g0kla", sizeof(pfh->uploader));
		/* TEST - This is set when received by the server */
		pfh->uploadTime = now;
		pfh->downloadCount = 54;
		strlcpy(pfh->destination,"AC2CZ", sizeof(pfh->destination));
		strlcpy(pfh->downloader,"ve2xyz", sizeof(pfh->downloader));
		pfh->downloadTime = now;
		pfh->expireTime = now;
		pfh->priority = 9;

		/* Optional Header Information */
		pfh->compression = BODY_COMPRESSED_PKZIP;
		pfh->BBSMessageType = 7;
		strlcpy(pfh->BID,"A54wqgjhsgf8*", sizeof(pfh->BID));
		strlcpy(pfh->title,"This is a test message", sizeof(pfh->title));
		strlcpy(pfh->keyWords,"TEST PACSAT ARISS", sizeof(pfh->keyWords));
		strlcpy(pfh->file_description,"TEST PACSAT ARISS", sizeof(pfh->file_description));
		strlcpy(pfh->compressionDesc,"Standard PKZIP", sizeof(pfh->compressionDesc));
		strlcpy(pfh->userFileName,user_file_name, sizeof(pfh->userFileName));
	}
}

int write_test_msg(char *dir_folder, char *pfh_filename, char *contents, int length) {
	char filename[MAX_FILE_PATH_LEN];
	strcpy(filename, dir_folder);
	strcat(filename, "/");
	strcat(filename, pfh_filename);
	FILE * outfile = fopen(filename, "wb");
	if (outfile == NULL) return EXIT_FAILURE;
	for (int i=0; i<length; i++) {
		int c = fputc(contents[i],outfile);
		if (c == EOF) {
			fclose(outfile);
			EXIT_FAILURE; // we could not write to the file
		}
	}
	fclose(outfile);
	return EXIT_SUCCESS;
}

int test_pacsat_header() {
	printf(" TEST PACSAT HEADER:\n");
	int rc = EXIT_SUCCESS;
	char *filename1 = "1234.act";
	char *userfilename1 = "file.txt";
	HEADER *pfh= pfh_new_header();
	pfh_populate_test_header(pfh, userfilename1);
	pfh_debug_print(pfh);

	/* Make test files */

	char *msg = "Hi there,\nThis is a test message\n73 Chris\n";
	rc = write_test_msg(".", userfilename1, msg, strlen(msg));
	if (rc != EXIT_SUCCESS) { printf("** Failed to make file.txt file.\n"); return EXIT_FAILURE; }

	rc = pfh_make_pacsat_file(pfh, ".");
	if (rc != EXIT_SUCCESS) { printf("** Failed to make pacsat file.  Make sure there is a test file called %s\n",userfilename1); return EXIT_FAILURE; }

	HEADER * pfh2 = pfh_load_from_file(filename1);
	if (pfh2 == NULL) { printf("** Failed to load pacsat header\n"); return EXIT_FAILURE; }
	pfh_debug_print(pfh2);
	/* Check the mandatory fields match */
	if (pfh->fileId != pfh2->fileId) { 				printf("** Mismatched fileId\n"); rc = EXIT_FAILURE; }
	if (strcmp(pfh->fileName,pfh2->fileName) != 0) {printf("** Mismatched fileName\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->fileExt,pfh2->fileExt) != 0) { 	printf("** Mismatched fileExt\n"); rc = EXIT_FAILURE;}
	if (pfh->fileSize != pfh2->fileSize) { 			printf("** Mismatched fileSize\n"); rc = EXIT_FAILURE;}
	if (pfh->createTime != pfh2->createTime) {		printf("** Mismatched createTime\n"); rc = EXIT_FAILURE;}
	if (pfh->modifiedTime != pfh2->modifiedTime) {	printf("** Mismatched modifiedTime\n"); rc = EXIT_FAILURE;}
	if (pfh->SEUflag != pfh2->SEUflag) {			printf("** Mismatched SEUflag\n"); rc = EXIT_FAILURE;}
	if (pfh->fileType != pfh2->fileType) {			printf("** Mismatched fileType\n"); rc = EXIT_FAILURE;}
	if (pfh->bodyCRC != pfh2->bodyCRC) {			printf("** Mismatched bodyCRC\n"); rc = EXIT_FAILURE;}
	if (pfh->headerCRC != pfh2->headerCRC) {		printf("** Mismatched headerCRC\n"); rc = EXIT_FAILURE;}
	if (pfh->bodyOffset != pfh2->bodyOffset) {		printf("** Mismatched bodyOffset\n"); rc = EXIT_FAILURE;}

	/* Check the extended header matches */
	if (strcmp(pfh->source,pfh2->source) != 0) {	printf("** Mismatched source\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->uploader,pfh2->uploader) != 0) {printf("** Mismatched uploader\n"); rc = EXIT_FAILURE;}
	if (pfh->uploadTime != pfh2->uploadTime) { 		printf("** Mismatched uploadTime\n"); rc = EXIT_FAILURE;}
	if (pfh->downloadCount != pfh2->downloadCount) {printf("** Mismatched downloadCount\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->destination,pfh2->destination) != 0) {printf("** Mismatched destination\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->downloader,pfh2->downloader) != 0) { 	printf("** Mismatched downloader\n"); rc = EXIT_FAILURE;}
	if (pfh->downloadTime != pfh2->downloadTime) { 	printf("** Mismatched downloadTime\n"); rc = EXIT_FAILURE;}
	if (pfh->expireTime != pfh2->expireTime) { 		printf("** Mismatched expireTime\n"); rc = EXIT_FAILURE;}
	if (pfh->priority != pfh2->priority) { 			printf("** Mismatched priority\n"); rc = EXIT_FAILURE;}

	/* Check the optional header items match */
	if (pfh->compression != pfh2->compression) { 	printf("** Mismatched compression\n"); rc = EXIT_FAILURE;}
	if (pfh->BBSMessageType != pfh2->BBSMessageType) {		printf("** Mismatched BBSMessageType\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->BID,pfh2->BID) != 0) {			printf("** Mismatched BID\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->title,pfh2->title) != 0) {		printf("** Mismatched title\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->keyWords,pfh2->keyWords) != 0) {printf("** Mismatched keyWords\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->file_description,pfh2->file_description) != 0) {printf("** Mismatched description\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->compressionDesc,pfh2->compressionDesc) != 0) {printf("** Mismatched compressionDesc\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->userFileName,pfh2->userFileName) != 0) {printf("** Mismatched userFileName\n"); rc = EXIT_FAILURE;}

	free(pfh);
	free(pfh2);

	if (rc == EXIT_SUCCESS)
		printf(" TEST PACSAT HEADER: success:\n");
	else
		printf(" TEST PACSAT HEADER: fail:\n");
	return rc;
}
