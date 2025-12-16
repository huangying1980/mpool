#huangying
CC := gcc
VPATH := ./
AR := ar
TARGET ?= release
MULTI_THREAD ?= false

ifeq ($(AR_FOR_SO), true)
CFLAGS_AR_FOR_SO := -fPIC
endif

ifeq ($(MULTI_THREAD),true)
SURFFIX := .mt
MT := -DMPOOL_MT
endif
RELEASE := -mtune=native -march=native -O3 -fomit-frame-pointer -fforce-addr -fivopts -ftree-vectorize -fweb -frename-registers -ftree-loop-linear -fno-bounds-check
CFLAGS := -Wall -Werror -pthread -Wextra -Warray-bounds -Wstringop-overflow -D_GNU_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 $(MT)

INC := -I./
LIB :=
MAJOR := 1
MINOR := 0
SMALL := 1
VERSION := -DMPOOL_MAJOR=$(MAJOR) -DMPOOL_MINOR=$(MINOR) -DMPOOL_SMALL=$(SMALL)
LDFLAGS := -Wl,--version-script=./mpool.ver
DEBUG :=
ifeq ($(TARGET),debug)
    DEBUG := -DMPOOL_ERROR_VERBOSE -DMPOOL_DEBUG_VERBOSE
    AR_TARGET := libmpool.a.dbg.$(MAJOR).$(MINOR).$(SMALL)$(SUFFIX)
    SO_TARGET := libmpool.so.dbg.$(MAJOR).$(MINOR).$(SMALL)$(SUFFIX)
else
    AR_TARGET := libmpool.a.$(MAJOR).$(MINOR).$(SMALL)$(SUFFIX)
    SO_TARGET := libmpool.so.$(MAJOR).$(MINOR).$(SMALL)$(SUFFIX)
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
	$(CC) -c $< -o $@ $(INC) $(CFLAGS) $(CFLAGS_AR_FOR_SO) $(VERSION) $(DEBUG) $(RELEASE)

$(AR_TARGET):$(AR_OBJ)
	$(AR) rcs $@ $^

install:
	ln -s $(SO_TARGET) libmpool.so
	ln -s $(AR_TARGET) libmpool.a

clean:
	rm -rf *.o $(SO_TARGET)
	rm -rf libmpool.a.* libmpool.so.*
	rm -rf libmpool.so
	rm -rf libmpool.a

debug:
	echo $(ALL_SRC)
	echo $(CFLAGS)
	echo $(INC)	
	echo $(LIB)
	echo $(SRC)
