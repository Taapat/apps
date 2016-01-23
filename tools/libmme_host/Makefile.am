AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libmme_host.la

AM_CFLAGS = -Wall -D__LINUX__

libmme_host_la_SOURCES = \
	$(MULTICOM)/mme/mme_linux_user.c \
	$(MULTICOM)/mme/ExecutionLoop.c \
	$(MULTICOM)/mme/LocalTransformer.c \
	$(MULTICOM)/mme/LookupTable.c \
	$(MULTICOM)/mme/MMEMessageQueue.c \
	$(MULTICOM)/mme/mme_tune.c \
	$(MULTICOM)/embxshell/embx_linux.c

