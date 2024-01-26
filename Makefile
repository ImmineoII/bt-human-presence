
CC?=$(CROSS_COMPILE)"gcc"

CFLAGS += $(shell cat .config | grep CONFIG | sed 's/^/-D/' | sed 's/"/\\"/g' | tr '\n' ' ')

default: bt-presence;

all: bt-presence;

clean:
	rm bt-presence &>/dev/null

bt-presence: 
	$(CC) ${CFLAGS} ${INCLUDES} bt-presence.c -o bt-presence ${LIBS} ${LDFLAGS} -lbluetooth -lmosquitto

menuconfig:
	kconfig-conf Kconfig
