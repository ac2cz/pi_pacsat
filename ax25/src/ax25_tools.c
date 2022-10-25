/*
 * ax25_tools.c
 *
 *  Created on: Oct 7, 2022
 *      Author: g0kla
 */


/*
 *	Convert a call from the shifted ascii form used in an
 *	AX.25 packet.
 *
 *	based on code in pb.c by OZ6BL et al, which was released with the GNU GPL
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "debug.h"

int decode_call(unsigned char *c, char *call) {
	unsigned char *ep = c + 6;
	int ct = 0;

	while (ct < 6) {
		if (((*c >> 1) & 127) == ' ') break;

		*call = (*c >> 1) & 127;
		call++;
		ct++;
		c++;
	}

	if ((*ep & 0x1E) != 0) {
		*call = '-';
		call++;
		call += sprintf(call, "%d", (int)(((*ep) >> 1) & 0x0F));
	}

	*call = '\0';

	if (*ep & 1) return 0;

	return 1;
}

/**
 * Convert a callsign to AX25 format
 */
int encode_call(char *name, unsigned char *buf, int final_call, char command) {
	int ct   = 0;
	int ssid = 0;
	const char *p = name;
	char c;

	while (ct < 6) {
		c = toupper(*p);

		if (c == '-' || c == '\0')
			break;

		if (!isalnum(c)) {
			error_print("axutils: invalid symbol in callsign '%s'\n", name);
			return EXIT_FAILURE;
		}

		buf[ct] = c << 1;

		p++;
		ct++;
	}

	while (ct < 6) {
		buf[ct] = ' ' << 1;
		ct++;
	}

	if (*p != '\0') {
		p++;

		if (sscanf(p, "%d", &ssid) != 1 || ssid < 0 || ssid > 15) {
			error_print("axutils: SSID must follow '-' and be numeric in the range 0-15 - '%s'\n", name);
			return EXIT_FAILURE;
		}
	}

	buf[6] = ((ssid + '0') << 1) & 0x1E;
	command = (command & 0b1) << 7;
	buf[6] = buf[6] | command;
	if (final_call)
		buf[6] = buf[6] | 0x01;
	return EXIT_SUCCESS;
}
