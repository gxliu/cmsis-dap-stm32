all:
	make -C src
clean:
	make -C src clean
flash:
	make -C src flash

.PHONY:all clean flash
