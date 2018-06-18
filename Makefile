all:
	$(CC) obmc-ikvm.c -o obmc-ikvm

.PHONY: clean
clean:
	rm -f obmc-ikvm
