SUBDIRS = \
shell \
init

all:
	@for d in $(SUBDIRS); do \
		$(MAKE) -C $$d; \
	done

clean:
	@for d in $(SUBDIRS); do \
		$(MAKE) -C $$d clean; \
	done

.PHONY: all clean
