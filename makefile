LIBS = -lGL -lEGL -lm -lX11  -lwayland-client -lwayland-server -lwayland-cursor -lwayland-egl
CFLAGS = -I/usr/include/cairo -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/pixman-1 -I/usr/include/freetype2 -I/usr/include/libdrm -I/usr/include/libpng12  -I/usr/include

test : wayland_backend.o 
	gcc xdg-shell.c xdg-decorations.c glad.c wayland_backend.c ${CFLAGS} -o $@ ${LIBS}

clean:
	rm -f *.o *~ 