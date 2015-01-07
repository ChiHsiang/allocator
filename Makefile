CFLAGS = -std=c11 -fPIC -D_GNU_SOURCE -fvisibility=hidden -Wall -Wextra
LDFLAGS = -Wl,--as-needed
LDLIBS = -lpthread
OBJECTS = alloc.o bump.o chunk.o extent.o huge.o memory.o

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	CFLAGS += -Og -g
else
	CFLAGS += -flto -O2 -DNDEBUG
	LDFLAGS += -flto -O2
endif

all: alloc.so test_small test_large test_huge

alloc.so: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared $^ $(LDLIBS) -o $@

test_small: test_small.c $(OBJECTS)
test_large: test_large.c $(OBJECTS)
test_huge: test_huge.c $(OBJECTS)

alloc.o: alloc.c bump.h chunk.h chunk.h huge.h memory.h util.h
bump.o: bump.c bump.h chunk.h memory.h
chunk.o: chunk.c chunk.h extent.h memory.h
extent.o: extent.c bump.h extent.h
huge.o: huge.c chunk.h huge.h memory.h
memory.o: memory.c memory.h

.PHONY: all
