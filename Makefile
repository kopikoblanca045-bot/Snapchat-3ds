# Minimal Makefile for FilterCam (devkitARM + libctru)
ifeq ($(strip $(DEVKITARM)),)
$(error Set DEVKITARM, e.g. export DEVKITARM=/opt/devkitpro/devkitARM)
endif
DEVKITPRO ?= /opt/devkitpro

CC     := $(DEVKITARM)/bin/arm-none-eabi-gcc
ARCH   := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) \
          -D__3DS__ -I$(DEVKITPRO)/libctru/include
LDFLAGS:= -specs=3dsx.specs -g $(ARCH) -L$(DEVKITPRO)/libctru/lib
LIBS   := -lctru -lm

all: camera.elf

camera.elf: source/main.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f camera.elf camera.cia banner.bnr icon.icn
