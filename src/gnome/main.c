/*
 * This program is part of the Clear Linux Project
 *
 * Copyright 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms and conditions of the GNU Lesser General Public License, as
 * published by the Free Software Foundation; either version 2.1 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <locale.h>
#include <grp.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <pwd.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <errno.h>

static GtkWidget *window = NULL;
static GtkWidget *spinner = NULL;
static GtkWidget *rbutton1, *rbutton2, *rbutton3;
static GtkWidget *button_ok, *button_send, *button_dont_send;
static int consent = 1;  /* 0: consent, anything else: no consent */

static GIOChannel *tm_stderr_ch = NULL;
static char payload_buffer[1024*16];

static int get_severity(void)
{
	int severity = 1;
	char *start, *end;

	start = end = NULL;
	if ((start = strstr (payload_buffer, "severity: ")) != NULL) {
			start += strlen ("severity: ");
			end = strchr (start, '\n');
	}

	if (start != NULL && end != NULL) {
		gchar *strseverity = g_strndup (start, (guint)(end - start));
		severity = atoi(strseverity);
		free(strseverity);
	}

	return severity;
}

static char *get_os_name (void)
{
	char *buffer;
	char *name;

	name = NULL;

	if (g_file_get_contents ("/usr/lib/os-release", &buffer, NULL, NULL)) {
		char *start, *end;

		start = end = NULL;
		if ((start = strstr (buffer, "NAME=")) != NULL) {
			start += strlen ("NAME=");
			end = strchr (start, '\n');
			}

		if (start != NULL && end != NULL) {
			name = g_strndup (start, (guint)(end - start));
		}

		g_free (buffer);
	}

	if (name && *name != '\0') {
		char *tmp;
		tmp = g_shell_unquote (name, NULL);
		g_free (name);
		name = tmp;
	}

	// TODO: maybe add VARIANT_ID if present

	if (name == NULL)
		name = g_strdup ("Clear Linux OS");

	return name;
}

static char *get_privacy_policy_url (void)
{
	char *buffer;
	char *url;

	url = NULL;

	if (g_file_get_contents ("/usr/lib/os-release", &buffer, NULL, NULL)) {
		char *start, *end;

		start = end = NULL;
		if ((start = strstr (buffer, "PRIVACY_POLICY_URL=")) != NULL) {
			start += strlen ("PRIVACY_POLICY_URL=");
			end = strchr (start, '\n');
		}

		if (start != NULL && end != NULL) {
			url = g_strndup (start, (guint)(end - start));
		}

		g_free (buffer);
	}

	if (url && *url != '\0') {
		char *tmp;
		tmp = g_shell_unquote (url, NULL);
		g_free (url);
		url = tmp;
	}

	if (url == NULL)
		url = g_strdup ("https://www.intel.com/content/www/us/en/privacy/intel-privacy-notice.html");

	return url;
}

static void privs_user(char *username)
{
	struct passwd *pw;

	printf("JB: [%s] username:%s\n", __func__, username);
	pw = getpwnam(username);
	if (!pw) {
		//telem_log(LOG_ERR, "user \"%s\" not found\n", username);
		exit(EXIT_FAILURE);
	}

	// The order is important here:
	// change supplemental groups, our gid, and then our uid
	if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
		//telem_perror("Failed to set supplemental group list");
		exit(EXIT_FAILURE);
	}
	if (setgid(pw->pw_gid) != 0) {
		//telem_perror("Failed to set GID");
		exit(EXIT_FAILURE);
	}
	if (setuid(pw->pw_uid) != 0) {
		//telem_perror("Failed to set UID");
		exit(EXIT_FAILURE);
	}

	assert(getuid() == pw->pw_uid);
	assert(geteuid() == pw->pw_uid);
	assert(getgid() == pw->pw_gid);
	assert(getegid() == pw->pw_gid);

	printf("JB: [%s] Done\n", __func__);
}

static void
on_button_show_click (GtkButton *buttonin, gpointer user_data)
{
	GtkWindow *parent = user_data;
	GtkWidget *dialog, *window2, *viewer;

	dialog = gtk_dialog_new_with_buttons ("Report Contents", parent,
				GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_USE_HEADER_BAR,
				"Close", GTK_RESPONSE_CLOSE, NULL);

	window2 = gtk_scrolled_window_new(0,0);
	gtk_container_set_border_width (GTK_CONTAINER (window2), 5);
	gtk_button_set_label(GTK_BUTTON(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog),
					GTK_RESPONSE_CLOSE)),"OK"); //setting label in dialog

	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(window2),400);
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(window2),700);

	viewer = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW (viewer), FALSE);
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (viewer), TRUE);
	gtk_container_add(GTK_CONTAINER(window2), viewer);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),window2,0,0,0);

    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(viewer)), payload_buffer, -1);
	g_signal_connect(dialog, "delete-event",G_CALLBACK(gtk_widget_destroy), NULL);
	gtk_widget_show_all(dialog);
	gint resp = gtk_dialog_run(GTK_DIALOG(dialog));

	if (resp == GTK_RESPONSE_CLOSE) {
		gtk_widget_destroy(dialog);
	}
}

static void tm_free_channel(void)
{
	if (tm_stderr_ch != NULL) {
		GError *error = NULL;
		if (g_io_channel_shutdown (tm_stderr_ch, TRUE, &error) != G_IO_STATUS_NORMAL) {
			g_warning ("Could not shutdown tm_stderr IO channel: %s", error->message);
		g_error_free (error);
		}

		g_io_channel_unref (tm_stderr_ch);
		tm_stderr_ch = NULL;
	}
}

static void
show_error_dialog (GtkWindow *parent, const gchar *message, const gchar *errmsg)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new (GTK_WINDOW(parent),
				   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT |
						      GTK_DIALOG_USE_HEADER_BAR,
				   GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
	if (errmsg != NULL)
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", errmsg);

	g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void
tm_telemctl_watch_func (GPid pid, gint status, gpointer user_data)
{
	GtkWindow *parent = user_data;
	gtk_spinner_stop (GTK_SPINNER (spinner));
	g_spawn_close_pid (pid);

	if (status != 0) {
		gchar *string = NULL;

		g_io_channel_read_line (tm_stderr_ch, &string, NULL, NULL, NULL);
		show_error_dialog (parent, _("Error"), string);
		g_free (string);
		tm_free_channel ();
	}
#if 1
	else {
		show_error_dialog (parent, _("Successfully changed error reporting state"), NULL);
	}
#endif
}

#if 0
static gint
capture_command_output (const char * cmd, char ** output)
{
	GError* error = NULL;
	gchar *ioerr;
	gint exit_status;

	if (!g_spawn_command_line_sync(cmd, output, &ioerr, &exit_status, &error)) {
		fprintf (stderr, "Error: %s\n", error->message);
		g_error_free (error);
		//TODO: proper exit
		//exit (1);
	}
	return exit_status;
}
#endif

static gboolean
capture_command_output_async (GtkWindow *parent,
			      void (*callback)(GPid, gint, gpointer),
			      gchar *argv[])
{
	g_autoptr(GError) error = NULL;
	GPid pid_child;

	gchar **envp;
	gchar str_tmp[80];
	gint tm_stderr, tm_stdout;

	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "LC_ALL", "C", TRUE);
	envp = g_environ_setenv (envp, "DISPLAY", ":0", TRUE);
	sprintf(str_tmp,"/run/user/%d/gdm/Xauthority", getuid());
	envp = g_environ_setenv (envp, "XAUTHORITY", str_tmp, TRUE);
//	sprintf(str_tmp,"/run/user/%d", getuid());
//	envp = g_environ_setenv (envp, "XDG_RUNTIME_DIR", str_tmp, TRUE);
//	envp = g_environ_setenv (envp, "XDG_SESSION_TYPE", "x11", TRUE);
//envp = g_environ_setenv (envp, "XDG_SEAT", "seat0", TRUE);
	gtk_spinner_start (GTK_SPINNER (spinner));
	//gtk_widget_set_sensitive (GTK_WIDGET (self->telemetry_install_button), FALSE);

	g_spawn_async_with_pipes (NULL,                            /* Working directory */
			    argv,                  /* Argument vector */
			    envp,                            /* Environment */
			    G_SPAWN_DO_NOT_REAP_CHILD,       /* Flags */
			    NULL,                            /* Child setup */
			    NULL,                            /* Data to child setup */
			    &pid_child,                      /* child PID */
			    NULL,                            /* Stdin */
			    &tm_stdout,                      /* Stdout */
			    &tm_stderr,                      /* Stderr */
			    &error);                         /* GError */

	g_strfreev (envp);

	if (error) {
		g_error ("Error in %s: %s", __func__, error->message);
		return FALSE;
	}

	g_child_watch_add (pid_child, callback, parent);
	tm_stderr_ch = g_io_channel_unix_new (tm_stderr);
	return TRUE;
}

typedef enum
	{
	DO_OPT_IN = 0,
	DO_CONSENT,
	DO_OPT_OUT
} tm_state;

static tm_state get_tm_state(void)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rbutton1)) == TRUE) {
		return DO_OPT_IN;
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rbutton2)) == TRUE) {
		return DO_CONSENT;
	} else {
		return DO_OPT_OUT;
	}
}

static void
on_button_ok_click (GtkButton *buttonin, gpointer user_data)
{
	GtkWindow *parent = user_data;
	tm_state state = get_tm_state();

#if 0
	/* Verify that we now have a controlling tty. */
	int fd = open("/dev/tty", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "ERROR: open /dev/tty failed; could not set controlling tty: %s",
			strerror(errno));
    } else {
		fprintf(stderr, "Success openning /dev/tty\n");
		close(fd);
	}
#endif
	if (state == DO_OPT_IN) {
		#if 1
		static gchar xauthority[80];
		sprintf(xauthority,"XAUTHORITY=/run/user/%d/gdm/Xauthority", getuid());
		//show_error_dialog (parent,xauthority, NULL);
		static gchar *argv[]  = {"/usr/bin/pkexec", "/usr/bin/env", "DISPLAY=:0", xauthority, "/usr/bin/telemctl", "opt-in", NULL};
		//static gchar *argv[]  = {"/usr/bin/pkexec", "/usr/bin/telemctl", "opt-in", NULL};
		if (!capture_command_output_async(parent, tm_telemctl_watch_func, argv)) {
			show_error_dialog (parent, _("Failed to opt-in to telemetry"), NULL);
		}
		
		#else
		GError* error = NULL;
		gchar *ioout, *ioerr;
		gint exit_status;
		g_spawn_command_line_sync("/usr/bin/pkexec /usr/bin/telemctl opt-in", &ioout, &ioerr, &exit_status, &error);
		fprintf(stderr, "JB ioout: %s\n", ioout);
		fprintf(stderr, "JB ioerr: %s\n", ioerr);
		#endif
		consent = 0;
	} else { //if (state == DO_OPT_OUT)
		static gchar *argv[] = {"/usr/bin/pkexec", "/usr/bin/telemctl", "opt-out", NULL};
		if (!capture_command_output_async(parent, tm_telemctl_watch_func, argv)) {
			show_error_dialog (parent, _("Failed to opt-out of telemetry"), NULL);
		}
		consent = 1;
	}
}

static void
on_button_send_click (GtkButton *buttonin, gpointer user_data)
{
	fprintf(stderr,"Clicked \"Send\"\n");
	consent = 0;
	gtk_widget_destroy(window);
}

static void
on_button_dont_send_click (GtkButton *buttonin, gpointer user_data)
{
	fprintf(stderr,"Clicked \"Don't send\"\n");
	consent = 1;
	gtk_widget_destroy(window);
}

/*Signal handler for the "toggled" signal of the RadioButton*/
static void
button_toggled_cb (GtkWidget *button, gpointer user_data)
{
	char *b_state;
	const char *button_label;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		b_state = "on";
		if (button == rbutton2) {
			gtk_widget_hide (GTK_WIDGET (button_ok));
			gtk_widget_show (GTK_WIDGET (button_send));
			gtk_widget_show (GTK_WIDGET (button_dont_send));
		} else {
			gtk_widget_show (GTK_WIDGET (button_ok));
			gtk_widget_hide (GTK_WIDGET (button_send));
			gtk_widget_hide (GTK_WIDGET (button_dont_send));
		}
	} else {
		b_state = "off";
		g_print ("\n");
	}

	button_label = gtk_button_get_label (GTK_BUTTON (button));
	g_print ("%s was turned %s\n", button_label, b_state);
}


static void
activate (GtkApplication *app, gpointer user_data)
{
	GtkWidget *fixed;
	GtkWidget *image;
	gchar *os_name, *url, *msg;

	int severity = get_severity();
	printf("JB: [%s] Here severity: %d\n", __func__, severity);
	spinner = gtk_spinner_new();
	GtkWidget *label1 = gtk_label_new(NULL);

	// Change icon and label text based on severity
	switch (severity) {
		case 1:
			image = gtk_image_new_from_icon_name ("dialog-question", GTK_ICON_SIZE_DIALOG);
			gtk_label_set_markup(GTK_LABEL(label1), "<b>Consent is needed to send a report</b>");
			break;
		case 2:
			image = gtk_image_new_from_icon_name ("dialog-warning", GTK_ICON_SIZE_DIALOG);
			gtk_label_set_markup(GTK_LABEL(label1), "<b>Sorry, the OS detected a possible problem</b>");
			break;
		case 3:
		case 4:
		default:
			image = gtk_image_new_from_icon_name ("dialog-error", GTK_ICON_SIZE_DIALOG);
			gtk_label_set_markup(GTK_LABEL(label1), "<b>Sorry, the OS detected a problem</b>");
			break;
	}

	//image = gtk_image_new_from_file ("/home/juro/clr.resized.png");

	/*Create the window and set a title*/
	window = gtk_application_window_new (app);
	gtk_window_set_title (GTK_WINDOW (window), "Clear Linux OS");
	gtk_container_set_border_width (GTK_CONTAINER (window), 12);
	gtk_window_set_default_size (GTK_WINDOW (window), 400, 300);

	//gtk_label_set_justify (GTK_LABEL (label1), GTK_JUSTIFY_CENTER);
	gtk_label_set_line_wrap (GTK_LABEL (label1), TRUE);
	//gtk_widget_set_hexpand (label1, TRUE);

	os_name = get_os_name ();

	/* translators: '%s' is the distributor's name, such as 'Clear Linux OS' */
	msg = g_strdup_printf
	(_("Sending reports of technical problems helps us improve %s. Reports are sent anonymously and are scrubbed of personal data."),
	os_name);

	g_free (os_name);
	GtkWidget *label_telemetry_explanation = gtk_label_new(NULL);
	gtk_label_set_line_wrap (GTK_LABEL (label_telemetry_explanation), TRUE);
	gtk_label_set_text (GTK_LABEL (label_telemetry_explanation), msg);
	g_free (msg);
	url = get_privacy_policy_url ();
	msg = g_strdup_printf ("<a href=\"%s\">%s</a>", url, _("Privacy Policy"));
	g_free (url);
	GtkWidget *label_privacy_policy_link = gtk_label_new(NULL);
	gtk_label_set_markup (GTK_LABEL (label_privacy_policy_link), msg);
	g_free (msg);

	/* The default buttons are: "send a report" & "Get user consent".
	 * We wouldn't be here otherwise
	 */

	rbutton1 = gtk_radio_button_new_with_label (NULL, _("Send all reports automatically from now on"));
	gtk_widget_set_tooltip_text(rbutton1, _( "Telemetry opt-in"));
	rbutton2 = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (rbutton1),
							 _("Get user consent for each report"));
	gtk_widget_set_tooltip_text(rbutton2, _( "Explicit consent is required for each report"));
	rbutton3 = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (rbutton1),
							 _("Never report anything"));
    gtk_widget_set_tooltip_text(rbutton3, _( "Opt-out of telemetry"));

	/* Be sure to set the initial state of each button */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rbutton1), FALSE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rbutton2), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rbutton3), FALSE);

	/* Connect the signal handlers to the buttons */
	g_signal_connect (GTK_TOGGLE_BUTTON (rbutton1), "toggled",
		    G_CALLBACK (button_toggled_cb), window);
	g_signal_connect (GTK_TOGGLE_BUTTON (rbutton2), "toggled",
		    G_CALLBACK (button_toggled_cb), window);
	g_signal_connect (GTK_TOGGLE_BUTTON (rbutton3), "toggled",
		    G_CALLBACK (button_toggled_cb), window);

	GtkWidget *button_details = gtk_button_new_with_label (_("Show details"));
	g_signal_connect (GTK_BUTTON (button_details), "clicked",
						G_CALLBACK (on_button_show_click), window);

	button_ok = gtk_button_new_with_label ("OK");
	g_signal_connect (GTK_BUTTON (button_ok), "clicked",
						G_CALLBACK (on_button_ok_click), window);
	gtk_widget_hide (GTK_WIDGET (button_ok));
	button_send = gtk_button_new_with_label ("Send");
	g_signal_connect (GTK_BUTTON (button_send), "clicked",
						G_CALLBACK (on_button_send_click), window);
	button_dont_send = gtk_button_new_with_label ("Don't send");
	g_signal_connect (GTK_BUTTON (button_dont_send), "clicked",
						G_CALLBACK (on_button_dont_send_click), window);

	fixed = gtk_fixed_new();
	gtk_container_add(GTK_CONTAINER(window), fixed);

	gtk_fixed_put(GTK_FIXED(fixed), image, 10, 20);
	gtk_fixed_put(GTK_FIXED(fixed), label1, 76, 10);
	gtk_widget_set_size_request(label1, 300, 30);
	gtk_fixed_put(GTK_FIXED(fixed), label_telemetry_explanation, 76, 40);
	gtk_widget_set_size_request(label_telemetry_explanation, 300, 60);
	gtk_fixed_put(GTK_FIXED(fixed), label_privacy_policy_link, 76, 110);
	gtk_fixed_put(GTK_FIXED(fixed), rbutton1, 10, 140);
	gtk_widget_set_size_request(rbutton1, 80, 30);
	gtk_fixed_put(GTK_FIXED(fixed), rbutton2, 10, 170);
	gtk_widget_set_size_request(rbutton2, 80, 30);
	gtk_fixed_put(GTK_FIXED(fixed), rbutton3, 10, 200);
	gtk_widget_set_size_request(rbutton3, 80, 30);
	gtk_fixed_put(GTK_FIXED(fixed), button_details, 10, 240);
	gtk_widget_set_size_request(button_details, 120, 30);
	gtk_fixed_put(GTK_FIXED(fixed), button_ok, 190, 240);
	gtk_widget_set_size_request(button_ok, 120, 30);
	//gtk_widget_hide (GTK_WIDGET (button_ok));
	
	gtk_fixed_put(GTK_FIXED(fixed), button_send, 135, 240);
	gtk_widget_set_size_request(button_send, 100, 30);
	gtk_fixed_put(GTK_FIXED(fixed), button_dont_send, 240, 240);
	gtk_widget_set_size_request(button_dont_send, 120, 30);
	gtk_widget_show_all (window);
}

static void create_control_terminal(void)
{
	char *slavename;
	int masterfd, fd;

	masterfd = open("/dev/ptmx", O_RDWR);
	fprintf(stderr, "masterfd: %d\n", masterfd);
	grantpt(masterfd);
	unlockpt(masterfd);
	slavename = ptsname(masterfd);
	fprintf(stderr,"slavename: %s\n", slavename);

	setsid();

	/* try to open /dev/tty */
	fd = open("/dev/tty", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open /dev/tty failed (expected): %s\n", strerror(errno));
    } else {
		fprintf(stderr, "Success (unexpected) openning /dev/tty\n");
		close(fd);
		return;
	}

	int ttyfd = open(slavename, O_RDWR);
	if (ttyfd < 0) {
		fprintf(stderr, "Failed to open %s (%s)\n", slavename, strerror(errno));
		//close(*ptyfd);
//		return FALSE;
	}

	  /* Make it our controlling tty. */
	fprintf(stderr, "Setting controlling tty using TIOCSCTTY.\n");

	if (ioctl(ttyfd, TIOCSCTTY, NULL) == -1) {
		fprintf(stderr, "ERROR: ioctl() TIOCSCTTY %s\n", strerror(errno));
	} else {
		fprintf(stderr, "SUCCSSES creating controlling terminal\n");
	}

  /* This appears to be necessary on some machines...  */
//	setpgid(0, 0);

	/* Verify that we now have a controlling tty. */
	fd = open("/dev/tty", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "ERROR: open /dev/tty failed; could not set controlling tty: %s\n",
			strerror(errno));
    } else {
		fprintf(stderr, "SUCCSESS openning /dev/tty\n");
		close(fd);
	}
}


int main (int argc, char **argv)
{
	GtkApplication *app;
	setlocale(LC_ALL, "");

#if 0
	if (fcntl(STDERR_FILENO, F_GETFL) < 0) {
		// redirect stderr to avoid bad things to happen with
		// fd 2 when launched from kernel core pattern
		if (freopen("/dev/null", "w", stderr) == NULL) {
			exit(EXIT_FAILURE);
		}
	}
#endif
	// Since this program handles core files, make sure it does not produce
	// core files itself, or else it will be invoked endlessly...
	prctl(PR_SET_DUMPABLE, 0);

	printf("\nJB: [%s] argc:%d\n", __func__, argc);

	if (argc > 1) {
		privs_user(argv[1]);
	}

	char line[LINE_MAX];
	char *buf = payload_buffer;
	size_t len, total_len = 0;

	memset (payload_buffer, 0, sizeof(payload_buffer));

	while (fgets(line,sizeof(line),stdin)) {
		//printf("JB->: %s",line);
		len = strlen(line);
		if ((len + total_len) < sizeof(payload_buffer)) {
			memcpy(buf, line, len);
			buf += len;
			total_len += len;
		}
	}

	create_control_terminal();

	app = gtk_application_new ("org.clearlinux.telemgui", G_APPLICATION_FLAGS_NONE);
	g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
	g_application_run (G_APPLICATION (app), 1, argv);
	g_object_unref (app);
	fprintf(stderr, "JB: [%s] DONE (consent:%d)\n", __func__, consent);
	return consent;
}
