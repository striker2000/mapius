CFLAGS += -Wall `pkg-config --cflags gtk+-3.0 libsoup-2.4 python-2.7`
CFLAGS += -DGDK_PIXBUF_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED
LIBS += `pkg-config --libs gtk+-3.0 libsoup-2.4 python-2.7`
LIBS += -lproj

all: mapius

mapius: mapius-map.o main.o
	$(CC) -o $@ $^ $(LIBS)

clean:
	$(RM) *.o mapius
