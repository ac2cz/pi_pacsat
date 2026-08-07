/*
 * pacsat_command.h
 *
 *  Created on: May 31, 2024
 *      Author: g0kla
 */

#ifndef PACSAT_COMMAND_H_
#define PACSAT_COMMAND_H_

#include "pacsat_dir.h"

int pc_load_signing_key(int key_number);
int pc_handle_command(char *from_callsign, unsigned char *data, int len);
int pc_install_file(DIR_NODE *node, char *folder);

#endif /* PACSAT_COMMAND_H_ */
