
CC       := $(CROSS_HOST)gcc
PKGCONF  := $(CROSS_HOST)pkg-config
CFLAGS   := -g -Wall -pthread -O3 $(EXTRA_CFLAGS)
LDFLAGS  := -g -lm -pthread $(EXTRA_LDFLAGS)
OBJS     := sdr.o sdr_file.o sdr_rtlsdr.o apollo-tv.o
PKGS     := sdl2 librtlsdr $(EXTRA_PKGS)

CFLAGS  += $(shell $(PKGCONF) --cflags $(PKGS))
LDFLAGS += $(shell $(PKGCONF) $(EXTRA_PKGFLAGS) --libs $(PKGS))

all: apollo-tv

apollo-tv: $(OBJS)
	$(CC) -o apollo-tv $(OBJS) $(LDFLAGS)

%.o: %.c Makefile
	$(CC) $(CFLAGS) -c $< -o $@
	@$(CC) $(CFLAGS) -MM $< -o $(@:.o=.d)

install:
	cp -f apollo-tv /usr/local/bin/

clean:
	rm -f *.o *.d apollo-tv apollo-tv.exe

-include $(OBJS:.o=.d)

