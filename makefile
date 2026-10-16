# SPIRE — TI-84 Plus CE
# Needs the CE C toolchain. https://ce-programming.github.io/toolchain/

ifeq ($(shell command -v cedev-config 2>/dev/null),)
  export PATH := $(HOME)/CEdev/bin:$(PATH)
endif

NAME = SPIRE
ICON = icon.png
DESCRIPTION = "Climb the spire"
COMPRESSED = YES
ARCHIVED = YES
HAS_PRINTF = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

include $(shell cedev-config --makefile)
