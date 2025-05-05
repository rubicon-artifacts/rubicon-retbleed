CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2

OBJS = poc.o massage_shadow.o cache_files.o

TARGET = poc

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

poc.o: poc.c massage_shadow.h
	$(CC) $(CFLAGS) -c poc.c

massage_shadow.o: massage_shadow.c massage_shadow.h cache_files.h
	$(CC) $(CFLAGS) -c massage_shadow.c

cache_files.o: cache_files.c cache_files.h
	$(CC) $(CFLAGS) -c cache_files.c

clean:
	rm -f *.o $(TARGET)