CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../01_c_demo/inc -I../01_c_demo/third_party/cJSON
GWSRC = ../01_c_demo/src/miio_crypto.c ../01_c_demo/src/discovery.c ../01_c_demo/src/command.c ../01_c_demo/third_party/cJSON/cJSON.c
TARGET = miio_cli
LDLIBS = -lcurl -lssl -lcrypto

.PHONY: all clean

all: $(TARGET)

$(TARGET): src/main.c $(GWSRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f $(TARGET)
