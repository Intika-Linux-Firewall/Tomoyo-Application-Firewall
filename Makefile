all:
	$(MAKE) -C usr_sbin all

install: all
	$(MAKE) -C usr_sbin install

clean:
##
## I don't enable "find" line because older versions does not support -delete
## action.
##
#	find -name '*~' -delete
	$(MAKE) -C usr_sbin clean

.PHONY: clean install
