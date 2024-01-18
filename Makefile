CC?=$(CROSS_COMPILE)"gcc"

default: bt-presence;

all: bt-presence;

clean:
	rm bt-presence &>/dev/null

bt-presence: 
	$(CC) ${CFLAGS} ${INCLUDES} bt-presence.c -o bt-presence ${LIBS} ${LDFLAGS} -lbluetooth