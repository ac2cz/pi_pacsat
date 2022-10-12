/*
	crc.h

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

#define CRCLENGTH 2

int CheckCRC(unsigned char *buf, int length);
