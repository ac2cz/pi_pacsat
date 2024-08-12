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
char *get_data_folder();
char *get_dir_folder();
char *get_upload_folder();
char *get_wod_folder();
char *get_log_folder();
char *get_txt_folder();
int dir_next_file_number();
uint32_t dir_get_upload_time_now();
void dir_get_upload_file_path_from_file_id(int file_id, char *filename, int max_len);
void dir_get_file_path_from_file_id(int file_id, char *dir_name, char *filename, int max_len);
void dir_get_filename_from_file_id(uint32_t file_id, char *file_name, int max_len);
uint32_t dir_get_file_id_from_filename(char *file_name);
int dir_load();
int dir_validate_file(HEADER *pfh, char *filename);
void dir_free();
DIR_NODE * dir_add_pfh(HEADER * new_pfh, char *filename);
DIR_NODE * dir_get_pfh_by_date(DIR_DATE_PAIR pair, DIR_NODE *p);
DIR_NODE * dir_get_pfh_by_folder_id(char *folder, DIR_NODE *p);
DIR_NODE * dir_get_node_by_id(int file_id);
void dir_maintenance();
void dir_file_queue_check(time_t now, char * folder, uint8_t file_type, char * destination);

int test_pacsat_dir();
int test_pacsat_dir_one();
int make_big_test_dir();

#endif /* PACSAT_DIR_H_ */
