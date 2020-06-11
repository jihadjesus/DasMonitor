CC=gcc
CFLAGS=-I.
DEPS = hellomake.h
OBJ = hellomake.o hellofunc.o 


DEFAULT: dasmonitor

bitbangtest: bitbangtest.c
	gcc -Wall -o bitbangtest bitbangtest.c -lwiringPi

bitbanger: bitbanger.c
	gcc -Wall -o bitbanger bitbanger.c -lwiringPi

logging.o: logging.c logging.h dasmonitor.h config.h secrets.h
	gcc -Wall -c -o logging.o logging.c -lpthread -lcurl

dasmonitor: dasmonitor.c dasmonitor.h config.h logging.h logging.o
	gcc -Wall -o dasmonitor dasmonitor.c logging.o -lwiringPi -lpthread -lcurl

xmit: 
	scp *.c Makefile *.h 192.168.1.7:DasMonitor

remotebuild: xmit
	ssh 192.168.1.7 "cd DasMonitor; make dasmonitor"

test: bitbanger
	sudo ./bitbanger
