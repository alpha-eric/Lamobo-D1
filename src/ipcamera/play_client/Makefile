TARGETS = play_client                                                                                                                                                                                     
SRCS = play_client.cpp

PREFIX = /usr/local
LARGE_FILE = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_LARGE_FILE

CFLAGS    = $(PLATFORM_CFLAGS) -Wall -W
CXXFLAGS  = $(CFLAGS)
CPPFLAGS  = $(PLATFORM_CPPFLAGS) -D_REENTRANT -D_LINUX -I/usr/include -I$(PREFIX)/include $(LARGE_FILE)
LDFLAGS   = $(PLATFORM_LDFLAGS) -s
LOADLIBES = $(PLATFORM_LOADLIBES) -L/usr/lib -L$(PREFIX)/lib
LDLIBS    = $(PLATFORM_LDLIBS) -lpthread

CC        = g++
CPP       = gcc -E
CXX       = g++
AS        = as
AR        = ar
LD        = ld
NM        = nm
OBJCOPY   = objcopy
OBJDUMP   = objdump
RANLIB    = ranlib
READELF   = readelf
SIZE      = size
STRINGS   = strings
STRIP     = strip


.PHONY: all clean
all: $(TARGETS)
clean:
	rm -f *.o *.d *~
	rm -f $(TARGETS)

PREFIX_BINDIR := $(PREFIX)/bin
install: $(TARGETS)

%.d: %.c
	$(CC) -MM $(CPPFLAGS) $< | sed -re 's,^.*:,$*.o $*.d:,g' > $@
%.d: %.cpp
	$(CC) -MM $(CPPFLAGS) $< | sed -re 's,^.*:,$*.o $*.d:,g' > $@
DEPENDS := $(SRCS:%.c=%.d)
DEPENDS := $(DEPENDS:%.cpp=%.d)
ifneq ($(MAKECMDGOALS),clean)
-include $(DEPENDS)
endif

rectest: rectest.o Parameters.o RandomData.o Evaluator.o
