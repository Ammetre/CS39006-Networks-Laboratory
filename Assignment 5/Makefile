library: mysocket.o
	ar rcs libmsocket.a mysocket.o
mysocket.o: mysocket.h mysocket.c
	gcc $(CFLAGS) mysocket.c
CFLAGS = -g -c
OBJFILES = mysocket.o
LIBNAME = libmsocket.a
RM = rm -f
clean:
	-$(RM) $(OBJFILES)
distclean:
	-$(RM) $(OBJFILES) $(LIBNAME)
