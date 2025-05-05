CC=clang
CFLAGS=-I. -O2
#-fPIE -pie

RB_MASSEUR_O = ../massaging/massage_shadow.o ../massaging/cache_files.o

all: break_kaslr retbleed shmemer

shmemer: shmemer.c retbleed.h $(RB_MASSEUR_O)
	$(CC) -o $@ $(CFLAGS) $< $(RB_MASSEUR_O)

break_kaslr: break_kaslr.c retbleed.h
	$(CC) -o $@ $(CFLAGS) $<

retbleed: retbleed.c retbleed.h
	$(CC) -o $@ -lpthread $(CFLAGS) $<

clean:
	rm -f break_kaslr retbleed shmemer
