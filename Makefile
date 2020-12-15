CC = gcc -g
CFLAGS = -O3 -Wall -Werror -DDRIVER

OBJS = mdriver.o mm.o memlib.o fsecs.o fcyc.o clock.o ftimer.o

all: mdriver

mdriver: $(OBJS)
	$(CC) $(CFLAGS) -o mdriver $(OBJS)

mdriver.o: mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h
memlib.o: memlib.c memlib.h
mm.o: mm.c mm.h memlib.h
fsecs.o: fsecs.c fsecs.h config.h
fcyc.o: fcyc.c fcyc.h
ftimer.o: ftimer.c ftimer.h config.h
clock.o: clock.c clock.h

format:
	clang-format --style=file -i *.c *.h

ARCHIVE = so19_$(shell basename $(PWD))
FILES = clock.c clock.h config.h fcyc.c fcyc.h fsecs.c fsecs.h ftimer.c \
	ftimer.h mdriver.c memlib.c memlib.h mm-implicit.c mm.c mm.h Makefile

archive: clean
	mkdir -p $(ARCHIVE) $(ARCHIVE)/traces
	cp -L $(FILES) $(ARCHIVE)/
	cp -L traces/* $(ARCHIVE)/traces/
	sed -i '' -e '/^#if.*STUDENT/,/^#endif.*STUDENT/d' \
		$(ARCHIVE)/mm-implicit.c $(ARCHIVE)/config.h
	tar cvzhf $(ARCHIVE).tar.gz $(ARCHIVE)
	rm -rf $(ARCHIVE)

clean:
	rm -f *~ *.o mdriver

.PHONY: all format archive clean
