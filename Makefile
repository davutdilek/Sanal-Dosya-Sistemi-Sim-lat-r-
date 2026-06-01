CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -Wno-stringop-overflow -Wno-restrict -Wno-array-bounds
TARGET  = fs_sim

all: $(TARGET)

$(TARGET): fs_sim.c
	$(CC) $(CFLAGS) -o $(TARGET) fs_sim.c

clean:
	rm -f $(TARGET) virtual_disk.bin

.PHONY: all clean
