CFLAGS=-O2 -Wall `pkg-config --cflags pangocairo x11`
LDLIBS=`pkg-config --libs pangocairo x11`
main: main.o utf8.o
