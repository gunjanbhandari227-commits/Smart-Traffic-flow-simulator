CC      = gcc
TARGET  = traffic_sim
SRCDIR  = src

SRCS    = $(SRCDIR)/traffic_logic.c $(SRCDIR)/main_gui.c
OBJS    = $(SRCS:.c=.o)

GTK_FLAGS := $(shell pkg-config --cflags --libs gtk4)
CFLAGS  = -Wall -Wextra -O2 $(GTK_FLAGS) -I$(SRCDIR) -lm
LDFLAGS = $(GTK_FLAGS) -lm

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -f $(SRCDIR)/*.o $(TARGET)
