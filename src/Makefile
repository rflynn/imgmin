
CFLAGS = -W -Wall -ggdb

imgmin: imgmin.c
	$(CC) $(CFLAGS) `MagickWand-config --cflags --cppflags` -Os -o imgmin imgmin.c `MagickWand-config --ldflags --libs`

clean:
	$(RM) imgmin
