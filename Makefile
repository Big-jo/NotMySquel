CC = gcc
CFLAGS = -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -lcgreen -Wl,-rpath,/opt/homebrew/lib

SRCS = main.c
TEST_SRCS = tests/test_main.c

TARGET = my_sql_app
TEST_TARGET = my_sql_tests

all: $(TARGET) $(TEST_TARGET)

$(TARGET):
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

$(TEST_TARGET):
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $(TEST_TARGET) $(LDFLAGS)

run: all
	./$(TARGET)

test:
	DYLD_LIBRARY_PATH=/opt/homebrew/lib ./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET)
