#SANI=-fsanitize=address -fno-omit-frame-pointer

CC ?= cc
CFLAGS +=  -std=c99 -Wall -Wno-missing-braces -g -O -I/usr/local/include $(SANI)
LDFLAGS += -lm -linotify -L/usr/local/lib $(SANI)

TARGET = chiaharvestgraph
SRC = chiaharvestgraph.c grapher.c
OBJ = $(SRC:.c=.o)

all:	$(TARGET)

$(TARGET):	$(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.o $(TARGET)
	@echo All clean
