
AM_CFLAGS = -W -Wall -Os
AM_LDLIBS = -lm

bin_PROGRAMS = imgmin mod_imgmin
imgmin_SOURCES = imgmin.c

imgmin_OBJS = imgmin.o

imgmin:
	$(CC) $(AM_CFLAGS) $(AM_LDFLAGS) `MagickWand-config --cflags --cppflags` -o $@ imgmin.c `MagickWand-config --ldflags --libs`

mod_imgmin:
	$(MAKE) -C apache2

