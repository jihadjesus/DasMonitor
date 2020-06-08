DEFAULT: framebanger

bitbangtest: bitbangtest.c
	gcc -Wall -o bitbangtest bitbangtest.c -lwiringPi

bitbanger: bitbanger.c
	gcc -Wall -o bitbanger bitbanger.c -lwiringPi

framebanger: framebanger.c
	gcc -Wall -o framebanger framebanger.c -lwiringPi -lpthread -lcurl


run: bitbanger
	sudo ./bitbanger
