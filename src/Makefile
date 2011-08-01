
CFLAGS = -W -Wall -Os
LDFLAGS = -lm

imgmin: imgmin.c
	$(CC) $(CFLAGS) $(LDFLAGS) `MagickWand-config --cflags --cppflags` -o imgmin imgmin.c `MagickWand-config --ldflags --libs`

clean:
	$(RM) imgmin
