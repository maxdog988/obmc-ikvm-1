all:
	$(CC) obmc-ikvm.c -o obmc-ikvm -lvncserver

.PHONY: clean
clean:
	rm -f obmc-ikvm
