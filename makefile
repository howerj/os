#CFLAGS=-Wall -Wextra -pedantic -O2 -std=gnu99 `sdl2-config --cflags --libs` -lpcap
CFLAGS=-Wall -Wextra -pedantic -O2 -std=gnu99

all: vm uc as.hex

run: vm os.img
	./vm os.img out.img

os.hex: uc os.p
	./uc os.p
	touch $@

os.img: os.hex hx
	./hx $< $@

as.hex: as.fth
	gforth as.fth

clean:
	rm -rf vm uc hx *.hex *.img
