# LADSPA plugin IDs 5761—5800 have been reserved for github/michaellass
PLUGIN_ID := 5761

# Default destination
DESTDIR ?= /usr/local/lib/ladspa

.phony: clean install

notch_harmonics_$(PLUGIN_ID).so: notch_harmonics.c
	$(CC) -DPLUGIN_ID=$(PLUGIN_ID) -O2 -shared -fPIC -o $@ $^ -lm

clean:
	rm -f notch_harmonics_$(PLUGIN_ID).so

install: notch_harmonics_$(PLUGIN_ID).so
	install -Dm 755 $< $(DESTDIR)/$<