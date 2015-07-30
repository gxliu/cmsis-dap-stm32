all:
	make -C src
	make -C tool
clean:
	make -C src clean
	make -C tool clean
flash:
	make -C src flash

.PHONY:all clean flash
