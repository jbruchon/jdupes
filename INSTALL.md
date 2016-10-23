## Installing jdupes

To install the program, issue the following commands:

```
make
su root
make install
```

This will install the program in `/usr/local/bin`. You may change this
to a different location by editing the Makefile. Please refer to the
Makefile for an explanation of compile-time options. If you're having
trouble compiling, please take a look at the Makefile.

Various build options are available and can be turned on at compile
time by setting `CFLAGS_EXTRA` or by passing it to 'make':

```
make CFLAGS_EXTRA=-DYOUR_OPTION
make CFLAGS_EXTRA='-DYOUR_OPTION_ONE -DYOUR_OPTION_TWO'
```

This is a list of options that can be "turned on" this way:

>
```
ENABLE_BTRFS           Enable '-B/--dedupe' for btrfs deduplication
HAVE_BTRFS_IOCTL_H     Same as ENABLE_BTRFS
DEBUG              *   Turn on algorithm statistic reporting with '-D'
OMIT_GETOPT_LONG       Do not use getopt_long() C library call
ON_WINDOWS             Modify code to compile with MinGW on Windows
USE_TREE_REBALANCE *   Use experimental tree rebalancing code
CONSIDER_IMBALANCE *   Change tree rebalance to analyze weights first
```
`*` These options may slow down the program somewhat and are off by
  default. Do not enable them unless you are experimenting.

You can turn on the `-@` option for "loud" debugging with:

```
make LOUD=1
```

Non-loud debugging can be enabled with

```
make DEBUG=1
```

A test directory is included so that you may familiarize yourself with
the way jdupes operates. You may test the program before installing
it by issuing a command such as `./jdupes testdir` or
`./jdupes -r testdir`, just to name a couple of examples. Refer to the
documentation for information on valid options.

A comparison shell script is also included. It will run your natively
installed 'jdupes' or 'jdupes' with the directories and extra options
you specify and compare the run times and output a 'diff' of the two
program outputs. Unless the core algorithm or sort behavior is changed,
both programs should produce identical outputs and the 'diff' output
shouldn't appear at all. To use it, type:

```
./compare_jdupes.sh [options]
```
