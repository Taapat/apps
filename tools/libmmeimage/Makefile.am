lib_LTLIBRARIES = libmmeimage.la

libmmeimage_la_SOURCES = libmmeimage.c hw_interface.c libmmeimg_jpeg.c

AM_CFLAGS = -Wall -I$(DRIVER_TOPDIR)/multicom-4.0.6/include

libmmeimageincludedir =$(includedir)/libmmeimage

libmmeimageinclude_HEADERS = libmmeimage.h \
			hw_interface.h \
			libmmeimg_debug.h \
			libmmeimg_error.h
