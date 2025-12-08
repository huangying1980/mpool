#huangying
CC := gcc
VPATH := ./
AR := ar
TARGET ?= release

RELEASE := -mtune=native -march=native -O3 -fomit-frame-pointer -fforce-addr -fivopts -ftree-vectorize -fweb -frename-registers -ftree-loop-linear -fno-bounds-check
CFLAGS := -Wall -Werror -pthread -Wextra -Warray-bounds -Wstringop-overflow -D_GNU_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
INC := -I./
LIB :=
MAJOR := 1
MINOR := 0
SMALL := 0
VERSION := -DMPOOL_MAJOR=$(MAJOR) -DMPOOL_MINOR=$(MINOR) -DMPOOL_SMALL=$(SMALL)
LDFLAGS := -Wl,--version-script=./mpool.ver
DEBUG :=
ifeq ($(TARGET),debug)
    DEBUG := -DMPOOL_ERROR_VERBOSE -DMPOOL_DEBUG_VERBOSE
    AR_TARGET := libmpool.a.dbg.$(MAJOR).$(MINOR).$(SMALL)
    SO_TARGET := libmpool.so.dbg.$(MAJOR).$(MINOR).$(SMALL)
else
    AR_TARGET := libmpool.a.$(MAJOR).$(MINOR).$(SMALL)
    SO_TARGET := libmpool.so.$(MAJOR).$(MINOR).$(SMALL)
endif

ALL_SRC := $(wildcard *.c)
SRC := $(ALL_SRC)

AR_OBJ := $(SRC:%.c=%.a.o)
SO_OBJ := $(SRC:%.c=%.so.o)

.PHONY: all clean install debug


all: $(AR_TARGET) $(SO_TARGET) install

	
$(SO_OBJ): %.so.o:%.c
	$(CC) -c $< -o $@ $(INC) $(CFLAGS) -fPIC $(VERSION) $(DEBUG) $(RELEASE)

$(SO_TARGET): $(SO_OBJ)
	$(CC) $^ -o $@ $(LIB) $(LDFLAGS) -shared $(DEBUG) $(RELEASE)

$(AR_OBJ): %.a.o:%.c
	$(CC) -c $< -o $@ $(INC) $(CFLAGS) $(VERSION) $(DEBUG) $(RELEASE)

$(AR_TARGET):$(AR_OBJ)
	$(AR) rcs $@ $^

install:
	ln -s $(SO_TARGET) libmpool.so

clean:
	rm -rf *.o $(SO_TARGET)
	rm -rf libmpool.a.* libmpool.so.*
	rm -rf libmpool.so

debug:
	echo $(ALL_SRC)
	echo $(CFLAGS)
	echo $(INC)	
	echo $(LIB)
	echo $(SRC)
