# 02 miOT 本地协议 C Demo
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../01_c_demo/inc
GWSRC = ../01_c_demo/src/miio_crypto.c ../01_c_demo/src/discovery.c ../01_c_demo/src/command.c ../01_c_demo/third_party/cJSON/cJSON.c
LDLIBS = -lcurl -lssl -lcrypto
TARGET = miot_local_demo

.PHONY: all clean
all: $(TARGET)
$(TARGET): src/main.c $(GWSRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
clean:
	rm -f $(TARGET)
