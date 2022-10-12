/*
 * util.h
 *
 *  Created on: Sep 28, 2022
 *      Author: g0kla
 */

#ifndef STR_UTIL_H_
#define STR_UTIL_H_

size_t strlcpy(char *dst, const char *src, size_t dsize);
size_t strlcat(char *dst, const char *src, size_t dsize);
int str_ends_with(const char *str, const char *suffix);
#endif /* STR_UTIL_H_ */
