
AM_CFLAGS = -W -Wall -Os
AM_LDLIBS = -lm

bin_PROGRAMS = imgmin mod_imgmin
imgmin_SOURCES = imgmin.c

imgmin:
	$(CC) $(AM_CFLAGS) $(AM_LDFLAGS) `$(MAGICK_CONFIG) --cflags --cppflags` -o $@ imgmin.c `$(MAGICK_CONFIG) --ldflags --libs`

mod_imgmin:
	$(MAKE) -C apache2

