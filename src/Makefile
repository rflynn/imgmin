
CFLAGS = -W -Wall -Os
LDFLAGS = -lm

build_all: imgmin mod_imgmin

install: imgmin
	install imgmin /usr/local/bin/imgmin
	$(MAKE) -C apache2 install

imgmin: imgmin.c
	$(CC) $(CFLAGS) $(LDFLAGS) `MagickWand-config --cflags --cppflags` -o imgmin imgmin.c `MagickWand-config --ldflags --libs`

mod_imgmin:
	$(MAKE) -C apache2

clean:
	$(RM) imgmin
	$(MAKE) -C apache2 clean
