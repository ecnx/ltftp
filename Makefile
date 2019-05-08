# Little Tftp Makefile
INCLUDES=-I include
INDENT_FLAGS=-br -ce -i4 -bl -bli0 -bls -c4 -cdw -ci4 -cs -nbfda -l100 -lp -prs -nlp -nut -nbfde -npsl -nss
CC=gcc
LD=gcc
CFLAGS=-c -Wall -Wextra -O2 -ffunction-sections -fdata-sections
LDFLAGS=-s -Wl,--gc-sections -Wl,--relax

SERVER_OBJS = \
	release/server.o \
	release/util.o

CLIENT_OBJS = \
	release/client.o \
	release/util.o

all: server client

prepare:
	@mkdir -p release

util:
	@echo "  CC    src/util.c"
	@$(CC) $(CFLAGS) $(INCLUDES) src/util.c -o release/util.o

server: prepare util
	@echo "  CC    src/server.c"
	@$(CC) $(CFLAGS) $(INCLUDES) src/server.c -o release/server.o
	@echo "  LD    release/tftpd"
	@$(LD) -o release/tftpd $(SERVER_OBJS) $(LDFLAGS)

client: prepare util
	@echo "  CC    src/client.c"
	@$(CC) $(CFLAGS) $(INCLUDES) src/client.c -o release/client.o
	@echo "  LD    release/tftp"
	@$(LD) -o release/tftp $(CLIENT_OBJS) $(LDFLAGS)

internal: client server

host:
	@make internal \
		CC=gcc \
		LD=gcc \
		CFLAGS='-c -Wall -Wextra -Os -ffunction-sections -fdata-sections' \
		LDFLAGS='-s -Wl,--gc-sections -Wl,--relax'

install:
	@cp -v release/tftpd /usr/bin/tftpd
	@cp -v release/tftp /usr/bin/tftp

uninstall:
	@rm -fv /usr/bin/tftpd
	@rm -fv /usr/bin/tftp

indent:
	@indent $(INDENT_FLAGS) ./*/*.h
	@indent $(INDENT_FLAGS) ./*/*.c
	@rm -rf ./*/*~

clean:
	@echo "  CLEAN ."
	@rm -rf release

analysis:
	@scan-build make
	@cppcheck --force */*.h
	@cppcheck --force */*.c
