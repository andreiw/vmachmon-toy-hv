CC_FLAGS = -I./include

all: pvp
pvp: pvp.c pmem.c log.c
	gcc $^ $(CC_FLAGS) -o $@

clean:
	rm -f *~ *.o pvp
