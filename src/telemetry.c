/*
 * telemetry.c
 *
 *  Created on: Jul 11, 2026
 *      Author: g0kla
 */

#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "state_file.h"
#include "common_config.h"
#include "agw_tnc.h"
#include "pacsat_broadcast.h"
#include "fstelemetry.h"
#include "ftl0.h"


/* Variables */
static fstelemetry_t fstelemetry;

int send_telemetry(time_t now) {

	fstelemetry.timestamp = (uint32_t) time(0);
	if (fstelemetry.timestamp < CLOCK_2024_01_01) {
		/* The clock is not set or is corrupt */
		return EXIT_FAILURE;
	}
	fstelemetry.NumOfFiles = g_dir_next_file_number;
	fstelemetry.TelemPeriod = g_telem_send_period_in_seconds;

	fstelemetry.DirMaintPeriod = g_dir_maintenance_period_in_seconds;
	fstelemetry.FileQueueCheckPeriod = g_file_queue_check_period_in_seconds;
	fstelemetry.FTL0MaintPeriod = g_ftl0_maintenance_period_in_seconds;

	fstelemetry.PBEnabled = g_state_pb_open;
    fstelemetry.PBStatusPeriod = g_pb_status_period_in_seconds;
    fstelemetry.PBTimeout = g_pb_max_period_for_client_in_seconds;

	fstelemetry.UplinkEnabled = g_state_uplink_open;
    fstelemetry.UplinkStatusPeriod = g_uplink_status_period_in_seconds;
    fstelemetry.UplinkTimeout = g_uplink_max_period_for_client_in_seconds;
    fstelemetry.BytesQueued = ftl0_get_space_reserved_by_upload_table();

	//debug_print("Sending FS Telem\n");
	int rc = send_raw_packet(g_broadcast_callsign, TELEM_CALLSIGN, PID_NO_PROTOCOL, (unsigned char *)&fstelemetry, sizeof(fstelemetry));

	if (rc != EXIT_SUCCESS) {
		return rc;
	}
	return EXIT_SUCCESS;

}
