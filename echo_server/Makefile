#
# Copyright 2022, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(BUILD_DIR)),)
$(error BUILD_DIR must be specified)
endif

ifeq ($(strip $(SEL4CP_SDK)),)
$(error SEL4CP_SDK must be specified)
endif

ifeq ($(strip $(SEL4CP_BOARD)),)
$(error SEL4CP_BOARD must be specified)
endif

ifeq ($(strip $(SEL4CP_CONFIG)),)
$(error SEL4CP_CONFIG must be specified)
endif

TOOLCHAIN := aarch64-linux-gnu

CPU := cortex-a53

CC := $(TOOLCHAIN)-gcc
LD := $(TOOLCHAIN)-ld
AS := $(TOOLCHAIN)-as
SEL4CP_TOOL ?= $(SEL4CP_SDK)/bin/sel4cp

LWIPDIR=lwip/src
BENCHDIR=benchmark
RINGBUFFERDIR=libsharedringbuffer
MICROPYDIR=micropython/ports/imx8mm-sel4cp
LIBNFSDIR=libnfs
MUSLDIR=musl/build
SEL4CPRUNTIME=sel4cp_runtime

BOARD_DIR := $(SEL4CP_SDK)/board/$(SEL4CP_BOARD)/$(SEL4CP_CONFIG)

IMAGES := eth.elf lwip.elf benchmark.elf idle.elf websrv.elf nfs.elf
CFLAGS := -mcpu=$(CPU) -mstrict-align -ffreestanding -g3 -O3 -Wall  -Wno-unused-function
LDFLAGS := -L$(MUSLDIR)/lib -L$(SEL4CPRUNTIME)/lib -L$(BOARD_DIR)/lib -L/host/extras/gcc-arm-10.2-2020.11-x86_64-aarch64-none-elf/lib/gcc/aarch64-none-elf/10.2.1
LIBS := -lsel4cp -Tsel4cp.ld -lc -lgcc

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt

CFLAGS += -I$(BOARD_DIR)/include \
	-Iinclude	\
	-Iinclude/arch	\
	-I$(LWIPDIR)/include \
	-I$(LWIPDIR)/include/ipv4 \
	-I$(RINGBUFFERDIR)/include \
	-I$(BENCHDIR)/include \
	-I$(MICROPYDIR)/include \
	-I$(MUSLDIR)/include \
	-I$(LIBNFSDIR)/include \
	-I$(SEL4CPRUNTIME)/include \

# COREFILES, CORE4FILES: The minimum set of files needed for lwIP.
COREFILES=$(LWIPDIR)/core/init.c \
	$(LWIPDIR)/core/def.c \
	$(LWIPDIR)/core/dns.c \
	$(LWIPDIR)/core/inet_chksum.c \
	$(LWIPDIR)/core/ip.c \
	$(LWIPDIR)/core/mem.c \
	$(LWIPDIR)/core/memp.c \
	$(LWIPDIR)/core/netif.c \
	$(LWIPDIR)/core/pbuf.c \
	$(LWIPDIR)/core/raw.c \
	$(LWIPDIR)/core/stats.c \
	$(LWIPDIR)/core/sys.c \
	$(LWIPDIR)/core/altcp.c \
	$(LWIPDIR)/core/altcp_alloc.c \
	$(LWIPDIR)/core/altcp_tcp.c \
	$(LWIPDIR)/core/tcp.c \
	$(LWIPDIR)/core/tcp_in.c \
	$(LWIPDIR)/core/tcp_out.c \
	$(LWIPDIR)/core/timeouts.c \
	$(LWIPDIR)/core/udp.c

CORE4FILES=$(LWIPDIR)/core/ipv4/autoip.c \
	$(LWIPDIR)/core/ipv4/dhcp.c \
	$(LWIPDIR)/core/ipv4/etharp.c \
	$(LWIPDIR)/core/ipv4/icmp.c \
	$(LWIPDIR)/core/ipv4/igmp.c \
	$(LWIPDIR)/core/ipv4/ip4_frag.c \
	$(LWIPDIR)/core/ipv4/ip4.c \
	$(LWIPDIR)/core/ipv4/ip4_addr.c

# NETIFFILES: Files implementing various generic network interface functions
NETIFFILES=$(LWIPDIR)/netif/ethernet.c

STDLIB_OBJS := $(SEL4CPRUNTIME)/syscall_time.o \
	$(SEL4CPRUNTIME)/syscall_implementation.o \
	$(SEL4CPRUNTIME)/sel4cp_runtime.o \
	$(SEL4CPRUNTIME)/syscall_uart.o \

# LWIPFILES: All the above.
LWIPFILES=lwip.c $(COREFILES) $(CORE4FILES) $(NETIFFILES)
LWIP_OBJS := $(LWIPFILES:.c=.o) \
				lwip.o \
				libsharedringbuffer/shared_ringbuffer.o \
				utilization_socket.o \
				udp_echo_socket.o timer.o \
				lwip_websrv_socket.o \
				lwip_nfs_socket.o \
				$(STDLIB_OBJS)


ETH_OBJS := eth.o libsharedringbuffer/shared_ringbuffer.o $(STDLIB_OBJS)
BENCH_OBJS := benchmark/benchmark.o $(STDLIB_OBJS)
IDLE_OBJS := benchmark/idle.o $(STDLIB_OBJS)
WEBSRV_OBJS := websrv.o libsharedringbuffer/shared_ringbuffer.o $(STDLIB_OBJS)
NFS_OBJS := nfs.o $(STDLIB_OBJS) libsharedringbuffer/shared_ringbuffer.o

all: directories $(IMAGE_FILE) sel4cp_runtime/lib/crt0.o

sel4cp_runtime/lib/crt0.o: $(SEL4CPRUNTIME)/lib/crt0.s
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile
	$(AS) -g3 -mcpu=$(CPU) $< -o $@

$(BUILD_DIR)/websrv.elf: $(addprefix $(BUILD_DIR)/, $(WEBSRV_OBJS)) $(MICROPYDIR)/build/websrv.o
	$(LD) $(MUSLDIR)/lib/crt1.o $(LDFLAGS) $^ $(LIBS) -o $@ 

$(BUILD_DIR)/nfs.elf: $(addprefix $(BUILD_DIR)/, $(NFS_OBJS))
	$(LD) $(MUSLDIR)/lib/crt1.o -L$(LIBNFSDIR)/build/lib $(LDFLAGS) $^ -lnfs $(LIBS) -o $@

$(BUILD_DIR)/eth.elf: $(addprefix $(BUILD_DIR)/, $(ETH_OBJS)) 
	$(LD) $(MUSLDIR)/lib/crt1.o $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/lwip.elf: $(addprefix $(BUILD_DIR)/, $(LWIP_OBJS))
	$(LD) $(MUSLDIR)/lib/crt1.o $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/benchmark.elf: $(addprefix $(BUILD_DIR)/, $(BENCH_OBJS))
	$(LD) $(MUSLDIR)/lib/crt1.o $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/idle.elf: $(addprefix $(BUILD_DIR)/, $(IDLE_OBJS))
	$(LD) $(MUSLDIR)/lib/crt1.o $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) eth.system
	$(SEL4CP_TOOL) eth.system --search-path $(BUILD_DIR) --board $(SEL4CP_BOARD) --config $(SEL4CP_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

.PHONY: all depend compile clean

%.o:
	$(CC) $(CFLAGS) -c $(@:.o=.c) -o $@

#Make the Directories
directories:
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/src))	\
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/src/core)) 	\
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/src/netif))	\
	$(info $(shell mkdir -p $(BUILD_DIR)/lwip/src/core/ipv4)) \
    $(info $(shell mkdir -p $(BUILD_DIR)/libsharedringbuffer))	\
	$(info $(shell mkdir -p $(BUILD_DIR)/benchmark))	\
	$(info $(shell mkdir -p $(BUILD_DIR)/sel4cp_runtime))	\

clean:
	rm -f *.o *.elf .depend*
	find . -name \*.o |xargs --no-run-if-empty rm

depend dep: .depend

include .depend

.depend: $(LWIPFILES)
	$(CC) $(CFLAGS) -MM $^ > .depend || rm -f .depend
	