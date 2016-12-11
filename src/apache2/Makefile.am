
# build and install apache2 module via the apxs2/apxs utility
# Reference: http://httpd.apache.org/docs/2.2/programs/apxs.html

bin_PROGRAMS = mod_imgmin_la
mod_imgmin_la_SOURCES = mod_imgmin.c ../imgmin.c ../dssim.c

mod_imgmin_la$(EXEEXT): $(mod_imgmin_la_SOURCES)
	if [ "$(APXS)" != "" ]; then \
		$(APXS) -I `pwd`/.. `$(MAGICK_CONFIG) --cppflags --ldflags --libs|xargs|sed "s/-fopenmp\s//"` -Wc,-DIMGMIN_LIB -Wc,-W -Wc,-Wall -Wc,-Wno-unused-parameter -c $(mod_imgmin_la_SOURCES); \
	fi

install-exec-local$(EXEEXT): mod_imgmin_la
	if [ "$(APXS)" != "" ]; then \
		$(APXS) -i -a mod_imgmin.la; \
	fi

