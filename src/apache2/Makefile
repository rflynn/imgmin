
all:
	apxs2 -i -a -I `pwd`/.. `MagickWand-config --cppflags` `MagickWand-config --ldflags --libs|xargs` -Wc,-DIMGMIN_LIB -c mod_imgmin.c -c ../imgmin.c

