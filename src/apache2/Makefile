
# build and install apache2 module via the apxs2 utility
# Reference: http://httpd.apache.org/docs/2.2/programs/apxs.html

mod_imgmin.la: mod_imgmin.c ../imgmin.c
	apxs2 -I `pwd`/.. `MagickWand-config --cppflags` `MagickWand-config --ldflags --libs|xargs` -Wc,-DIMGMIN_LIB -c mod_imgmin.c -c ../imgmin.c

install: mod_imgmin.la
	apxs2 -i -a mod_imgmin.la

clean:
	$(RM) mod_imgmin.la mod_imgmin.lo mod_imgmin.slo

