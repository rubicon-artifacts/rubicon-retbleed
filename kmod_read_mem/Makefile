CC=gcc
mod-name := read_mem
obj-m += $(mod-name).o
ccflags-y := \
	-O1 \
	-DDEBUG \
	-std=gnu99 \
	-Werror \
	$(CCFLAGS)
PWD=$(shell pwd)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

.PHONY:
install: all
	cp $(mod-name).ko /tmp
	sudo insmod /tmp/$(mod-name).ko

.PHONY:
remove:
	sudo rmmod $(mod-name).ko

.PHONY:
compiledb: clean
	bear -- make
	/home/jnes/COMSEC/code/kernel_tool/remove_compile_commands compile_commands.json
