all:
	gcc masd.c -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse -pthread -lfuse -lrt -ldl -o masd -g
	gcc samd.c -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse -pthread -lfuse -lrt -ldl -o samd -g

clean:
	rm -f samd masd
