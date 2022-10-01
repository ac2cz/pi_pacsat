/*
 * debug.h
 *
 *  Created on: Mar 6, 2022
 *      Author: g0kla
 *
 *  Copyright (C) 2022
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
 */

#ifndef DEBUG_H_
#define DEBUG_H_

#include <stdarg.h>
#include <stdio.h>

#include "config.h"

/* -- Macro Definitions
 * The do while(0) structure ensures that this code looks like a function and the
 * compiler will always check the code is valid.  But when DEBUG is
 * 0 the optimizer will remove the code
 *
 * The ##__VA_ARGS_ is supported by gcc and allows a list or not.
 * See: https://stackoverflow.com/questions/1644868/define-macro-for-debug-printing-in-c
 *
 * */
#define debug_print(fmt, ...) \
            do { if (DEBUG) fprintf(stdout, fmt, ##__VA_ARGS__); } while (0)

#define verbose_print(fmt, ...) \
            if (g_verbose) fprintf(stdout, fmt, ##__VA_ARGS__);

#define error_print(fmt, ...) \
            fprintf(stderr, "ERROR: %s:%d:%s(): " fmt, __FILE__, \
                                __LINE__, __func__, ##__VA_ARGS__);

#endif /* DEBUG_H_ */
