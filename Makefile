CFLAGS=-O2 -Wall `pkg-config --cflags pangocairo x11`
LDLIBS=`pkg-config --libs pangocairo x11` -lutil
main: main.o utf8.o shell.o
