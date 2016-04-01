all: vmm
vmm: vmachmon32.c
	gcc $< -o $@

clean:
	rm -f *~ *.o vmm