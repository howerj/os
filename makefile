CFLAGS=-Wall -Wextra -pedantic -O2 -std=gnu99

all: vm uc

run: vm os.img
	./vm os.img out.img

os.hex: uc os.p
	./uc os.p
	touch $@

os.img: os.hex hx
	./hx $< $@

clean:
	rm -rf vm uc hx *.hex *.img
