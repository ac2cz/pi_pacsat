/*
 * fstelemetry.h
 *
 *  Created on: Jul 11, 2026
 *      Author: g0kla
 */

#ifndef FSTELEMETRY_H_
#define FSTELEMETRY_H_



typedef struct __attribute__((__packed__)) {
    unsigned int timestamp : 32;
    unsigned int NumOfFiles : 32;
    unsigned int TelemPeriod : 16;
    unsigned int BytesQueued : 16;
    unsigned int DirMaintPeriod : 16;
    unsigned int FileQueueCheckPeriod : 16;
    unsigned int FTL0MaintPeriod : 16;
    unsigned int PBStatusPeriod : 16;
    unsigned int PBTimeout : 16;
    unsigned int UplinkStatusPeriod : 16;
    unsigned int UplinkTimeout : 16;
    unsigned int MaxFileAgeDays : 16;
    unsigned int FTL0MaxFileSizeKb : 16;
    unsigned int FTL0MaxUploadAgeMin : 16;
    unsigned int LogLevel : 8;
    unsigned int PBEnabled : 1;
    unsigned int UplinkEnabled : 2;
    unsigned int pad1 : 5;

} fstelemetry_t;
#endif /* FSTELEMETRY_H_ */
