/*
 * ftl0.h
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
 */

#ifndef FTL0_H_
#define FTL0_H_

void ftl0_connection_received(char *from_callsign, char *to_callsign, int incomming, char * data);
void ftl0_process_frame(char *from_callsign, char *to_callsign, char *data, int len);

#endif /* FTL0_H_ */
