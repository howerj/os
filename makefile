CFLAGS=-Wall -Wextra -pedantic -O2 -std=c99

all: vm uc

run: vm os.img
	./vm os.img

os.img: uc
	./uc
	touch $@

clean:
	rm -rf vm uc
