CC_FLAGS = -I./include -I./fdt -Wall

all: pvp pvp.dtb
pvp: pvp.c vmm.c pmem.c lib/log.c lib/err.c guest.c fdt/fdt.c fdt/fdt_ro.c fdt/fdt_strerror.c fdt/fdt_pvp.c rom.c lib/ranges.c term.c socket.c mon.c mmu_ranges.c disk.c
	gcc -g $^ $(CC_FLAGS) -o $@
pvp.dtb: pvp.dts
	dtc -I dts -O dtb < $< > $@

clean:
	rm -f include/*~ *~ *.o pvp
