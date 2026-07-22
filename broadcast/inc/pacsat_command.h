/*
 * pacsat_command.h
 *
 *  Created on: May 31, 2024
 *      Author: g0kla
 */

#ifndef PACSAT_COMMAND_H_
#define PACSAT_COMMAND_H_

int load_signing_key(int key_number);
int pc_handle_command(char *from_callsign, unsigned char *data, int len);


#endif /* PACSAT_COMMAND_H_ */
