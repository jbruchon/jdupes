# jdupes Makefile

#####################################################################
# Standand User Configuration Section                               #
#####################################################################

# PREFIX determines where files will be installed. Common examples
# include "/usr" or "/usr/local".
PREFIX = /usr/local

# Certain platforms do not support long options (command line options).
# To disable long options, uncomment the following line.
#CFLAGS += -DOMIT_GETOPT_LONG

# Uncomment for -B/--dedupe.
# This can also be enabled at build time: 'make ENABLE_DEDUPE=1'
#CFLAGS += -DENABLE_DEDUPE

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
RMDIR	= rmdir -p
MKDIR   = mkdir -p
#MKDIR   = mkdirhier
#MKDIR   = mkinstalldirs

# Make Configuration
CC ?= gcc
COMPILER_OPTIONS = -Wall -Wextra -Wwrite-strings -Wcast-align -Wstrict-aliasing -Wstrict-overflow -Wstrict-prototypes -Wpointer-arith -Wundef
COMPILER_OPTIONS += -Wshadow -Wfloat-equal -Wstrict-overflow=5 -Waggregate-return -Wcast-qual -Wswitch-default -Wswitch-enum -Wconversion -Wunreachable-code -Wformat=2 -Winit-self
COMPILER_OPTIONS += -std=gnu99 -O2 -g -D_FILE_OFFSET_BITS=64 -fstrict-aliasing -pipe
COMPILER_OPTIONS += -DSMA_MAX_FREE=11

#####################################################################
# no need to modify anything beyond this point                      #
#####################################################################

# Set built-on date for display in program version info screen
ifdef EMBED_BUILD_DATE
BD=$(shell date +"\"%Y-%m-%d %H:%M:%S %z\"")
$(shell echo "#define BUILT_ON_DATE \"$(BD)\"" > build_date.h)
COMPILER_OPTIONS += -DBUILD_DATE
endif

# Debugging code inclusion
ifdef LOUD
DEBUG=1
COMPILER_OPTIONS += -DLOUD_DEBUG
endif
ifdef DEBUG
COMPILER_OPTIONS += -DDEBUG
else
COMPILER_OPTIONS += -DNDEBUG
endif
ifdef HARDEN
COMPILER_OPTIONS += -Wformat -Wformat-security -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE -fpie -Wl,-z,relro -Wl,-z,now
endif

# Catch someone trying to enable BTRFS in flags and turn on ENABLE_DEDUPE
ifneq (,$(findstring DENABLE_BTRFS,$(CFLAGS) $(CFLAGS_EXTRA)))
	ENABLE_DEDUPE=1
endif
ifneq (,$(findstring DENABLE_DEDUPE,$(CFLAGS) $(CFLAGS_EXTRA)))
	ENABLE_DEDUPE=1
endif

# MinGW needs this for printf() conversions to work
ifeq ($(OS), Windows_NT)
ifndef NO_UNICODE
	UNICODE=1
	COMPILER_OPTIONS += -municode
	PROGRAM_SUFFIX=.exe
endif
	COMPILER_OPTIONS += -D__USE_MINGW_ANSI_STDIO=1 -DON_WINDOWS=1
	OBJS += win_stat.o winres.o
	override undefine ENABLE_DEDUPE
endif

# Compatibility mappings for dedupe feature
ifdef ENABLE_BTRFS
ENABLE_DEDUPE=1
endif
ifdef STATIC_BTRFS_H
STATIC_DEDUPE_H=1
endif

# Dedupe feature (originally only BTRFS, now generalized)
ifdef ENABLE_DEDUPE
COMPILER_OPTIONS += -DENABLE_DEDUPE
OBJS += act_dedupefiles.o
else
OBJS_CLEAN += act_dedupefiles.o
endif
ifdef STATIC_DEDUPE_H
COMPILER_OPTIONS += -DSTATIC_DEDUPE_H
endif

# Low memory mode
ifdef LOW_MEMORY
COMPILER_OPTIONS += -DLOW_MEMORY -DSMA_PAGE_SIZE=32768 -DCHUNK_SIZE=16384 -DNO_HARDLINKS -DNO_SYMLINKS -DNO_USER_ORDER -DNO_PERMS
endif

CFLAGS += $(COMPILER_OPTIONS) $(CFLAGS_EXTRA)

INSTALL_PROGRAM = $(INSTALL) -m 0755
INSTALL_DATA    = $(INSTALL) -m 0644

# ADDITIONAL_OBJECTS - some platforms will need additional object files
# to support features not supplied by their vendor. Eg: GNU getopt()
#ADDITIONAL_OBJECTS += getopt.o

OBJS += jdupes.o jody_paths.o jody_sort.o jody_win_unicode.o jody_strtoepoch.o string_malloc.o
OBJS += jody_cacheinfo.o
OBJS += act_deletefiles.o act_linkfiles.o act_printmatches.o act_summarize.o act_printjson.o
OBJS += xxhash.o
OBJS += $(ADDITIONAL_OBJECTS)

OBJS_CLEAN += jdupes-standalone

all: $(PROGRAM_NAME)

static: $(PROGRAM_NAME)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM_NAME) $(OBJS) -static

static_stripped: $(PROGRAM_NAME)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM_NAME) $(OBJS) -static
	strip $(PROGRAM_NAME)

$(PROGRAM_NAME): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM_NAME) $(OBJS)

winres.o : winres.rc winres.manifest.xml
	./tune_winres.sh
	windres winres.rc winres.o

standalone: jdupes-standalone

installdirs:
	test -e $(DESTDIR)$(BIN_DIR) || $(MKDIR) $(DESTDIR)$(BIN_DIR)
	test -e $(DESTDIR)$(MAN_DIR) || $(MKDIR) $(DESTDIR)$(MAN_DIR)

install: $(PROGRAM_NAME) installdirs
	$(INSTALL_PROGRAM)	$(PROGRAM_NAME)   $(DESTDIR)$(BIN_DIR)/$(PROGRAM_NAME)
	$(INSTALL_DATA)		$(PROGRAM_NAME).1 $(DESTDIR)$(MAN_DIR)/$(PROGRAM_NAME).$(MAN_EXT)

uninstalldirs:
	-test -e $(DESTDIR)$(BIN_DIR) && $(RMDIR) $(DESTDIR)$(BIN_DIR)
	-test -e $(DESTDIR)$(MAN_DIR) && $(RMDIR) $(DESTDIR)$(MAN_DIR)

uninstall: uninstalldirs
	$(RM)	$(DESTDIR)$(BIN_DIR)/$(PROGRAM_NAME)
	$(RM)	$(DESTDIR)$(MAN_DIR)/$(PROGRAM_NAME).$(MAN_EXT)

test:
	./test.sh

stripped: $(PROGRAM_NAME)
	strip $(PROGRAM_NAME)$(PROGRAM_SUFFIX)

clean:
	$(RM) $(OBJS) $(OBJS_CLEAN) build_date.h $(PROGRAM_NAME) $(PROGRAM_NAME).exe *~ *.gcno *.gcda *.gcov

distclean: clean
	$(RM) *.pkg.tar.xz
	$(RM) -r jdupes-*-*/ jdupes-*-*.zip

chrootpackage:
	+./chroot_build.sh
package:
	+./generate_packages.sh
