
AM_CFLAGS = -W -Wall -Os `MagickWand-config --cflags --cppflags --ldflags`
AM_LDFLAGS = -lm `MagickWand-config --libs`

bin_PROGRAMS = imgmin mod_imgmin
imgmin_SOURCES = imgmin.c

# $(CC) $(AM_CFLAGS) $(AM_LDFLAGS) -c -o imgmin.o $(imgmin_SOURCES)
# $(LD) `MagickWand-config --ldflags --libs` -o imgmin imgmin.o

imgmin:
	$(CC) $(AM_CFLAGS) -c -o imgmin.o $(imgmin_SOURCES)
	$(LD) $(AM_LDFLAGS) -o imgmin imgmin.o

mod_imgmin:
	$(MAKE) -C apache2

clean:
	$(RM) imgmin
	$(MAKE) -C apache2 clean

