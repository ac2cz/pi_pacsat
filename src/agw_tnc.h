/*
 * agw_tnc.h
 *
 *  Created on: Sep 28, 2022
 *      Author: g0kla
 */

#ifndef AGW_TNC_H_
#define AGW_TNC_H_

struct t_agw_header {
  unsigned char portx;			/* 0 for first, 1 for second, etc. */
  unsigned char reserved1;
  unsigned char reserved2;
  unsigned char reserved3;
  unsigned char data_kind;
  unsigned char reserved4;
  unsigned char pid;
  unsigned char reserved5;
  char call_from[10];
  char call_to[10];
  int data_len;			/* Number of data bytes following. */
  int user_reserved;
};

struct t_agw_frame {
	struct t_agw_header header;
	char data[AX25_MAX_DATA_LEN];
};

struct t_agw_frame_ptr {
	struct t_agw_header *header;
	char *data;
};

#define MAX_RX_QUEUE_LEN 256

int tnc_connect(char *addr, int port);
int tnc_start_monitoring(char type);
int tnc_register_callsign(char *callsign);
int send_ui_packet(char *from_callsign, char *to_callsign, char *bytes, int len);
int tnc_receive_packet();
void print_header(struct t_agw_header *header);
void print_data(char *data, int len);

/* Listen to the TNC and store any received packets in a queue. */
void *tnc_listen_process(void * arg);
int get_next_frame(int frame_num, struct t_agw_frame_ptr *frame);

#endif /* AGW_TNC_H_ */
