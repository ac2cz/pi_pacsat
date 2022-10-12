/*
 * agw_tnc.c
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
 * Interface with the AGW TNC interface of Direwolf.
 *
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <assert.h>
#include <str_util.h>
#include <string.h>

/* program include files */
#include "config.h"
#include "debug.h"
#include "agw_tnc.h"



int sockfd = 0;

struct sockaddr_in serv_addr;
int listen_thread_called = 0;

int next_frame_ptr = 0;
struct t_agw_frame receive_circular_buffer[MAX_RX_QUEUE_LEN]; // buffer received frames
int debug_raw_frames = false;

/* Forward declarations*/
int send_raw_packet(char datakind, char *from_callsign, char *to_callsign, char pid, char *bytes, int len);

/**
 * Connect to the AGW TNC socket using the passed address and port
 * Returns 0 if successful otherwise 1
 */
int tnc_connect(char *addr, int port) {
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("\n Error : Could not create socket \n");
		return EXIT_FAILURE;
	}

	memset(&serv_addr, '0', sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	/* Convert INET address to binary format */
	if(inet_pton(AF_INET, addr, &serv_addr.sin_addr)<=0) {
		printf("\n inet_pton error occured\n");
		return EXIT_FAILURE;
	}

	if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("\n Error : Connect Failed \n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/**
 * Send an AGW frame to start monitoring the output of the TNC.
 *
 * Returns 0 if successful otherwise 1
 */
int tnc_start_monitoring(char type) {
	struct t_agw_header header;
	memset (&header, 0, sizeof(header));
	header.data_kind = type; // toggle monitoring of RAW UI frames
	int err = 0;
	err = send(sockfd, (char*)(&header), sizeof(header), MSG_NOSIGNAL);
	if (err == -1) {
		printf ("Socket Send error\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/**
 * Registers the callsign of this station with Direwolf using an AGW X type frame
 * Returns 0 if successful otherwise 1
 */
int tnc_register_callsign(char *callsign) {
	struct t_agw_header header;
	memset (&header, 0, sizeof(header));
	header.data_kind = 'X'; // register callsign
	strlcpy( header.call_from, callsign, sizeof(header.call_from) );
	int err = 0;
	err = send(sockfd, (char*)(&header), sizeof(header), MSG_NOSIGNAL);
	if (err == -1) {
		printf ("Socket Send error\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int send_ui_packet(char *from_callsign, char *to_callsign, char pid, char *sendbytes, int len) {

	char bytes[len+1];
	bytes[0] = 0x00; // number of VIAs
	for (int i=0; i < len; i++) {
		bytes[i+1] = sendbytes[i];
//		if (isprint(bytes[i]))
//			printf("%c",bytes[i]);
//		else
//			printf(" ");
	}

//	int err = send_raw_packet('V', from_callsign, to_callsign, bytes, sizeof(bytes));
	int err = send_raw_packet('M', from_callsign, to_callsign, pid, bytes, sizeof(bytes));

	return err;
}

int send_raw_packet(char datakind, char *from_callsign, char *to_callsign, char pid, char *bytes, int len) {
	struct t_agw_header header;

	printf("SENDING: ");

	memset (&header, 0, sizeof(header));
	header.pid = pid;
	strlcpy( header.call_from, from_callsign, sizeof(header.call_from) );
	strlcpy( header.call_to, to_callsign,sizeof(header.call_to)  );

	header.data_kind = datakind; //'K';  // transmit a RAW frame
	header.data_len = len;
	for (int i=0; i < header.data_len; i++) {
		if (isprint(bytes[i]))
			printf("%c",bytes[i]);
		else
			printf(" ");
	}
	printf(" .. %d bytes\n", header.data_len);
	int err = send(sockfd, (char*)(&header), sizeof(header), MSG_NOSIGNAL);
	if (err == -1) {
		printf ("Socket Send error with header, Terminating.\n");
		return EXIT_FAILURE;
	}
	err = send(sockfd, bytes, len, MSG_NOSIGNAL);
	if (err == -1) {
		printf ("Socket Send error with data, Terminating.\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

void *tnc_listen_process(void * arg) {
	char *name;
	name = (char *) arg;
	if (listen_thread_called) {
		error_print("Thread already started.  Exiting: %s\n", name);
	}
	listen_thread_called++;
	printf("Starting Thread: %s\n", name);

	while (1) {

		int err = tnc_receive_packet();
		if (err != 0) {
			printf("Data!!\n");
		}
	}

	printf("Exiting Thread: %s\n", name);
	listen_thread_called--;
	//free(telem_packet);
	//return EXIT_SUCCESS;
}

void print_header(struct t_agw_header *header) {
	// print something
	debug_print ("Port [%d] Kind %c Pid %02X From:", header->portx, header->data_kind, header->pid & 0xff);
	for (int i=0; i < 10; i++) {
		if (isprint(header->call_from[i]))
			printf("%c",header->call_from[i]);
	}
	debug_print(" To:");
	for (int i=0; i < 10; i++) {
		if (isprint(header->call_to[i]))
			printf("%c",header->call_to[i]);
	}
	debug_print(" Len:%d ||",header->data_len);
}

void print_data(char *data, int len) {
	for (int i=0; i < len; i++) {
		if (isprint(data[i]))
			debug_print("%c",data[i]);
		else
			debug_print(" ");
	}
	debug_print(" : ");
	for (int i=0; i < len; i++) {
		debug_print("%02x ",data[i] & 0xff);
	}
}

int tnc_receive_packet() {
	struct t_agw_header header;
	int n = read(sockfd, (char*)(&receive_circular_buffer[next_frame_ptr].header), sizeof(header));

	header = receive_circular_buffer[next_frame_ptr].header;

	if (n != sizeof(header)) {
		printf ("Read error, received %d command bytes.\n", n);
		exit (1);
	}

	assert (header.data_len >= 0 && header.data_len < (int)(sizeof(receive_circular_buffer[next_frame_ptr].data)));

	if (debug_raw_frames) {
		debug_print("RX :%d:", next_frame_ptr);
		print_header(&header);
	}

	if (header.data_len > 0) {
		n = read (sockfd, receive_circular_buffer[next_frame_ptr].data, header.data_len);

		if (n != header.data_len) {
			printf ("Read error, client received %d data bytes when %d expected.  Terminating.\n", n, header.data_len);
			exit (1);
		}
		if (debug_raw_frames)
			print_data(receive_circular_buffer[next_frame_ptr].data, header.data_len);

		if (debug_raw_frames)
			debug_print("\n");

		next_frame_ptr++;
		if (next_frame_ptr == MAX_RX_QUEUE_LEN)
			next_frame_ptr=0;
		return EXIT_SUCCESS;
	}
	return 0;
}

/**
 * Return the frame if there is one available, which is true if the write pointer is
 * not equal to this number
 * TODO - this fails if we have wrapped all the way around the circular buffer.  But then
 * we are way behind and probably in trouble anyway
 */
int get_next_frame(int frame_num, struct t_agw_frame_ptr *frame) {
	if (next_frame_ptr != frame_num) {
		frame->header = &receive_circular_buffer[frame_num].header;
		frame->data = &receive_circular_buffer[frame_num].data;
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}
}

