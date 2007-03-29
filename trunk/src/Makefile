obj-m += rbd.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

sd: sdops.c sd.c
	gcc -g -o sd sdops.c sd.c

sdfile: sdops.c sdfile.c
	gcc -g -o sdfile sdops.c sdfile.c

sdtest: sdops.c sdtest.c
	gcc -g -o sdtest sdops.c sdtest.c
