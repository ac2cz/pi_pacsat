/*
 * crc.c
 *
 *  Created on: Oct 2, 2022
 *      Author: g0kla
 */


/*
	crc.c

	CCITT CRC routines

	John Melton
	G0ORX, N6LYT

	4 Charlwoods Close
	Copthorne
	West Sussex
	RH10 3QZ
	England

	INTERNET:	g0orx@amsat.org
			n6lyt@amsat.org
			john@images.demon.co.uk
			J.D.Melton@slh0613.icl.wins.co.uk

*/


short gen_crc(unsigned char *buf, int length) {
	short crc = 0;
	int y, i;

	for (i = 0; i < length; i++) {
		//printf("c:%02x ",buf[i]);
		//if (i%8 == 0 && i!=0) printf("\n");
		crc ^= buf[i] << 8;

		for (y = 0; y < 8; y++)
		{
			if (crc & 0x8000)
				crc = crc << 1 ^ 0x1021;
			else
				crc <<= 1;
		}
	}
	return crc;
}

int check_crc(unsigned char *buf, int length)
{
	short crc = gen_crc(buf, length);

	if (crc != 0)
		return 0;
	else
		return 1;
}
