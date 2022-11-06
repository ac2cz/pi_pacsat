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
#include <string.h>

/* program include files */
#include "config.h"
#include "debug.h"
#include "agw_tnc.h"
#include "ax25_tools.h"
#include "str_util.h"

int sockfd = 0;

struct sockaddr_in serv_addr;
int listen_thread_called = 0;

int next_frame_ptr = 0;
struct t_agw_frame receive_circular_buffer[MAX_RX_QUEUE_LEN]; // buffer received frames
int debug_tx_raw_frames = false;
int debug_rx_raw_frames = false;

/* Forward declarations*/


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

/**
 * Ask AGW to send connected data.  Must already have registered the from_callsign
 *
 * Returns EXIT_SUCCESS if successful otherwise EXIT_FAILURE
 */
int tnc_send_connected_data(char *from_callsign, char *to_callsign, int channel, unsigned char *bytes, int len) {
	struct t_agw_header header;
	memset (&header, 0, sizeof(header));
	header.data_kind = 'D'; // disconnect
	header.portx = channel; // channel
	strlcpy( header.call_from, from_callsign, sizeof(header.call_from) );
	strlcpy( header.call_to, to_callsign,sizeof(header.call_to)  );
	header.data_len = len;
	header.pid = 0xf0;

	if (debug_tx_raw_frames) {
		debug_print("SENDING: %s>%s: ", from_callsign, to_callsign);
		for (int i=0; i < header.data_len; i++) {
			debug_print("%02x ",bytes[i]);
		}
	}
	debug_print(" .. %d bytes\n", header.data_len);

if (g_run_self_test) return EXIT_SUCCESS; /* Dont transmit the bytes in test mode */

	int err = send(sockfd, (unsigned char*)(&header), sizeof(header), MSG_NOSIGNAL);
	if (err == -1) {
		printf ("Socket Send error with header, Terminating.\n");
		return EXIT_FAILURE;
	}
	err = send(sockfd, bytes, len, MSG_NOSIGNAL);
	if (err == -1) {
		printf ("Socket Send error with data, Terminating.\n");
		return EXIT_FAILURE;
	}
	float n = 8*(sizeof(header) + len ) / (float)g_bit_rate;
//	debug_print(" .. wait %f secs ..\n",n);
	usleep((int)(n * 1000000));

	return EXIT_SUCCESS;
 }
/**
 * Ask AGW to disconnect
 * Returns 0 if successful otherwise 1
 */
int tnc_diconnect(char *from_callsign, char *to_callsign, int channel) {
	struct t_agw_header header;
	memset (&header, 0, sizeof(header));
	header.data_kind = 'd'; // disconnect
	header.portx = channel; // channel
	strlcpy( header.call_from, from_callsign, sizeof(header.call_from) );
	strlcpy( header.call_to, to_callsign,sizeof(header.call_to)  );
	header.data_len = 0;
	header.pid = 0xf0;
	int err = 0;
	err = send(sockfd, (char*)(&header), sizeof(header), MSG_NOSIGNAL);
	if (err == -1) {
		printf ("Socket Send error\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/**
 * Ask AGW for the number of frames queued
 * Returns 0 if successful otherwise 1
 */
int tnc_frames_queued() {
	struct t_agw_header header;
	memset (&header, 0, sizeof(header));
	header.data_kind = 'y'; // register callsign
	int err = 0;
	err = send(sockfd, (char*)(&header), sizeof(header), MSG_NOSIGNAL);
	if (err == -1) {
		printf ("Socket Send error\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

//int send_V_packet(char *from_callsign, char *to_callsign, char pid, unsigned char *sendbytes, int len) {
//
//	char bytes[len+1];
//	bytes[0] = 0x00; // number of VIAs
//	for (int i=0; i < len; i++) {
//		bytes[i+1] = sendbytes[i];
////		if (isprint(bytes[i]))
////			printf("%c",bytes[i]);
////		else
////			printf(" ");
//	}
//
//	int err = send_raw_packet('V', from_callsign, to_callsign, pid, bytes, sizeof(bytes));
//
//	return err;
//}

/**
 * Send a UI packet()
 *
 * Note that Direwolf assumes the data is String data.  So the 0x00 bytes in the header terminate
 * the string.  This means that binary data is not transmitted.  Use the K packet for binary data.
 *
 * Direwolf also assumes the PID is F0.
 *
 */
int send_ui_packet(char *from_callsign, char *to_callsign, char pid, unsigned char *bytes, int len) {
	struct t_agw_header header;

	debug_print("SENDING: ");

	memset (&header, 0, sizeof(header));
	header.pid = pid;
	strlcpy( header.call_from, from_callsign, sizeof(header.call_from) );
	strlcpy( header.call_to, to_callsign,sizeof(header.call_to)  );

	header.data_kind = 'M';
	header.data_len = len;
	for (int i=0; i < header.data_len; i++) {
		if (isprint(bytes[i]))
			printf("%c",bytes[i]);
		else
			printf(" ");
	}
	if (debug_tx_raw_frames) {
		debug_print("|");
		for (int i=0; i < header.data_len; i++) {
			debug_print("%02x ",bytes[i]);
		}
	}
	debug_print(" .. %d bytes\n", header.data_len);

if (g_run_self_test) return EXIT_SUCCESS; /* Dont transmit the bytes in test mode */

	int err = send(sockfd, (unsigned char*)(&header), sizeof(header), MSG_NOSIGNAL);
	if (err == -1) {
		printf ("Socket Send error with header, Terminating.\n");
		return EXIT_FAILURE;
	}
	err = send(sockfd, bytes, len, MSG_NOSIGNAL);
	if (err == -1) {
		printf ("Socket Send error with data, Terminating.\n");
		return EXIT_FAILURE;
	}

	g_frames_queued++;
//	debug_print("~~~~M Sent :%d\n", g_frames_queued);
//	float n = 8*(sizeof(header) + len ) / (float)g_bit_rate;
//	debug_print(" .. wait %f secs ..\n",n);
////////////////	usleep((int)(n * 1000000));

	return EXIT_SUCCESS;
}

int send_raw_packet(char *from_callsign, char *to_callsign, char pid, unsigned char *bytes, int len) {
	struct t_agw_header header;

	debug_print("SENDING: ");

	memset (&header, 0, sizeof(header));
	header.pid = pid;
	strlcpy( header.call_from, from_callsign, sizeof(header.call_from) );
	strlcpy( header.call_to, to_callsign,sizeof(header.call_to)  );

	unsigned char raw_hdr[17];
	raw_hdr[0] = 0x00; /* Port settings */

	unsigned char buf[7];
	int l = encode_call(to_callsign, buf, false, 0);
	if (l != EXIT_SUCCESS) return EXIT_FAILURE;
	for (int i=0; i<7; i++)
		raw_hdr[i+1] = buf[i];
	l = encode_call(from_callsign, buf, true, 0);
	if (l != EXIT_SUCCESS) return EXIT_FAILURE;
	for (int i=0; i<7; i++)
		raw_hdr[i+8] = buf[i];
	raw_hdr[15] = 0x03; // UI Frame control byte
	raw_hdr[16] = pid;
	header.data_kind = 'K';

	unsigned char raw_bytes[sizeof(raw_hdr)+len];
	for (int i=0; i< sizeof(raw_hdr); i++) {
		raw_bytes[i] = raw_hdr[i];
	}
	for (int i=0; i< len; i++) {
		raw_bytes[i+sizeof(raw_hdr)] = bytes[i];
	}
	header.data_len = len+sizeof(raw_hdr);

	if (debug_tx_raw_frames) {
		for (int i=0; i< (len+sizeof(raw_hdr)); i++) {
			if (isprint(raw_bytes[i]))
				printf("%c",raw_bytes[i]);
			else
				printf(" ");
		}
		for (int i=0; i< (len+sizeof(raw_hdr)); i++) {
			printf("%02x ",raw_bytes[i]);
			if (i%40 == 0 && i!=0) printf("\n");
		}
	}

	debug_print(" .. %d bytes\n", header.data_len);

if (g_run_self_test) return EXIT_SUCCESS; /* Dont transmit the bytes in test mode */

	int err = send(sockfd, (unsigned char*)(&header), sizeof(header), MSG_NOSIGNAL);
	if (err == -1) {
		error_print ("Socket Send error with header, Terminating.\n");
		return EXIT_FAILURE;
	}
	err = send(sockfd, raw_bytes, len+sizeof(raw_hdr), MSG_NOSIGNAL);
	if (err == -1) {
		error_print ("Socket Send error with data, Terminating.\n");
		return EXIT_FAILURE;
	}

	/* this gets overridden when we read the y frame from the TNC, but we increment it because the
	 * y frame data lags.  This prevents us from sending too many frames before we know the status */
	g_frames_queued++;
//	debug_print("~~~~K Sent :%d\n", g_frames_queued);

//	float n = 8*(sizeof(header) + len+sizeof(raw_hdr) ) / (float)g_bit_rate;
//	debug_print(" .. wait %f secs ..\n",n);
//////////////	usleep((int)(n * 1000000));

	return EXIT_SUCCESS;
}

void *tnc_listen_process(void * arg) {
	char *name;
	name = (char *) arg;
	if (listen_thread_called) {
		error_print("Thread already started.  Exiting: %s\n", name);
	}
	listen_thread_called++;
	debug_print("Starting Thread: %s\n", name);

	while (1) {

		int err = tnc_receive_packet();
		if (err != 0) {
			printf("Data!!\n");
		}
	}

	debug_print("Exiting Thread: %s\n", name);
	listen_thread_called--;
}

void print_header(struct t_agw_header *header) {
	// print something
	debug_print ("Port [%d] Kind %c Pid %02X From:", header->portx, header->data_kind, header->pid & 0xff);
	for (int i=0; i < 10; i++) {
		if (isprint(header->call_from[i]))
			debug_print("%c",header->call_from[i]);
	}
	debug_print(" To:");
	for (int i=0; i < 10; i++) {
		if (isprint(header->call_to[i]))
			debug_print("%c",header->call_to[i]);
	}
	debug_print(" Len:%d ||",header->data_len);
}

void print_data(unsigned char *data, int len) {
	for (int i=0; i < len; i++) {
		if (isprint(data[i]))
			debug_print("%c",(char)data[i]);
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
		error_print ("Read error, received %d command bytes.\n", n);
		exit (EXIT_FAILURE);
	}

	/* If this asset fails then we are likely receiving frames that are longer than AX25_MAX_DATA_LEN */
	assert (header.data_len >= 0 && header.data_len < (int)(sizeof(receive_circular_buffer[next_frame_ptr].data)));

	if (header.data_kind == 'T') {
		//g_frames_queued--;
		//debug_print("~~~~T Confirmed :%d  ", g_frames_queued);
		//print_header(&header);
		//debug_print("\n");
	} else {
		if (debug_rx_raw_frames) {
			debug_print("RX :%d:", next_frame_ptr);
			print_header(&header);
		}
	}

	if (header.data_len > 0) {
		n = read (sockfd, receive_circular_buffer[next_frame_ptr].data, header.data_len);

		if (n != header.data_len) {
			error_print ("Read error, client received %d data bytes when %d expected.  Terminating.\n", n, header.data_len);
			exit (1);
		}
		if (debug_rx_raw_frames && header.data_kind != 'T')
			print_data(receive_circular_buffer[next_frame_ptr].data, header.data_len);
		if (debug_rx_raw_frames && header.data_kind != 'T')
			debug_print("\n");

		next_frame_ptr++;
		if (next_frame_ptr == MAX_RX_QUEUE_LEN)
			next_frame_ptr=0;
		return EXIT_SUCCESS;
	}
	return EXIT_SUCCESS;
}

/**
 * Return the frame if there is one available, which is true if the write pointer is
 * not equal to this number
 * TODO - this fails if we have wrapped all the way around the circular buffer. Need
 * to print a warning.  But then we are way behind and probably in trouble anyway.
 *
 */
int get_next_frame(int frame_num, struct t_agw_frame_ptr *frame) {
	if (next_frame_ptr != frame_num) {
		frame->header = &receive_circular_buffer[frame_num].header;
		frame->data = (unsigned char *)&receive_circular_buffer[frame_num].data;
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}
}

