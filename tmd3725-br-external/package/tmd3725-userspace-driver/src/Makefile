CC     ?= gcc
CFLAGS ?= -Wall
LDFLAGS ?=

TARGET = tmd3725_sensor
SRCS   = main.c TMD3725.c als_process.c
OBJS   = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
