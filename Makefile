SBINDIR=/usr/sbin
MANDIR=/usr/share/man
DOCDIR=/usr/share/doc

MODULE := ifstatfake.ko
obj-m := ifstatfake.o

all: modules

modules:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd)

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	rm -f Module.symvers

install: modules_install doc_install
	install ifstatfake.sh $(SBINDIR)/ifstatfake

modules_install: modules
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules_install

doc_install:
	install --mode=0644 ifstatfake.5 $(MANDIR)/man5
	install --mode=0644 ifstatfake.8 $(MANDIR)/man8
	install -d $(DOCDIR)/ifstatfake
	install --mode=0644 README $(DOCDIR)/ifstatfake
	install --mode=0644 COPYING $(DOCDIR)/ifstatfake
	install --mode=0644 INSTALL $(DOCDIR)/ifstatfake
	install --mode=0644 AUTHORS $(DOCDIR)/ifstatfake

uninstall: modules_uninstall doc_uninstall
	rm -f $(SBINDIR)/ifstatfake

modules_uninstall:
	@echo "================================================================================"
	@echo "The kernel's buildsystem doesn't support uninstalling modules."
	@echo "Please delete 'ifstatfake.ko' by hand from your system's module-tree"
	@echo "================================================================================"

doc_uninstall:
	rm -f $(MANDIR)/man5/ifstatfake.5
	rm -f $(MANDIR)/man8/ifstatfake.8
	rm -f $(DOCDIR)/ifstatfake/README
	rm -f $(DOCDIR)/ifstatfake/COPYING
	rm -f $(DOCDIR)/ifstatfake/INSTALL
	rm -f $(DOCDIR)/ifstatfake/AUTHORS
	rmdir $(DOCDIR)/ifstatfake/

