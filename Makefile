CC_FLAGS = -I./include

all: vmm
vmm: vmm.c pmem.c log.c
	gcc $^ $(CC_FLAGS) -o $@

clean:
	rm -f *~ *.o vmm
