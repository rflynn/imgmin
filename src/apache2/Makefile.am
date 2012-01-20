
# build and install apache2 module via the apxs2/apxs utility
# Reference: http://httpd.apache.org/docs/2.2/programs/apxs.html

bin_PROGRAMS = mod_imgmin_la
mod_imgmin_la_SOURCES = mod_imgmin.c ../imgmin.c

mod_imgmin_la:
	if [ "$(APXS)" != "" ]; then \
		$(APXS) -I `pwd`/.. `MagickWand-config --cppflags` `MagickWand-config --ldflags --libs|xargs` -Wc,-DIMGMIN_LIB -Wc,-W -Wc,-Wall -Wc,-Wno-unused-parameter -c mod_imgmin.c -c ../imgmin.c; \
	fi

install-exec-local: mod_imgmin_la
	if [ "$(APXS)" != "" ]; then \
		$(APXS) -i -a mod_imgmin.la; \
	fi

