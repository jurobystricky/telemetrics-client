MKDIR_P = /usr/bin/mkdir -p
mkdir_p = $(MKDIR_P)

#subdirsX = $(filter %/, $(wildcard docs/man/*/))

#subdirs=$(shell find lib -type d)

MANPAGES = \
	docs/man/telemctl.1 \
	docs/man/telemprobd.1 \
	docs/man/telempostd.1 \
	docs/man/telem-record-gen.1 \
	docs/man/telemetry.3 \
	docs/man/telemetrics.conf.5

MANLINKS = \
	docs/man/tm_create_record.3 \
	docs/man/tm_send_record.3 \
	docs/man/tm_set_config_file.3 \
	docs/man/tm_set_payload.3

dist_man_MANS = \
	$(MANPAGES) $(MANLINKS)

manpages: manpages-en manpages-@USE_NLS@

manpages-@USE_NLS@:

manpages-no:
manpages-yes:
	@echo add localized man pages

manpages-en:
	for MANPAGE in $(MANPAGES); do \
		rst2man.py $${MANPAGE}.rst > $${MANPAGE}; \
		rst2html.py $${MANPAGE}.rst > $${MANPAGE}.html; \
	done

manpages-yes-create-pot:
	$(mkdir_p) po-man; 
	for MANPAGE in $(MANPAGES); do \
		po4a-gettextize -f text -m $${MANPAGE}.rst -p $${MANPAGE}.pot ; \
	done

# vim: filetype=automake tabstop=8 shiftwidth=8 noexpandtab
