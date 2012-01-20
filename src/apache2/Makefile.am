
# build and install apache2 module via the apxs2 utility
# Reference: http://httpd.apache.org/docs/2.2/programs/apxs.html

mod_imgmin.la: mod_imgmin.c ../imgmin.c
	if [ "$(APXS)" != "" ]; then \
		$(APXS) -I `pwd`/.. `MagickWand-config --cppflags` `MagickWand-config --ldflags --libs|xargs` -Wc,-DIMGMIN_LIB -Wc,-W -Wc,-Wall -Wc,-Wno-unused-parameter -c mod_imgmin.c -c ../imgmin.c \
	fi

install: mod_imgmin.la
	if [ "$(APXS)" != "" ]; then \
		$(APXS) -i -a mod_imgmin.la \
	fi

