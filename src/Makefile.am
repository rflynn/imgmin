
AM_CFLAGS = -W -Wall -Os `MagickWand-config --cflags --cppflags --ldflags`
AM_LDLIBS = -lm `MagickWand-config --libs`

bin_PROGRAMS = imgmin mod_imgmin
imgmin_SOURCES = imgmin.c

imgmin_OBJS = imgmin.o

imgmin:
	$(CC) $(AM_CFLAGS) -c -o $(imgmin_OBJS)
	$(LD) $(AM_LDLIBS) -o $@ $(imgmin_OBJS)

mod_imgmin:
	$(MAKE) -C apache2

