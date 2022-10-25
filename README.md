# Pacsat
This is an Open source implementation of the Pacsat protocol based on public documents.  Designed to run on a Linux computer using Direwolf as the TNC.  
For those interested I have a list of the Pacsat Protocol documents on this page: https://www.g0kla.com/pacsat/index.php

To build this, clone the repository then cd into the Debug folder

You can use:
make all

to build everything, or
make clean to remove all the compiled objects.

Follow the instructions to setup direwolf from https://github.com/wb2osz/direwolf/blob/master/doc/

To run the program start direwolf in one terminal and then run Pacsat in another.  It should connect to the direwolf AGW engine to send and receive packets.
