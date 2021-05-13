CC ?= cc
CFLAGS +=  -D_POSIX_C_SOURCE=200809L -std=c99 -Wall -Wno-missing-braces -g
LDFLAGS +=

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
