#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include "application.h"

typedef GtkApplicationClass PulseManagerAppClass;

G_DEFINE_TYPE (PulseManagerApp, pulse_manager_app, GTK_TYPE_APPLICATION);

static void
pulse_manager_app_finalize (GObject *object)
{
	G_OBJECT_CLASS (pulse_manager_app_parent_class)->finalize (object);
}

static void
pulse_manager_app_shutdown (GApplication *app)
{
	G_APPLICATION_CLASS (pulse_manager_app_parent_class)->shutdown (app);
}

static void
pulse_manager_app_resize (PulseManagerApp *app)
{
	int min_height, nat_height;

	gtk_widget_get_preferred_height (GTK_WIDGET (app->list), &min_height, &nat_height);

	gtk_window_resize (GTK_WINDOW (app->window),
	                   gtk_widget_get_allocated_width (app->window),
	                   min_height);
}

static void
pulse_manager_app_update_sink (PulseManagerApp *app, const pa_sink_input_info *info)
{
	PulseManagerSink *sink;
	const char *icon_name;

	sink = g_hash_table_lookup (app->sinks, GUINT_TO_POINTER (info->index));

	if (sink == NULL) {
		GtkBox *box;

		sink = pulse_manager_sink_new (app->context, info->index);

		gtk_box_pack_start (app->list, GTK_WIDGET (sink), 0, 0, 0);
		g_hash_table_insert (app->sinks,
		                     GUINT_TO_POINTER (info->index),
							 g_object_ref (sink));

		if (app->active_sink == NULL) {
			app->active_sink = sink;
			pulse_manager_sink_highlight (sink, NULL);
		}
	}

	pulse_manager_sink_update (sink, info);
}

static void
_got_sink_info_callback (pa_context *context,
                         const pa_sink_input_info *info,
						 int eol,
						 void *userdata)
{
	if (eol)
		return;

	PulseManagerApp *app = userdata;

#if 0
	printf ("APP\n");

	void *c = NULL;
	const char *key;
	while (key = pa_proplist_iterate (info->proplist, &c)) {
		printf ("\t%s --> %s\n", key, pa_proplist_gets (info->proplist, key));
	}
#endif

	pulse_manager_app_update_sink (app, info);
}

static void
_subscribe_callback (pa_context *context,
                     pa_subscription_event_type_t t,
                     uint32_t index,
                     void *userdata)
{
	PulseManagerApp *app = userdata;

	PulseManagerSink *sink;
	int event_about;
	int event_type;

	event_about = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
	event_type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

	if (event_about != PA_SUBSCRIPTION_EVENT_SINK_INPUT)
		return;

	switch (event_type) {
		case PA_SUBSCRIPTION_EVENT_NEW:
		case PA_SUBSCRIPTION_EVENT_CHANGE:
			pa_context_get_sink_input_info (context, index, _got_sink_info_callback, app);
			break;
		case PA_SUBSCRIPTION_EVENT_REMOVE:
			sink = g_hash_table_lookup (app->sinks, GUINT_TO_POINTER (index));

			if (sink) {
				g_hash_table_remove (app->sinks, GUINT_TO_POINTER (index));

				if (sink == app->active_sink) {
					// assign first item in list or NULL
					app->active_sink = g_hash_table_get_values (app->sinks)->data;
					if (app->active_sink)
						pulse_manager_sink_highlight (app->active_sink, NULL);
				}

				gtk_container_remove (GTK_CONTAINER (app->list), GTK_WIDGET (sink));
				pulse_manager_app_resize (app);
			}

			break;
	}
}

static void
_state_callback (pa_context *context, void *userdata)
{
	PulseManagerApp *app = userdata;
	pa_context_state_t state;

	state = pa_context_get_state (context);

	switch (state) {
		case PA_CONTEXT_CONNECTING:
			break;
		case PA_CONTEXT_READY:
			pa_context_subscribe (app->context, PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL);
			pa_context_get_sink_input_info_list (context, _got_sink_info_callback, app);
			break;
		case PA_CONTEXT_FAILED:
			break;
		case PA_CONTEXT_TERMINATED:
			break;
		case PA_CONTEXT_SETTING_NAME:
			break;
		case PA_CONTEXT_AUTHORIZING:
			break;
		case PA_CONTEXT_UNCONNECTED:
			break;
	}
}

gboolean _focus_lost_callback (GtkWidget *widget, GdkEventFocus *event, void *userdata)
{
	gtk_widget_hide (widget);

	return TRUE;
}

gboolean _key_press_callback (GtkWidget *widget, GdkEventKey *event, void *userdata)
{
	PulseManagerApp *app = userdata;
	int direction = 0;

	switch (event->keyval) {
		case GDK_KEY_Up:
		case GDK_KEY_j:
			direction = -1;
			break;
		case GDK_KEY_Down:
		case GDK_KEY_k:
			direction = 1;
			break;
		case GDK_KEY_Escape:
		case GDK_KEY_q:
			gtk_widget_hide (app->window);
			break;
		case GDK_KEY_h:
			g_signal_emit_by_name (app->active_sink->slider,
			                       "move-slider",
								   GTK_SCROLL_STEP_LEFT);
			break;
		case GDK_KEY_l:
			g_signal_emit_by_name (app->active_sink->slider,
			                       "move-slider",
								   GTK_SCROLL_STEP_RIGHT);
			break;
		case GDK_KEY_x:
		case GDK_KEY_m:
		{
			int is_muted = pulse_manager_sink_is_muted (app->active_sink);
			pulse_manager_sink_mute (app->active_sink, !is_muted);
			break;
		}
	}

	if (direction == 0)
		return FALSE;

	PulseManagerSink *next_active_sink = NULL;

	GList *l = gtk_container_get_children (GTK_CONTAINER (app->list));

	while (l) {
		if (l->data == app->active_sink) {
			if (direction < 0 && l->prev)
				next_active_sink = l->prev->data;
			else if (l->next)
				next_active_sink = l->next->data;
			break;
		}

		l = l->next;
	}

	if (next_active_sink) {
		pulse_manager_sink_highlight (next_active_sink, app->active_sink);
		app->active_sink = next_active_sink;
	}

	g_list_free (l);

	return TRUE;
}

static void
pulse_manager_app_startup (GApplication *application)
{
	PulseManagerApp *app = (PulseManagerApp *)application;

	G_APPLICATION_CLASS (pulse_manager_app_parent_class)->startup (application);

	app->sinks = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);

	pa_context_set_subscribe_callback (app->context, _subscribe_callback, app);
	pa_context_set_state_callback (app->context, _state_callback, app);

	pa_context_connect (app->context, NULL, PA_CONTEXT_NOFAIL, NULL);

	app->window = gtk_application_window_new (GTK_APPLICATION (app));
	app->list = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 6));

	g_object_set (G_OBJECT (app->list), "margin", 12, NULL);
	gtk_window_set_keep_above (GTK_WINDOW (app->window), 1);
	gtk_window_set_default_size (GTK_WINDOW (app->window), 640, -1);
	gtk_container_add (GTK_CONTAINER (app->window), GTK_WIDGET (app->list));

	g_signal_connect (G_OBJECT (app->window), "key-press-event", (GCallback)_key_press_callback, app);
	g_signal_connect (G_OBJECT (app->window), "focus-out-event", (GCallback)_focus_lost_callback, app);
}

static void
pulse_manager_app_activate (GApplication *application)
{
	PulseManagerApp *app = (PulseManagerApp *)application;
	GdkScreen *screen;
	GdkRectangle rect;
	int primary, window_width, window_height;

	screen = gtk_window_get_screen (GTK_WINDOW (app->window));

	gdk_screen_get_primary_monitor (screen);
	gdk_screen_get_monitor_geometry (screen, primary, &rect);

	gtk_window_get_size (GTK_WINDOW (app->window), &window_width, &window_height);
	gtk_window_move (GTK_WINDOW (app->window),
	                 rect.x + rect.width / 2 - window_width / 2,
	                 rect.y + 20);

	gtk_widget_show_all (app->window);
	gtk_window_present (GTK_WINDOW (app->window));
}

static int
pulse_manager_app_command_line (GApplication *application,
                                GApplicationCommandLine *command_line)
{
	PulseManagerApp *app = (PulseManagerApp *)application;
	int i, argc;
	char **argv;
	char *subject;

	if (!app->active_sink) {
		printf ("No sink selected, aborting.\n");
		exit (1);
		return 1;
	}

	argv = g_application_command_line_get_arguments (command_line, &argc);

	// no special command, just show
	if (argc < 2) {
		pulse_manager_app_activate (application);
		return 0;
	}

	subject = argv[1];

	if (strcmp (subject, "active") != 0) {
		printf ("Only actions on active app currently supported");
		return -1;
	}

	// print status
	if (argc == 2) {
		int volume;

		volume = pa_cvolume_max (&app->active_sink->volume) / PA_VOLUME_NORM * 100;

		printf ("Active app is '%s', Volume: %i%%\n", app->active_sink->label, volume);
		return 0;
	}

	if (argc == 3) {
		if (strcmp (argv[2], "mute") == 0) {
			pulse_manager_sink_mute (app->active_sink, !pulse_manager_sink_is_muted (app->active_sink));
		} else {
			int change;
			double volume;

			volume = gtk_range_get_value (GTK_RANGE (app->active_sink->slider));
			sscanf (argv[2], "%i%%\n", &change);

			volume += change / 100.0;
			if (volume < 0) volume = 0;
			if (volume > 1) volume = 1;

			gtk_range_set_value (GTK_RANGE (app->active_sink->slider), volume);
		}

		return 0;
	}
}

static void
pulse_manager_app_class_init (PulseManagerAppClass *class)
{
	GApplicationClass *application_class = G_APPLICATION_CLASS (class);
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	application_class->startup = pulse_manager_app_startup;
	application_class->shutdown = pulse_manager_app_shutdown;
	application_class->activate = pulse_manager_app_activate;
	application_class->command_line = pulse_manager_app_command_line;

	object_class->finalize = pulse_manager_app_finalize;
}

static void
pulse_manager_app_init (PulseManagerApp *app)
{
	app->active_sink = NULL;
}

PulseManagerApp *
pulse_manager_app_new (pa_context *context)
{
	PulseManagerApp *app;

	g_set_application_name ("Pulse App Manager");

	app = g_object_new (pulse_manager_app_get_type (),
	                    "application-id", "org.pantheon.pulse-app-manager",
						"flags", G_APPLICATION_HANDLES_COMMAND_LINE,
						NULL);

	app->context = context;

	return app;
}

