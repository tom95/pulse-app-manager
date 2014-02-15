#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

typedef struct
{
	GtkWidget *widget;
	GtkScale *slider;
	GtkLabel *title;
	GtkImage *icon;

	gboolean app_moves_slider;

	pa_context *context;
	uint32_t index;
	pa_cvolume volume;
	pa_operation *current_operation;
} Sink;

typedef struct
{
	pa_context *context;
	GHashTable *sinks;
	GtkWidget *window;
	GtkBox *list;

	Sink *active_sink;
} App;

void state_callback (pa_context *context, void *userdata);
void subscribe_callback (pa_context *context, pa_subscription_event_type_t t,
	uint32_t index, void *userdata);
Sink *update_sink (App *app, const pa_sink_input_info *info);
gboolean key_press_callback (GtkWidget* widget, GdkEventKey *event, void *userdata);

int main (int argc, char **argv)
{
	gtk_init (&argc, &argv);

	pa_glib_mainloop *loop;
	pa_mainloop_api *api;
	pa_context *context;

	App app;

	loop = pa_glib_mainloop_new (NULL);
	api = pa_glib_mainloop_get_api (loop);
	app.context = pa_context_new (api, NULL);
	app.sinks = g_hash_table_new_full (NULL, NULL, NULL, g_free);
	app.active_sink = NULL;

	pa_context_set_subscribe_callback (app.context, subscribe_callback, &app);
	pa_context_set_state_callback (app.context, state_callback, &app);

	app.window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	app.list = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 6));

	g_object_set (G_OBJECT (app.list), "margin", 12, NULL);
	gtk_window_set_keep_above (GTK_WINDOW (app.window), 1);
	gtk_window_set_default_size (GTK_WINDOW (app.window), 640, -1);
	gtk_container_add (GTK_CONTAINER (app.window), GTK_WIDGET (app.list));
	gtk_widget_show_all (app.window);

	g_signal_connect (G_OBJECT (app.window), "key-press-event", (GCallback)key_press_callback, &app);

	pa_context_connect (app.context, NULL, PA_CONTEXT_NOFAIL, NULL);

	gtk_main ();

	pa_glib_mainloop_free (loop);
}

void highlight_sink (App *app, Sink *new, Sink *old)
{
	if (old)
		gtk_widget_override_background_color (old->widget, GTK_STATE_FLAG_NORMAL, NULL);

	GdkRGBA color = { 1.0, 1.0, 1.0, 1.0 };

	gtk_widget_override_background_color (new->widget, GTK_STATE_FLAG_NORMAL, &color);
	gtk_widget_grab_focus (GTK_WIDGET (new->slider));
}

void update_volume_finished_cb (pa_context *context, int success, void *userdata)
{
	Sink *sink = userdata;

	sink->current_operation = NULL;
}

gboolean key_press_callback (GtkWidget *widget, GdkEventKey *event, void *userdata)
{
	App *app = userdata;
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
			gtk_widget_hide (GTK_WIDGET (app->window));
			break;
		case GDK_KEY_h:
			g_signal_emit_by_name (app->active_sink->slider, "move-slider", GTK_SCROLL_STEP_LEFT);
			break;
		case GDK_KEY_l:
			g_signal_emit_by_name (app->active_sink->slider, "move-slider", GTK_SCROLL_STEP_RIGHT);
			break;
		case GDK_KEY_x:
		case GDK_KEY_m:
			if (app->active_sink->current_operation)
				pa_operation_cancel (app->active_sink->current_operation);

			int is_muted = pa_cvolume_is_muted (&app->active_sink->volume);

			app->active_sink->volume = *pa_cvolume_scale (&app->active_sink->volume,
				is_muted ? PA_VOLUME_NORM : PA_VOLUME_MUTED);

			app->active_sink->current_operation = pa_context_set_sink_input_volume (app->active_sink->context,
				app->active_sink->index, &app->active_sink->volume, update_volume_finished_cb, app->active_sink);
			break;
	}

	if (direction == 0)
		return FALSE;

	gpointer next_active_widget = NULL;

	GList *l = gtk_container_get_children (GTK_CONTAINER (app->list));

	// find next widget in the list
	while (l) {
		if (l->data == app->active_sink->widget) {
			if (direction < 0 && l->prev)
				next_active_widget = l->prev->data;
			else if (l->next)
				next_active_widget = l->next->data;
			break;
		}

		l = l->next;
	}

	g_list_free (l);

	// get sink struct for next widget
	if (next_active_widget) {
		l = g_hash_table_get_values (app->sinks);

		while (l) {
			if (((Sink *)l->data)->widget == next_active_widget) {

				highlight_sink (app, l->data, app->active_sink);

				app->active_sink = l->data;

				break;
			}

			l = l->next;
		}

		g_list_free (l);
	}

	return TRUE;
}

double sink_get_volume (const pa_sink_input_info *info)
{
	pa_volume_t current_volume;

	current_volume = pa_cvolume_max (&info->volume);

	return (double)current_volume / PA_VOLUME_NORM;
}

void update_volume (GtkRange *widget, Sink *sink)
{
	double percentage;

	if (sink->app_moves_slider)
		return;

	if (sink->current_operation != NULL)
		pa_operation_cancel (sink->current_operation);

	percentage = gtk_range_get_value (GTK_RANGE(sink->slider));

	sink->volume = *pa_cvolume_scale (&sink->volume, PA_VOLUME_NORM * percentage);

	sink->current_operation = pa_context_set_sink_input_volume (sink->context,
		sink->index, &sink->volume, update_volume_finished_cb, sink);
}

void got_sources_callback (pa_context *context, const pa_sink_input_info *info, int eol, void *userdata)
{
	if (eol)
		return;

	App *app = userdata;

	printf ("APP\n");

	void *c = NULL;
	const char *key;
	while (key = pa_proplist_iterate (info->proplist, &c)) {
		printf ("\t%s --> %s\n", key, pa_proplist_gets (info->proplist, key));
	}

	update_sink (app, info);
}

Sink *update_sink (App *app, const pa_sink_input_info *info)
{
	Sink *sink;
	const char *icon_name;

	sink = g_hash_table_lookup (app->sinks, GUINT_TO_POINTER (info->index));

	if (sink == NULL) {
		GtkBox *box;

		sink = malloc (sizeof (Sink));

		sink->app_moves_slider = FALSE;
		sink->current_operation = NULL;
		sink->widget = gtk_event_box_new ();
		sink->title = GTK_LABEL (gtk_label_new (NULL));
		sink->slider = GTK_SCALE (gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.1));
		sink->icon = GTK_IMAGE (gtk_image_new ());
		sink->context = app->context;

		box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6));
		g_object_set (G_OBJECT (box), "margin", 6, NULL);

		gtk_scale_set_draw_value (sink->slider, 0);
		g_signal_connect (G_OBJECT (sink->slider), "value-changed", (GCallback)update_volume, sink);

		gtk_label_set_max_width_chars (sink->title, 10);
		gtk_label_set_line_wrap (sink->title, FALSE);
		gtk_label_set_ellipsize (sink->title, PANGO_ELLIPSIZE_END);
		gtk_misc_set_alignment (GTK_MISC (sink->title), 0.0, 0.5);
		g_object_set (G_OBJECT (sink->title), "width-request", 80, NULL);

		gtk_box_pack_start (box, GTK_WIDGET (sink->icon), 0, 0, 0);
		gtk_box_pack_start (box, GTK_WIDGET (sink->title), 0, 0, 0);
		gtk_box_pack_start (box, GTK_WIDGET (sink->slider), 1, 1, 0);
		gtk_container_add (GTK_CONTAINER (sink->widget), GTK_WIDGET (box));
		gtk_widget_show_all (GTK_WIDGET (sink->widget));

		gtk_box_pack_start (app->list, GTK_WIDGET (sink->widget), 0, 0, 0);

		g_hash_table_insert (app->sinks, GUINT_TO_POINTER (info->index), sink);

		if (app->active_sink == NULL) {
			app->active_sink = sink;
			highlight_sink (app, sink, NULL);
		}
	}

	sink->index = info->index;
	sink->volume = info->volume;

	if (pa_proplist_contains (info->proplist, PA_PROP_APPLICATION_ICON_NAME))
		icon_name = pa_proplist_gets (info->proplist, PA_PROP_APPLICATION_ICON_NAME);
	else
		icon_name = "application-multimedia";

	if (pa_proplist_contains (info->proplist, PA_PROP_APPLICATION_NAME))
		gtk_label_set_label (sink->title, pa_proplist_gets (info->proplist, PA_PROP_APPLICATION_NAME));
	else
		gtk_label_set_label (sink->title, "<untitled>");

	gtk_image_set_from_icon_name (sink->icon, icon_name, GTK_ICON_SIZE_DIALOG);

	sink->app_moves_slider = TRUE;
	gtk_range_set_value (GTK_RANGE (sink->slider), sink_get_volume (info));
	sink->app_moves_slider = FALSE;

	return sink;
}

void state_callback (pa_context *context, void *userdata)
{
	App *app = userdata;
	pa_context_state_t state;

	state = pa_context_get_state (context);

	switch (state) {
		case PA_CONTEXT_CONNECTING:
			break;
		case PA_CONTEXT_READY:
			pa_context_subscribe (app->context, PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL);
			pa_context_get_sink_input_info_list (context, got_sources_callback, userdata);
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

void resize (App *app) {
	int min_height, nat_height;
	gtk_widget_get_preferred_height (GTK_WIDGET (app->list), &min_height, &nat_height);

	gtk_window_resize (GTK_WINDOW (app->window),
		gtk_widget_get_allocated_width (app->window),
		min_height);
}

void subscribe_callback (pa_context *context, pa_subscription_event_type_t t,
	uint32_t index, void *userdata)
{
	App *app = userdata;

	Sink *sink;
	int event_about;
	int event_type;

	event_about = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
	event_type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

	if (event_about != PA_SUBSCRIPTION_EVENT_SINK_INPUT)
		return;

	switch (event_type) {
		case PA_SUBSCRIPTION_EVENT_NEW:
		case PA_SUBSCRIPTION_EVENT_CHANGE:
			pa_context_get_sink_input_info (context, index, got_sources_callback, userdata);
			break;
		case PA_SUBSCRIPTION_EVENT_REMOVE:
			sink = g_hash_table_lookup (app->sinks, GUINT_TO_POINTER (index));

			if (sink) {
				gtk_container_remove (GTK_CONTAINER (app->list), GTK_WIDGET (sink->widget));
				g_hash_table_remove (app->sinks, GUINT_TO_POINTER (index));

				resize (app);
			}

			if (sink == app->active_sink) {
				// assign first item in list or NULL
				app->active_sink = g_hash_table_get_values (app->sinks)->data;
				if (app->active_sink)
					highlight_sink (app, app->active_sink, NULL);
			}

			break;
	}
}

