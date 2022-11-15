/*
 * pacsat_dir.h
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
 *
 */

#ifndef PACSAT_DIR_H_
#define PACSAT_DIR_H_

#include "pacsat_header.h"
#include "pacsat_broadcast.h"

/*
 * We implement the directory as a doubly linked list.  Most operations involve traversing the list
 * in order to retrieve dir fills. When we add items they are near the end of the list (updated
 * files), so we search backwards to find the insertion point.
 *
 * Each dir_node stores the full packsat header together with the list pointers
 */
struct dir_node {
	HEADER * pfh;
	struct dir_node *next;
	struct dir_node *prev;
};
typedef struct dir_node DIR_NODE;

int dir_init(char *folder);
char *get_dir_folder();
int dir_next_file_number();
int dir_load();
int dir_validate_file(HEADER *pfh, char *filename);
void dir_free();
DIR_NODE * dir_add_pfh(HEADER * new_pfh, char *filename);
DIR_NODE * dir_get_pfh_by_date(DIR_DATE_PAIR pair, DIR_NODE *p );
DIR_NODE * dir_get_node_by_id(int file_id);
int test_pacsat_dir();
int test_pacsat_dir_one();
int make_big_test_dir();

#endif /* PACSAT_DIR_H_ */
