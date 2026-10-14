# =====================================================
# 1. Host PC Settings (Linux x86_64)
# =====================================================
CC_PC = gcc
CFLAGS_PC = -O2 -Wall -Wextra -g
TARGET_PC = telemetry_host

# =====================================================
# 2. Raspberry Pi Zero 2 W Settings (ARMv8 Cortex-A53)
# =====================================================
CC_PI = arm-linux-gnueabihf-gcc
# Define cross-compilation sysroot path.
# The '?=' operator sets a default path if not already provided externally.
PI_SYSROOT ?= $(HOME)/pi_sysroot

# Compiler flags for cross-compilation using the ARM sysroot
CFLAGS_PI = -O2 -Wall -Wextra -g -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard \
            --sysroot=$(PI_SYSROOT) \
            -I$(PI_SYSROOT)/usr/include/arm-linux-gnueabihf \
            -L$(PI_SYSROOT)/usr/lib/arm-linux-gnueabihf \
            -L$(PI_SYSROOT)/lib/arm-linux-gnueabihf \
            -B$(PI_SYSROOT)/usr/lib/arm-linux-gnueabihf \
            -Wl,-rpath-link,$(PI_SYSROOT)/usr/lib/arm-linux-gnueabihf \
            -Wl,-rpath-link,$(PI_SYSROOT)/lib/arm-linux-gnueabihf
TARGET_PI = telemetry

# =====================================================
# Common Settings (Source Files and Libraries)
# =====================================================
SRC = firehose.c
LIBS = -lpthread -lwebsockets -lcjson

# =====================================================
# Build Rules
# =====================================================
# Default target: builds both host and target executables
all: pc pi

# Build rule for Host PC
pc:
	$(CC_PC) $(CFLAGS_PC) $(SRC) -o $(TARGET_PC) $(LIBS)

# Build rule for Raspberry Pi (Cross-Compilation)
pi:
	$(CC_PI) $(CFLAGS_PI) $(SRC) -o $(TARGET_PI) $(LIBS)

# Clean build artifacts and metrics log
clean:
	rm -f $(TARGET_PC) $(TARGET_PI) metrics_log.txt