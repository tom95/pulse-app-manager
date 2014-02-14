
pa: pa.c
	gcc -g pa.c -o pa `pkg-config --libs --cflags libpulse-mainloop-glib gtk+-3.0`
