CC_FLAGS = -I./include

all: pvp
pvp: pvp.c vmm.c pmem.c log.c guest.c
	gcc -g $^ $(CC_FLAGS) -o $@

clean:
	rm -f include/*~ *~ *.o pvp
