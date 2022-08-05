CC_FLAGS = -I./include -Wall

all: pvp
pvp: pvp.c vmm.c pmem.c log.c guest.c fdt.c fdt_ro.c fdt_strerror.c fdt_pvp.c rom.c ranges.c term.c
	gcc -g $^ $(CC_FLAGS) -o $@

clean:
	rm -f include/*~ *~ *.o pvp
