DEFAULT: framebanger

bitbangtest: bitbangtest.c
	gcc -Wall -o bitbangtest bitbangtest.c -lwiringPi

bitbanger: bitbanger.c
	gcc -Wall -o bitbanger bitbanger.c -lwiringPi

framebanger: framebanger.c
	gcc -Wall -o framebanger framebanger.c -lwiringPi -lpthread -lcurl

xmit: 
	scp *.c Makefile *.h 192.168.1.7:DasMonitor

remotebuild: xmit
	ssh 192.168.1.7 "cd DasMonitor; make framebanger"

run: bitbanger
	sudo ./bitbanger
