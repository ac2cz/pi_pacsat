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
#include <errno.h>
#include <sys/stat.h>

#include "config.h"
#include "pacsat_header.h"
#include "pacsat_dir.h"
#include "str_util.h"

/* Forward declarations */
void header_copy_to_str(unsigned char *, int, char *, int);
int pfh_save_pacsatfile(unsigned char * header, int header_len, char *filename, char *body_filename);
int pfh_generate_header_bytes(HEADER *pfh, int body_size, unsigned char *header_bytes);
unsigned char * pfh_store_char_field(unsigned char *buffer, unsigned short int id, unsigned char val);
unsigned char * pfh_store_short_int_field(unsigned char *buffer, unsigned short int id, unsigned short int val);
unsigned char * pfh_store_int_field(unsigned char *buffer, unsigned short int id, unsigned int val);
unsigned char * pfh_store_str_field(unsigned char *buffer, unsigned short id, unsigned char len, char* str);
unsigned char * add_mandatory_header(unsigned char *p, HEADER *pfh);
unsigned char * add_extended_header(unsigned char *p, HEADER *pfh);
unsigned char * add_optional_header(unsigned char *p, HEADER *pfh);

void pfh_populate_test_header(int id, HEADER *pfh, char *user_file_name);

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
		/* Mandatory */
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

		/* Extended */
		hdr->source[0]          = '\0';
	    hdr->source_length      = 0;
		hdr->uploader[0]        = '\0';
		hdr->uploadTime         = 0;
		hdr->downloadCount      = 0;
		hdr->destination[0]     = '\0';
		hdr->downloader[0]      = '\0';
		hdr->downloadTime       = 0;
		hdr->expireTime         = 0;
		hdr->priority           = 0;

		/* Optional */
		hdr->compression        = 0;
		hdr->BBSMessageType     = ' ';
		hdr->BID[0]             = '\0';
		hdr->title[0]           = '\0';
		hdr->keyWords[0]        = '\0';
		hdr->file_description[0]     = '\0';
		hdr->compressionDesc[0] = '\0';
		hdr->userFileName[0]    = '\0';

		int i;
		for (i = 0; i < PFH_NUM_OF_SPARE_FIELDS; i++) {
			hdr->other_id[i] = 0;
			hdr->other_data[i][0] = '\0';
		}
		return hdr;
	}
	return NULL;
}

// TODO - make sure that we are not assuming this is the name of the file on disk on the sat.  That is file_no.act and has no relation to this.
void pfh_get_8_3_filename(HEADER *hdr, char *dir_name, char *filename, int max_len) {
	strlcpy(filename, dir_name, max_len);
	strlcat(filename, "/", max_len);
	strlcat(filename, hdr->fileName, max_len);
	strlcat(filename, ".", max_len);
	strlcat(filename, hdr->fileExt, max_len);
}

void pfh_get_user_filename(HEADER *hdr,  char *dir_name, char *filename, int max_len) {
	strlcpy(filename, dir_name, max_len);
	strlcat(filename, "/", max_len);
	strlcat(filename, hdr->userFileName, max_len);
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
HEADER * pfh_extract_header(unsigned char *buffer, int nBytes, int *size, int *crc_passed) {
	int i = 0;
	int bMore = 0;
	unsigned id;
	unsigned char length;
	HEADER  *hdr;
	unsigned short int crc_result = 0;
	int other_field = 0;

	hdr = pfh_new_header();
	if (hdr != NULL ){

		if (buffer[0] != 0xAA || buffer[1] != 0x55)
		{
			free((char *)hdr);
			return (HEADER *)0;
		}

		bMore = 1;

		crc_result += buffer[0] & 0xff;
		//debug_print("%02x ",buffer[0]);
		crc_result += buffer[1] & 0xff;
		//debug_print("%02x ",buffer[1]);
		i = 2; /* skip over 0xAA 0x55 */

		while (bMore && i < nBytes) {
			crc_result += buffer[i] & 0xff;
			//debug_print("%02x ",buffer[i]);
			id = buffer[i++];
			crc_result += buffer[i] & 0xff;
			//debug_print("%02x ",buffer[i]);
			id += buffer[i++] << 8;
			crc_result += buffer[i] & 0xff;
			//debug_print("%02x ",buffer[i]);
			length = buffer[i++];

			if (id != HEADER_CHECKSUM) {
				for (int j=0; j<length; j++) {
					crc_result += buffer[i+j] & 0xff;
					//debug_print("%02x ",buffer[i+j]);
				}
			}

			//debug_print("ExtractHeader: id:%X length:%d \n", id, length);

			switch (id)
			{
			case 0x00:
				bMore = 0;
				break;
			case FILE_ID:
				hdr->fileId = *(unsigned int *)&buffer[i];
				break;
			case FILE_NAME:
				header_copy_to_str(&buffer[i], length, hdr->fileName, 8);
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
	            hdr->source_length = length; /* Store the actual source length in case it was truncated. */
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
				if (other_field >= PFH_NUM_OF_SPARE_FIELDS) {
					debug_print("** Too many extra fields %X skipped ** ", id);
					break;
				}
				hdr->other_id[other_field] = id;
				header_copy_to_str(&buffer[i], length, hdr->other_data[other_field], 32);

//				debug_print("** Unknown header id %X ** ", id);
//				for (int n=0; n<length; n++) {
//					if (isprint(buffer[i+n]))
//						debug_print("%c",buffer[i+n]);
//				}
//				debug_print(" |");
//				for (int n=0; n<length; n++)
//					debug_print(" %02X",buffer[i+n]);
//				debug_print("\n");
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

//	debug_print("CRC: %02x\n", crc_result);
	if (crc_result == hdr->headerCRC )
		*crc_passed = true;
	else
		*crc_passed = false;

	return hdr;
}

int pfh_add_keyword(HEADER *pfh, char *keyword) {
	if (pfh_contains_keyword(pfh, keyword))
		return EXIT_SUCCESS;
	if (strlen(pfh->keyWords) > 0)
		strlcat(pfh->keyWords, " ", PFH_SHORT_CHAR_FIELD_LEN);
	strlcat(pfh->keyWords, keyword, PFH_SHORT_CHAR_FIELD_LEN);

	return EXIT_SUCCESS;
}

int pfh_remove_keyword(HEADER *pfh, char *keyword) {
	char new_keywords[PFH_SHORT_CHAR_FIELD_LEN];
	char *key = strtok(pfh->keyWords, " ");
	strlcpy(new_keywords,"", PFH_SHORT_CHAR_FIELD_LEN);
	while (key != NULL) {
		if (strncmp(key, keyword, PFH_SHORT_CHAR_FIELD_LEN) != 0) {
			strlcat(new_keywords,key, PFH_SHORT_CHAR_FIELD_LEN);
			strlcat(new_keywords," ", PFH_SHORT_CHAR_FIELD_LEN);
		}
		key = strtok(NULL, " ");
	}
	if (strlen(new_keywords) > 0) {
		new_keywords[strlen(new_keywords)-1] = 0; // we always have an extra space, so remove it
	}
	strlcpy(pfh->keyWords,new_keywords, PFH_SHORT_CHAR_FIELD_LEN);

	return EXIT_SUCCESS;
}

int pfh_contains_keyword(HEADER *pfh, char *keyword) {
	char *key = strtok(pfh->keyWords, " ");
	while (key != NULL) {
		if (strncmp(key, keyword, PFH_SHORT_CHAR_FIELD_LEN) == 0) {
			return true;
		}
		key = strtok(NULL, " ");
	}
	return false;
}

/**
 * pfh_generate_header_bytes()
 *
 * Generate the header bytes from the structure.  header_bytes should
 * be passed in and be sufficient length for the generated bytes. The
 * number of bytes generated is returned.  This should be equal to
 * the body_offset
 *
 * The checksums will be calculated.
 *
 */
int pfh_generate_header_bytes(HEADER *pfh, int body_size, unsigned char *header_bytes) {
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

	unsigned char *p = &header_bytes[0];

	/* All PFHs start with 0xaa55 */
	*p++ = 0xaa;
	*p++ = 0x55;

	pfh->headerCRC = 0; /* Zero this out so it is recalculated correctly */
	p = add_mandatory_header(p, pfh);
	p = add_extended_header(p, pfh);
	p = add_optional_header(p, pfh);

	/* End the PFH */
	*p++  = 0x00;
	*p++ = 0x00;
	*p++ = 0x00;

	/* update some items in the mandatory header.  They are at a fixed offset because
	 * all the items before them are mandatory
	 */
	pfh->bodyOffset = p - &header_bytes[0];
	pfh->fileSize = pfh->bodyOffset + body_size;
	//debug_print("Body Offset: %02x\n",pfh->bodyOffset);
	//debug_print("File Size: %04x\n",pfh->fileSize);
	pfh_store_int_field(&header_bytes[FILE_SIZE_BYTE_POS] , FILE_SIZE, pfh->fileSize);
	pfh_store_short_int_field(&header_bytes[BODY_OFFSET_BYTE_POS] , BODY_OFFSET, pfh->bodyOffset);

	/* Now that all fields are populated we need to calculate the header checksum */
	short int header_checksum = 0;
	for (int i=0; i< pfh->bodyOffset; i++) {
		header_checksum += header_bytes[i] & 0xff;
	}
	pfh->headerCRC = header_checksum;
	pfh_store_short_int_field(&header_bytes[HEADER_CHECKSUM_BYTE_POS] , HEADER_CHECKSUM, header_checksum);
	return pfh->bodyOffset;
}

/**
 * pfh_update_pacsat_header()
 *
 * Update the header in a PACSAT file.  This will recalculate any checksums and
 * save the new bytes to the start of the file.  All fields except the header
 * checksum need to be correct.
 *
 * TODO - this does not preserve PFH fields that we do not know about. But it does
 * allow 5 arbitrary fields that the ground station can add.  If more unknown fields are
 * included then they will be lost in this update.
 * It may be better to update fields in place using the routines in dir.
 *
 */
int pfh_update_pacsat_header(HEADER *pfh, char *dir_folder) {
	char in_filename[MAX_FILE_PATH_LEN];
	dir_get_file_path_from_file_id(pfh->fileId, dir_folder, in_filename, MAX_FILE_PATH_LEN);

	/* Build Pacsat File Header */
	unsigned char pfh_buffer[MAX_PFH_LENGTH];
	int original_body_offset = pfh->bodyOffset;
	int body_size = pfh->fileSize - pfh->bodyOffset;
	int pfh_len = pfh_generate_header_bytes(pfh, body_size, pfh_buffer);

	char tmp_filename[MAX_FILE_PATH_LEN];
	strlcpy(tmp_filename, in_filename, MAX_FILE_PATH_LEN);
	strlcat(tmp_filename, ".", MAX_FILE_PATH_LEN);
	strlcat(tmp_filename, "tmp", MAX_FILE_PATH_LEN); /* This will give it a name like 0005.act.tmp */

	FILE * outfile = fopen(tmp_filename, "wb");
	if (outfile == NULL) return EXIT_FAILURE;

	/* Save the header bytes, which might be shorter or longer than the original header */
	for (int i=0; i<pfh_len; i++) {
		int c = fputc(pfh_buffer[i],outfile);
		if (c == EOF) {
			fclose(outfile);
			return EXIT_FAILURE; // we could not write to the file
		}
	}

	/* Add the file contents */
	FILE * infile=fopen(in_filename,"rb");
	if (infile == NULL) {
		fclose(outfile);
		return EXIT_FAILURE;
	}
	fseek(infile, original_body_offset, SEEK_SET); /* Read from the start of the original body offset */
	int check_size = 0;
	int ch=fgetc(infile);
	if (ch == EOF) {
		fclose(infile);
		fclose(outfile);
		return EXIT_FAILURE; // we could not read from to the infile
	}
	while (ch!=EOF) {
		check_size++;
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
	if (check_size != body_size)
		error_print("WARNING! Wrote different sized file body for %s\n",tmp_filename)
//	if (remove(tmp_filename) != EXIT_SUCCESS) {
//		error_print("Could not remove tmp file %s\n",tmp_filename)
//	}
	if (rename(tmp_filename, in_filename) != EXIT_SUCCESS) {
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


/**
 * pfh_extract_file()
 * Open a PSF, extract the header and use the information to extract the file
 * contents.  Save the extracted file in dest_filename.
 *
 * If dest_filename is a dir then use the user_filename.
 *
 * Returns EXIT_SUCCESS if the extracted file could be saved or EXIT_FAILURE if
 * it could not.
 *
 */
int pfh_extract_file_and_update_keywords(HEADER *pfh, char *dest_folder, int update_keywords_and_expiry) {

	char src_filename[MAX_FILE_PATH_LEN];
	char dest_filepath[MAX_FILE_PATH_LEN];
	if (pfh == NULL) return EXIT_FAILURE;
	dir_get_file_path_from_file_id(pfh->fileId, get_dir_folder(), src_filename, MAX_FILE_PATH_LEN);

	if (strlen(pfh->userFileName) == 0) {
		/* Build the full path if we use fild-id as the destination file name */
		char file_name[10];
		snprintf(file_name, 10, "%04x",pfh->fileId);
		strlcpy(dest_filepath, get_data_folder(), MAX_FILE_PATH_LEN);
		strlcat(dest_filepath, "/", MAX_FILE_PATH_LEN);
		strlcat(dest_filepath, dest_folder, MAX_FILE_PATH_LEN);
		strlcat(dest_filepath, "/", MAX_FILE_PATH_LEN);
		strlcat(dest_filepath, file_name, MAX_FILE_PATH_LEN);
	} else {
		/* Just build the folder path if we are going to use the user-filename*/
		strlcpy(dest_filepath, get_data_folder(), MAX_FILE_PATH_LEN);
		strlcat(dest_filepath, "/", MAX_FILE_PATH_LEN);
		strlcat(dest_filepath, dest_folder, MAX_FILE_PATH_LEN);
		strlcat(dest_filepath, "/", MAX_FILE_PATH_LEN);
		strlcat(dest_filepath, pfh->userFileName, MAX_FILE_PATH_LEN);
	}

	char tmp_filename[MAX_FILE_PATH_LEN];
	strlcpy(tmp_filename, dest_filepath, sizeof(tmp_filename));
	strlcat(tmp_filename, ".tmp", sizeof(tmp_filename));

	FILE * outfile = fopen(tmp_filename, "wb");
	if (outfile == NULL) {
		return EXIT_FAILURE;
	}

	/* Add the file contents */
	FILE * infile=fopen(src_filename,"rb");
	if (infile == NULL) {
		fclose(outfile);
		return EXIT_FAILURE;
	}
	int32_t rc = fseek(infile, pfh->bodyOffset, SEEK_SET);
	if (rc != 0) {
		debug_print("Could not seek body offset for file: %s - %s\n",src_filename, strerror(errno));
		return EXIT_FAILURE;
	}
	int ch=fgetc(infile);
	if (ch == EOF) {
		fclose(infile);
		fclose(outfile);
		remove(tmp_filename);
		return EXIT_FAILURE; // we could not read from to the infile
	}
	while (ch!=EOF) {
		int c = fputc((unsigned int)ch,outfile);
		if (c == EOF) {
			fclose(infile);
			fclose(outfile);
			remove(tmp_filename);
			return EXIT_FAILURE; // we could not write to the file
		}
		ch=fgetc(infile);
	}

	fclose(infile);
	fclose(outfile);

	if (update_keywords_and_expiry) {
		/* If successful we change the header to include a keyword for the installed dir and set the upload and expiry dates */
		pfh_add_keyword(pfh, dest_folder);
		pfh->uploadTime = time(0);
		pfh->expireTime = 0xFFFFFF7F; // 2038
		if (pfh_update_pacsat_header(pfh, get_dir_folder()) != EXIT_SUCCESS) {
			debug_print("** Failed to re-write header in file.\n");
			remove(tmp_filename);
			return EXIT_FAILURE;
		}
	}

	/* Commit the file */
	rename(tmp_filename, dest_filepath);
	//debug_print("Extracted %s from %s\n",dest_filepath, src_filename);

	/* Try to uncompress. We do this after the commit as the keywords are now updated. Unzip can change the filename
	 * TODO - failure scenarios here need more testing */
	if (pfh->compression == BODY_COMPRESSED_PKZIP) {

		char command[MAX_FILE_PATH_LEN];
		char output_folder[MAX_FILE_PATH_LEN];

		strlcpy(output_folder, get_data_folder(), MAX_FILE_PATH_LEN);
		strlcat(output_folder, "/", MAX_FILE_PATH_LEN);
		strlcat(output_folder, dest_folder, MAX_FILE_PATH_LEN);

		strlcpy(command, "unzip -o -d", MAX_FILE_PATH_LEN);
		strlcat(command, output_folder, MAX_FILE_PATH_LEN);
		strlcat(command, " ", MAX_FILE_PATH_LEN);
		strlcat(command, dest_filepath, MAX_FILE_PATH_LEN);
		debug_print("Uncomnpressing file: %s\n",command);
		// TODO System needs cancellation headers if this runs in seperate thread
		int shell_rc = system(command);
		if (shell_rc == -1) debug_print("Error: Unable to start shell for unzip command\n");
		if (shell_rc != 0) debug_print("Error: unzip returned %d\n",shell_rc);
	}

	return EXIT_SUCCESS;
}

int pfh_extract_file(HEADER *pfh, char *dest_folder) {
	return pfh_extract_file_and_update_keywords(pfh, dest_folder, false);
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
	unsigned char buffer[MAX_PFH_LENGTH]; // needs to be bigger than largest header but does not need to be the whole file
	int num = fread(buffer, sizeof(char), MAX_PFH_LENGTH, f);
	if (num == 0) {
		fclose(f);
		return NULL; // nothing was read
	}
	int size;
	int crc_passed;
	pfh = pfh_extract_header(buffer, num, &size, &crc_passed);
	//debug_print("Read: %d Header size: %d\n",num, size);

	if (!crc_passed) {
		//debug_print("CRC failed when loading PFH from file\n");
		free(pfh);
		return NULL;
	}

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
	debug_print("Crc:%04x ", pfh->headerCRC);
	debug_print("Size:%04x ", pfh->fileSize);

	char buf[30];
	time_t now = pfh->createTime;
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
	debug_print("Cr:%s ", buf);
	now = pfh->uploadTime;
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
	debug_print("Up:%s ", buf);
	now = pfh->expireTime;
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
	debug_print("Ex:%u %s ", pfh->expireTime, buf);
	debug_print(" Contains:%s\n", pfh->userFileName);
}

/**
 * pfh_save_pacsatfile()
 *
 * Create a Pacsat file based on the PFH byte stream specified with header and the file specified
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
		return EXIT_FAILURE; // we could not read from the infile
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
	p = pfh_store_str_field(p, FILE_NAME, 8, pfh->fileName);
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
	p = pfh_store_str_field(p, AX25_UPLOADER, 6, pfh->uploader);
	p = pfh_store_int_field(p, UPLOAD_TIME, pfh->uploadTime);
	p = pfh_store_char_field(p, DOWNLOAD_COUNT, pfh->downloadCount);
	p = pfh_store_str_field(p, DESTINATION, strlen(pfh->destination), pfh->destination);
	p = pfh_store_str_field(p, AX25_DOWNLOADER, 6, pfh->downloader);
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
	int i;
	for (i=0; i < PFH_NUM_OF_SPARE_FIELDS; i++) {
		if (pfh->other_data[i][0] != 0)
				p = pfh_store_str_field(p, pfh->other_id[i], strlen(pfh->other_data[i]), pfh->other_data[i]);
	}
	return p;
}

/**
 * pfh_make_internal_header()
 * Make a header for an internal file such as a log file or wod
 * Return a pointer to the header.  The caller is responsible for freeing
 *
 * the memory later.
 *
 * The id needs to be obtained from the dir before the header is created. This is then an
 * unused but allocated id until the file is added or discarded.  Similar to the upload
 * queue.
 *
 * An expire_time of 0 will use the default expire time for all files.
 *
 * Returns NULL if the header could not be created.
 *
 */
HEADER * pfh_make_internal_header(time_t now, uint8_t file_type, unsigned int id, char *filename,
		char *source, char *destination, char *title, char *user_filename, time_t update_time,
		int expire_time, char compression_type) {

	HEADER *pfh= pfh_new_header();
	if (pfh != NULL) {
		/* Required Header Information */
		pfh->fileId = id;
		strlcpy(pfh->fileName,filename, sizeof(pfh->fileName));
		strlcpy(pfh->fileExt,PSF_FILE_EXT, sizeof(pfh->fileExt));

		pfh->createTime = (uint32_t)update_time;
		pfh->modifiedTime = now;
		pfh->fileType = file_type;

		/* Extended Header Information */
		strlcpy(pfh->source,source, sizeof(pfh->source));

		strlcpy(pfh->destination,destination, sizeof(pfh->destination));
		if (expire_time != 0)
			pfh->expireTime = now + expire_time;

		/* Optional Header Information */
		strlcpy(pfh->title,title, sizeof(pfh->title));
		strlcpy(pfh->userFileName,user_filename, sizeof(pfh->userFileName));
		pfh->compression = compression_type;
	}
	return pfh;
}

/**
 * test_pfh_make_pacsat_file()
 *
 * Create a new PACSAT File based on a Header and the body file.  Save the new file
 * into out_filename which will be file_id.act and saved into dir_folder
 *
 * Returns: EXIT SUCCESS or EXIT_FAILURE
 */
int pfh_make_internal_file(HEADER *pfh, char *dir_folder, char *body_filename) {
	if (pfh == NULL) return EXIT_FAILURE;

	char out_filename[MAX_FILE_PATH_LEN];

	dir_get_file_path_from_file_id(pfh->fileId, dir_folder, out_filename, MAX_FILE_PATH_LEN);

	/* Measure body_size and calculate body_checksum */
	short int body_checksum = 0;
	unsigned int body_size = 0;

	FILE *infile = fopen(body_filename, "rb");
	if (infile == NULL) return EXIT_FAILURE;

	int ch=fgetc(infile);

	while (ch!=EOF) {
		body_checksum += ch & 0xff;
		body_size++;
		ch=fgetc(infile);
	}
	fclose(infile);
	pfh->bodyCRC = body_checksum;

	/* Build Pacsat File Header */
	unsigned char buffer[MAX_PFH_LENGTH];
	int len = pfh_generate_header_bytes(pfh, body_size, buffer);

	int rc = pfh_save_pacsatfile(buffer, len, out_filename, body_filename);

	return rc;
}



/*
 * SELF TESTS FOLLOW
 */

/**
 * test_pfh_make_pacsat_file()
 *
 * Create a new PACSAT File based on a Header and the body file specified by the file id.  Save the new file
 * into out_filename which will be file_id.act
 *
 * Returns: EXIT SUCCESS or EXIT_FAILURE
 */
int test_pfh_make_pacsat_file(HEADER *pfh, char *dir_folder) {
	if (pfh == NULL) return EXIT_FAILURE;

	char body_filename[MAX_FILE_PATH_LEN];
	char out_filename[MAX_FILE_PATH_LEN];
	pfh_get_user_filename(pfh, dir_folder, body_filename, MAX_FILE_PATH_LEN);
	//pfh_get_filename(pfh,dir_folder, out_filename, MAX_FILE_PATH_LEN);

	dir_get_file_path_from_file_id(pfh->fileId, dir_folder, out_filename, MAX_FILE_PATH_LEN);

	/* Measure body_size and calculate body_checksum */
	short int body_checksum = 0;
	unsigned int body_size = 0;

	FILE *infile = fopen(body_filename, "rb");
	if (infile == NULL) return EXIT_FAILURE;

	int ch=fgetc(infile);

	while (ch!=EOF) {
		body_checksum += ch & 0xff;
		body_size++;
		ch=fgetc(infile);
	}
	fclose(infile);
	pfh->bodyCRC = body_checksum;

	/* Build Pacsat File Header */
	unsigned char buffer[MAX_PFH_LENGTH];
	int len = pfh_generate_header_bytes(pfh, body_size, buffer);

	int rc = pfh_save_pacsatfile(buffer, len, out_filename, body_filename);

	return rc;
}

void pfh_populate_test_header(int id, HEADER *pfh, char *user_file_name) {
	if (pfh != NULL) {
		/* Required Header Information */
		pfh->fileId = id;
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

HEADER * make_test_header(unsigned int id, char *filename, char *source, char *destination, char *title, char *user_filename) {
	HEADER *pfh= pfh_new_header();
	if (pfh != NULL) {
		/* Required Header Information */
		pfh->fileId = id;
		strlcpy(pfh->fileName,filename, sizeof(pfh->fileName));
		strlcpy(pfh->fileExt,PSF_FILE_EXT, sizeof(pfh->fileExt));

		time_t now = time(0); // Get the system time in seconds since the epoch
		pfh->createTime = now;
		pfh->modifiedTime = now;
		pfh->SEUflag = 1;
		pfh->fileType = PFH_TYPE_ASCII;

		/* Extended Header Information */
		strlcpy(pfh->source,source, sizeof(pfh->source));

		pfh->uploadTime = 0;
		pfh->downloadCount = 0;
		strlcpy(pfh->destination,destination, sizeof(pfh->destination));
		pfh->downloadTime = 0;
		pfh->expireTime = now + 30*24*60*60;  // use for testing
		pfh->priority = 0;

		/* Optional Header Information */
		strlcpy(pfh->title,title, sizeof(pfh->title));
		strlcpy(pfh->userFileName,user_filename, sizeof(pfh->userFileName));
	}
	return pfh;
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
	printf("##### TEST PACSAT HEADER:\n");
	int rc = EXIT_SUCCESS;
	char *filename1 = "1234.act";
	char *userfilename1 = "file.txt";
	HEADER *pfh= pfh_new_header();
	pfh_populate_test_header(0x1234, pfh, userfilename1);
	pfh_debug_print(pfh);

	/* Make test files */
	char *msg = "Hi there,\nThis is a test message\n73 Chris\n";
	rc = write_test_msg(".", userfilename1, msg, strlen(msg));
	if (rc != EXIT_SUCCESS) { printf("** Failed to make file.txt file.\n"); return EXIT_FAILURE; }

	rc = test_pfh_make_pacsat_file(pfh, ".");
	if (rc != EXIT_SUCCESS) { printf("** Failed to make pacsat file.  Make sure there is a test file called %s\n",userfilename1); return EXIT_FAILURE; }
	pfh_debug_print(pfh);
	if (pfh->fileId != 0x1234) { 				printf("** Wrong fileId\n"); rc = EXIT_FAILURE; }

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
		printf("##### TEST PACSAT HEADER: success:\n");
	else
		printf("##### TEST PACSAT HEADER: fail:\n");
	return rc;
}

int test_pfh_checksum() {
	printf("##### TEST PACSAT HEADER CRC:\n");
	int rc = EXIT_SUCCESS;

	debug_print("Test PFH with checksum: Expected CRC: 282b\n");
	unsigned char big_header [] = {0xAA, 0x55, 0x01, 0x00, 0x04, 0x47, 0x03, 0x00, 0x00, 0x02, 0x00, 0x08, 0x35, 0x61, 0x62, 0x39, 0x38, 0x34, 0x62,
			0x30, 0x03, 0x00, 0x03, 0x20, 0x20, 0x20, 0x04, 0x00, 0x04, 0xDE, 0x3D, 0x01, 0x00, 0x05, 0x00, 0x04, 0x47, 0x7D, 0xB9, 0x5A,
			0x06, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x10, 0x09, 0x00, 0x02, 0x3E, 0x54,
			0x0A, 0x00, 0x02, 0x2B, 0x28,
			0x0B, 0x00, 0x02, 0xD8, 0x00, 0x10, 0x00, 0x05, 0x53, 0x54, 0x32, 0x4E, 0x48, 0x11, 0x00, 0x06, 0x53,
			0x54, 0x32, 0x4E, 0x48, 0x20, 0x12, 0x00, 0x04, 0x31, 0x85, 0xB9, 0x5A, 0x13, 0x00, 0x01, 0x00, 0x14, 0x00, 0x03, 0x41, 0x4C,
			0x4C, 0x15, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x16, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0xC7,
			0x71, 0xBD, 0x5A, 0x18, 0x00, 0x01, 0x00, 0x19, 0x00, 0x01, 0x00, 0x22, 0x00, 0x10, 0x4D, 0x59, 0x20, 0x53, 0x48, 0x41, 0x43,
			0x4B, 0x20, 0x41, 0x4E, 0x44, 0x20, 0x41, 0x4E, 0x54, 0x23, 0x00, 0x04, 0x3C, 0x57, 0x3E, 0x20, 0x26, 0x00, 0x11, 0x73, 0x74,
			0x32, 0x6E, 0x68, 0x20, 0x70, 0x69, 0x63, 0x20, 0x61, 0x6E, 0x74, 0x2E, 0x6A, 0x70, 0x67, 0x2A, 0x00, 0x07, 0x41, 0x57, 0x55,
			0x32, 0x2E, 0x31, 0x30, 0x2E, 0x00, 0x08, 0xAE, 0x47, 0xE1, 0x7A, 0x14, 0x2E, 0x2F, 0x40, 0x2F, 0x00, 0x08, 0xCD, 0xCC, 0xCC,
			0xCC, 0xCC, 0x4C, 0x40, 0xC0, 0x00, 0x00, 0x00};

	int size = 0;
	int crc_passed = false;
	HEADER *pfh = pfh_extract_header(big_header, sizeof(big_header), &size, &crc_passed);
	pfh_debug_print(pfh);
	if (crc_passed)
		debug_print("CRC PASSED\n");
	else {
		debug_print("CRC FAILED\n");
		return EXIT_FAILURE;
	}
	debug_print("Calculate PFH checksum: Expected CRC: 282b\n");
	unsigned char big_header_no_checksum [] = {0xAA, 0x55, 0x01, 0x00, 0x04, 0x47, 0x03, 0x00, 0x00, 0x02, 0x00, 0x08, 0x35, 0x61, 0x62, 0x39, 0x38, 0x34, 0x62,
				0x30, 0x03, 0x00, 0x03, 0x20, 0x20, 0x20, 0x04, 0x00, 0x04, 0xDE, 0x3D, 0x01, 0x00, 0x05, 0x00, 0x04, 0x47, 0x7D, 0xB9, 0x5A,
				0x06, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x10, 0x09, 0x00, 0x02, 0x3E, 0x54,
				0x0A, 0x00, 0x02, 0x00, 0x00,
				0x0B, 0x00, 0x02, 0xD8, 0x00, 0x10, 0x00, 0x05, 0x53, 0x54, 0x32, 0x4E, 0x48, 0x11, 0x00, 0x06, 0x53,
				0x54, 0x32, 0x4E, 0x48, 0x20, 0x12, 0x00, 0x04, 0x31, 0x85, 0xB9, 0x5A, 0x13, 0x00, 0x01, 0x00, 0x14, 0x00, 0x03, 0x41, 0x4C,
				0x4C, 0x15, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x16, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0xC7,
				0x71, 0xBD, 0x5A, 0x18, 0x00, 0x01, 0x00, 0x19, 0x00, 0x01, 0x00, 0x22, 0x00, 0x10, 0x4D, 0x59, 0x20, 0x53, 0x48, 0x41, 0x43,
				0x4B, 0x20, 0x41, 0x4E, 0x44, 0x20, 0x41, 0x4E, 0x54, 0x23, 0x00, 0x04, 0x3C, 0x57, 0x3E, 0x20, 0x26, 0x00, 0x11, 0x73, 0x74,
				0x32, 0x6E, 0x68, 0x20, 0x70, 0x69, 0x63, 0x20, 0x61, 0x6E, 0x74, 0x2E, 0x6A, 0x70, 0x67, 0x2A, 0x00, 0x07, 0x41, 0x57, 0x55,
				0x32, 0x2E, 0x31, 0x30, 0x2E, 0x00, 0x08, 0xAE, 0x47, 0xE1, 0x7A, 0x14, 0x2E, 0x2F, 0x40, 0x2F, 0x00, 0x08, 0xCD, 0xCC, 0xCC,
				0xCC, 0xCC, 0x4C, 0x40, 0xC0, 0x00, 0x00, 0x00};


	unsigned short int result = 0;
	for (int i=0; i< sizeof(big_header_no_checksum); i++)
		result += big_header_no_checksum[i] & 0xff;
	debug_print("CRC: %02x\n", result);

	if (result != 0x282b) { printf("** Mismatched CRC\n"); return EXIT_FAILURE; }

	if (rc == EXIT_SUCCESS)
			printf("##### TEST PACSAT HEADER CRC: success:\n");
		else
			printf("##### TEST PACSAT HEADER CRC: fail:\n");
		return rc;

}

int test_pacsat_header_disk_access() {
	printf("##### TEST PACSAT HEADER DISK ACCESS:\n");
	int rc = EXIT_SUCCESS;
	char *filename1 = "0999.act";
	char *userfilename1 = "999_file.txt";
	HEADER *pfh= pfh_new_header();
	pfh_populate_test_header(0x999, pfh, userfilename1);
	pfh_debug_print(pfh);

	/* Make test files */
	char *msg = "#!/bin/bash\necho This is a test script\n";
	rc = write_test_msg(".", userfilename1, msg, strlen(msg));
	if (rc != EXIT_SUCCESS) { printf("** Failed to make 999_file.txt file.\n"); return EXIT_FAILURE; }

	rc = test_pfh_make_pacsat_file(pfh, ".");
	if (rc != EXIT_SUCCESS) { printf("** Failed to make pacsat file.  Make sure there is a test file called %s\n",userfilename1); return EXIT_FAILURE; }
	if (pfh->fileId != 0x999) { 				printf("** Wrong fileId\n"); rc = EXIT_FAILURE; }

	HEADER * pfh3 = pfh_load_from_file(filename1);
	if (pfh3 == NULL) { printf("** Failed to load pacsat header\n"); return EXIT_FAILURE; }
	pfh_debug_print(pfh3);

	/* Now modify the keywords and add sstv_q1 dir */
	strlcpy(pfh3->keyWords, "SSTV", 33);
	if (pfh_update_pacsat_header(pfh3, ".") != EXIT_SUCCESS) { printf("** Failed to re-write header in file.\n"); return EXIT_FAILURE; }

	pfh_debug_print(pfh3);

	HEADER * pfh2 = pfh_load_from_file(filename1);
	if (pfh2 == NULL) { printf("** Failed to load pacsat header\n"); return EXIT_FAILURE; }
	pfh_debug_print(pfh2);
	/* Check the mandatory fields match */
	if (pfh->fileId != pfh2->fileId) { 				printf("** Mismatched fileId\n"); rc = EXIT_FAILURE; }
	if (strcmp(pfh->fileName,pfh2->fileName) != 0) {printf("** Mismatched fileName\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->fileExt,pfh2->fileExt) != 0) { 	printf("** Mismatched fileExt\n"); rc = EXIT_FAILURE;}
//	if (pfh->fileSize != pfh2->fileSize) { 			printf("** Mismatched fileSize expected %d got %d\n",pfh->fileSize, pfh2->fileSize); rc = EXIT_FAILURE;}
	if (pfh->createTime != pfh2->createTime) {		printf("** Mismatched createTime\n"); rc = EXIT_FAILURE;}
	if (pfh->modifiedTime != pfh2->modifiedTime) {	printf("** Mismatched modifiedTime\n"); rc = EXIT_FAILURE;}
	if (pfh->SEUflag != pfh2->SEUflag) {			printf("** Mismatched SEUflag\n"); rc = EXIT_FAILURE;}
	if (pfh->fileType != pfh2->fileType) {			printf("** Mismatched fileType\n"); rc = EXIT_FAILURE;}
	if (pfh->bodyCRC != pfh2->bodyCRC) {			printf("** Mismatched bodyCRC\n"); rc = EXIT_FAILURE;}
//	if (pfh->headerCRC != pfh2->headerCRC) {		printf("** Mismatched headerCRC\n"); rc = EXIT_FAILURE;}
//	if (pfh->bodyOffset != pfh2->bodyOffset) {		printf("** Mismatched bodyOffset\n"); rc = EXIT_FAILURE;}

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
	if (strcmp("SSTV",pfh2->keyWords) != 0) {printf("** Mismatched keyWords\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->file_description,pfh2->file_description) != 0) {printf("** Mismatched description\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->compressionDesc,pfh2->compressionDesc) != 0) {printf("** Mismatched compressionDesc\n"); rc = EXIT_FAILURE;}
	if (strcmp(pfh->userFileName,pfh2->userFileName) != 0) {printf("** Mismatched userFileName\n"); rc = EXIT_FAILURE;}

	free(pfh);
	free(pfh2);

	if (rc == EXIT_SUCCESS)
		printf("##### TEST PACSAT HEADER DISK ACCESS: success:\n");
	else
		printf("##### TEST PACSAT HEADER DISK ACCESS: fail:\n");
	return rc;
}
