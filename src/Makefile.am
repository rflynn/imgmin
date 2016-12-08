
AM_CFLAGS = -W -Wall -Os
AM_LDLIBS = -lm

bin_PROGRAMS = imgmin mod_imgmin
imgmin_SOURCES = imgmin.c dssim.c

imgmin$(EXEEXT): $(imgmin_SOURCES)
	$(CC) $(AM_CFLAGS) $(AM_LDFLAGS) `$(MAGICK_CONFIG) --cflags --cppflags` -o $@ $^ `$(MAGICK_CONFIG) --ldflags --libs` $(AM_LDLIBS)

mod_imgmin$(EXEEXT):
	$(MAKE) -C apache2

