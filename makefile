#CFLAGS=-Wall -Wextra -pedantic -O2 -std=gnu99 `sdl2-config --cflags --libs` -lpcap
CFLAGS=-Wall -Wextra -pedantic -O2 -std=gnu99

all: vm uc as.hex

run: vm os.img
	./vm os.img out.img

os.hex: uc os.p
	./uc os.p os.hex

as.hex: as.fth
	gforth $<

%.img: %.hex hx
	./hx $< $@

clean:
	rm -rf vm uc hx *.hex *.img
