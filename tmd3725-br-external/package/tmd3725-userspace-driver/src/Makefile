CC ?= gcc
CFLAGS ?=
CFLAGS += -Wall -MMD -MP
LDFLAGS ?=

TARGET = tmd3725_sensor
SRCS = main.c TMD3725.c als_process.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)

.PHONY: all
