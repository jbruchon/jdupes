# jdupes Makefile

#####################################################################
# Standand User Configuration Section                               #
#####################################################################

# PREFIX determines where files will be installed. Common examples
# include "/usr" or "/usr/local".
PREFIX = /usr

# Certain platforms do not support long options (command line options).
# To disable long options, uncomment the following line.
#CFLAGS += -DOMIT_GETOPT_LONG

# Uncomment for Linux with BTRFS support. Needed for -B/--dedupe.
# This can also be enabled at build time: 'make ENABLE_BTRFS=1'
#CFLAGS += -DENABLE_BTRFS

# Uncomment for low memory usage at the expense of speed and features
# This can be enabled at build time: 'make LOW_MEMORY=1'
#LOW_MEMORY=1

# Uncomment this to build in hardened mode.
# This can be enabled at build time: 'make HARDEN=1'
#HARDEN=1

#####################################################################
# Developer Configuration Section                                   #
#####################################################################

# PROGRAM_NAME determines the installation name and manual page name
PROGRAM_NAME = jdupes

# BIN_DIR indicates directory where program is to be installed.
# Suggested value is "$(PREFIX)/bin"
BIN_DIR = $(PREFIX)/bin

# MAN_DIR indicates directory where the jdupes man page is to be
# installed. Suggested value is "$(PREFIX)/man/man1"
MAN_BASE_DIR = $(PREFIX)/share/man
MAN_DIR = $(MAN_BASE_DIR)/man1
MAN_EXT = 1

# Required External Tools
INSTALL = install	# install : UCB/GNU Install compatiable
#INSTALL = ginstall
RM      = rm -f
MKDIR   = mkdir -p
#MKDIR   = mkdirhier
#MKDIR   = mkinstalldirs

# Make Configuration
CC ?= gcc
COMPILER_OPTIONS = -Wall -Wextra -Wwrite-strings -Wcast-align -Wstrict-aliasing -Wstrict-overflow -Wstrict-prototypes -Wpointer-arith -Wundef
COMPILER_OPTIONS += -Wshadow -Wfloat-equal -Wstrict-overflow=5 -Waggregate-return -Wcast-qual -Wswitch-default -Wswitch-enum -Wconversion -Wunreachable-code -Wformat=2 -Winit-self
COMPILER_OPTIONS += -std=gnu99 -O2 -g -D_FILE_OFFSET_BITS=64 -fstrict-aliasing -pipe

#####################################################################
# no need to modify anything beyond this point                      #
#####################################################################

# Debugging code inclusion
ifdef LOUD
DEBUG=1
COMPILER_OPTIONS += -DLOUD_DEBUG
endif
ifdef DEBUG
COMPILER_OPTIONS += -DDEBUG
endif
ifdef HARDEN
COMPILER_OPTIONS += -Wformat -Wformat-security -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE -fpie -Wl,-z,relro -Wl,-z,now
endif

# Catch someone trying to enable BTRFS in flags and turn on ENABLE_BTRFS
ifneq (,$(findstring DENABLE_BTRFS,$(CFLAGS)))
	ENABLE_BTRFS=1
endif
ifneq (,$(findstring DENABLE_BTRFS,$(CFLAGS_EXTRA)))
	ENABLE_BTRFS=1
endif

# MinGW needs this for printf() conversions to work
ifeq ($(OS), Windows_NT)
ifndef NO_UNICODE
	UNICODE=1
	COMPILER_OPTIONS += -municode
endif
	COMPILER_OPTIONS += -D__USE_MINGW_ANSI_STDIO=1 -DON_WINDOWS=1
	OBJS += win_stat.o
	override undefine ENABLE_BTRFS
endif

# xxHash support
ifdef USE_XXHASH
COMPILER_OPTIONS += -DUSE_HASH_XXHASH64
OBJS += xxhash.o
OBJS_CLEAN += jody_hash.o
else
COMPILER_OPTIONS += -DUSE_HASH_JODYHASH
OBJS += jody_hash.o
OBJS_CLEAN += xxhash.o
endif
# New BTRFS support option
ifdef ENABLE_BTRFS
COMPILER_OPTIONS += -DENABLE_BTRFS
OBJS += act_dedupefiles.o
else
OBJS_CLEAN += act_dedupefiles.o
endif
# Low memory mode
ifdef LOW_MEMORY
COMPILER_OPTIONS += -DLOW_MEMORY -DJODY_HASH_WIDTH=32 -DSMA_PAGE_SIZE=32768 -DCHUNK_SIZE=16384 -DNO_HARDLINKS -DNO_USER_ORDER
endif

CFLAGS += $(COMPILER_OPTIONS) $(CFLAGS_EXTRA)

INSTALL_PROGRAM = $(INSTALL) -m 0755
INSTALL_DATA    = $(INSTALL) -m 0644

# ADDITIONAL_OBJECTS - some platforms will need additional object files
# to support features not supplied by their vendor. Eg: GNU getopt()
#ADDITIONAL_OBJECTS += getopt.o

OBJS += jdupes.o jody_paths.o jody_sort.o jody_win_unicode.o string_malloc.o
OBJS += jody_cacheinfo.o
OBJS += act_deletefiles.o act_linkfiles.o act_printmatches.o act_summarize.o
OBJS += $(ADDITIONAL_OBJECTS)

all: $(PROGRAM_NAME)

$(PROGRAM_NAME): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM_NAME) $(OBJS)

installdirs:
	test -e $(DESTDIR)$(BIN_DIR) || $(MKDIR) $(DESTDIR)$(BIN_DIR)
	test -e $(DESTDIR)$(MAN_DIR) || $(MKDIR) $(DESTDIR)$(MAN_DIR)

install: jdupes installdirs
	$(INSTALL_PROGRAM)	$(PROGRAM_NAME)   $(DESTDIR)$(BIN_DIR)/$(PROGRAM_NAME)
	$(INSTALL_DATA)		$(PROGRAM_NAME).1 $(DESTDIR)$(MAN_DIR)/$(PROGRAM_NAME).$(MAN_EXT)

test:
	./test.sh
clean:
	$(RM) $(OBJS) $(OBJS_CLEAN) $(PROGRAM_NAME) $(PROGRAM_NAME).exe *~ *.gcno *.gcda *.gcov

distclean: clean
	$(RM) *.pkg.tar.xz

package:
	+./chroot_build.sh
