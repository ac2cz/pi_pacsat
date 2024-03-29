# Pacsat
This is an Open source implementation of the Pacsat protocol based on public documents.  Designed to run on a Linux computer using Direwolf as the TNC.  
For those interested I have a list of the Pacsat Protocol documents on this page: https://www.g0kla.com/pacsat/index.php

To build this, clone the repository then cd into the Debug folder

You can use:  make all to build everything, or make clean to remove all the compiled objects.

Follow the instructions to setup direwolf from https://github.com/wb2osz/direwolf/blob/master/doc/

Create a file called pacsat.config in the directory where you run this.  Here are some default contents:
```
# Changing the bitrate here does not change the rate of the TNC.  Also
# update the TNC configuration
bit_rate=1200
# Callsigns
bbs_callsign=XY2ZZ-12
broadcast_callsign=XY2ZZ-11
digi_callsign=XY2ZZ-1
# If a lot of frames are queued by the tnc then broadcast confirms can
# feel slugish.  Keep this value low.
max_frames_in_tx_buffer=2
# PACSAT Broadcast
pb_status_period_in_seconds=10
pb_max_period_for_client_in_seconds=36000
# UPLINK
uplink_status_period_in_seconds=15
uplink_max_period_for_client_in_seconds=36000
```

To run the program start direwolf in one terminal and then run Pacsat in another.  It should connect to the direwolf AGW engine to send and receive packets.

This supports broadcast requests, transmissions and file uploads.
