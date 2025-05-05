CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2

OBJS = find_secret.o

TARGET = find_secret

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

find_secret.o: find_secret.c
	$(CC) $(CFLAGS) -c find_secret.c

clean:
	rm -f *.o $(TARGET)