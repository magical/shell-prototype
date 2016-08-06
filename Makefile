CFLAGS=-O2 -Wall `pkg-config --cflags pangocairo x11`
LDLIBS=`pkg-config --libs pangocairo x11` -lutil
main: main.o utf8.o shell.o
main.o: main.c term.h shell.h utf8.h
shell.o: shell.c shell.h
utf8.o: utf8.h
