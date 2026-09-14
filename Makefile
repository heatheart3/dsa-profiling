.PHONY: all clean dsa-perf

CFLAGS ?= -O3 -g -Wall -Wextra -Werror

all: tools/cached_read_bw dsa-perf

tools/cached_read_bw: tools/cached_read_bw.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS)

dsa-perf:
	cd dsa-perf-micros && \
		(test -x configure || ./autogen.sh) && \
		(test -f Makefile || ./configure CFLAGS='-g -O2' \
			--prefix=/usr --sysconfdir=/etc --libdir=/usr/lib) && \
		$(MAKE)

clean:
	$(RM) tools/cached_read_bw
	$(MAKE) -C dsa-perf-micros clean
