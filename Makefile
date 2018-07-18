all:
	$(CC) obmc-ikvm.c -o obmc-ikvm -lvncserver -lpthread

.PHONY: clean
clean:
	rm -f obmc-ikvm
