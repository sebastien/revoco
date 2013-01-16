V=0.5
CFLAGS=-Os -DVERSION=\"$(V)\" -Wall
LDFLAGS=-s

revoco: revoco.o

clean:
	rm -f revoco revoco.o a.out

tag:
	git tag v$(V)

tar:
	git tar-tree v$(V) revoco-$(V) | gzip -9 >revoco-$(V).tar.gz

