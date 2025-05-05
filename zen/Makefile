CC = clang
CFLAGS = -O3

UNAME := $(shell uname -r)

RB_MASSEUR_O = ../massaging/massage_shadow.o ../massaging/cache_files.o

ifeq ($(UNAME), 5.8.0-63-generic)
	INC_OFFSETS = ./offsets_5_8_0_63_generic.h
	CFLAGS += -DUBUNTU_5_8_0_63_GENERIC
else
	INC_OFFSETS = ./offsets_5_8_0_63_generic.h
	CFLAGS += -DUBUNTU_5_8_0_63_GENERIC
endif
$(info Using offsets $(INC_OFFSETS))

all: break_kaslr retbleed noisy_neighbor lol

break_kaslr: ./break_kaslr.c ./retbleed.h ./retbleed_zen.h $(INC_OFFSETS)
	$(CC) $(CFLAGS) -o $@ $<

retbleed: ./retbleed.c ./retbleed.h ./retbleed_zen.h  $(INC_OFFSETS) $(RB_MASSEUR_O)
	$(CC) $(CFLAGS) -o $@ $< $(RB_MASSEUR_O)

noisy_neighbor: ./noisy_neighbor.c
	$(CC) -O0 -o $@ $<

lol: ./lol.c
	$(CC) -O0 -o $@ $<

clean:
	rm  -f break_kaslr retbleed noisy_neighbor lol
