
bin_PROGRAMS += \
	%D%/telemgui

%C%_telemgui_SOURCES = \
	%D%/main.c

%C%_telemgui_LDADD = \
	$(GTK_LIBS) \
	$(VTE_LIBS)

%C%_telemgui_CFLAGS = \
	$(AM_CFLAGS) \
	$(GTK_CFLAGS) \
	$(VTE_CFLAGS)

%C%_telemgui_LDFLAGS = \
	$(AM_LDFLAGS) \
	-pie
