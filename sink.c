#include "sink.h"

typedef GtkEventBoxClass PulseManagerSinkClass;

G_DEFINE_TYPE (PulseManagerSink, pulse_manager_sink, GTK_TYPE_EVENT_BOX);

static void
update_volume_finished_cb (pa_context *context, int success, void *userdata)
{
	PulseManagerSink *sink = userdata;

	sink->current_operation = NULL;
}

static double
pulse_manager_sink_get_volume_from_info (const pa_sink_input_info *info)
{
	pa_volume_t current_volume;

	current_volume = pa_cvolume_max (&info->volume);

	return (double)current_volume / PA_VOLUME_NORM;
}

static void
_update_volume_cb (GtkRange *range, PulseManagerSink *sink)
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

void
pulse_manager_sink_highlight (PulseManagerSink *sink, PulseManagerSink *old)
{
	if (old)
		gtk_widget_override_background_color (GTK_WIDGET (old), GTK_STATE_FLAG_NORMAL, NULL);

	GdkRGBA color = { 1.0, 1.0, 1.0, 1.0 };

	gtk_widget_override_background_color (GTK_WIDGET (sink), GTK_STATE_FLAG_NORMAL, &color);
	gtk_widget_grab_focus (GTK_WIDGET (sink->slider));
}

void
pulse_manager_sink_update (PulseManagerSink *sink, const pa_sink_input_info *info)
{
	const char *icon_name;

	sink->volume = info->volume;

	if (pa_proplist_contains (info->proplist, PA_PROP_APPLICATION_ICON_NAME))
		icon_name = pa_proplist_gets (info->proplist, PA_PROP_APPLICATION_ICON_NAME);
	else
		icon_name = "application-multimedia";

	if (pa_proplist_contains (info->proplist, PA_PROP_APPLICATION_NAME))
		sink->label = pa_proplist_gets (info->proplist, PA_PROP_APPLICATION_NAME);
	else
		sink->label = "<untitled>";

	gtk_label_set_label (sink->title, sink->label);

	gtk_image_set_from_icon_name (sink->icon, icon_name, GTK_ICON_SIZE_DIALOG);

	sink->app_moves_slider = TRUE;
	gtk_range_set_value (GTK_RANGE (sink->slider), pulse_manager_sink_get_volume_from_info (info));
	sink->app_moves_slider = FALSE;
}

int
pulse_manager_sink_is_muted (PulseManagerSink *sink)
{
	return pa_cvolume_is_muted (&sink->volume);
}

void
pulse_manager_sink_mute (PulseManagerSink *sink, int mute)
{
	if (sink->current_operation)
		pa_operation_cancel (sink->current_operation);

	sink->volume = *pa_cvolume_scale (&sink->volume,
		mute ? PA_VOLUME_MUTED : PA_VOLUME_NORM);

	sink->current_operation = pa_context_set_sink_input_volume (sink->context,
	                                                            sink->index,
																&sink->volume,
																update_volume_finished_cb,
																sink);
}

static void
pulse_manager_sink_finalize (GObject *object)
{
	G_OBJECT_CLASS (pulse_manager_sink_parent_class)->finalize (object);
}

static void
pulse_manager_sink_init (PulseManagerSink *sink)
{
	GtkBox *box;

	sink->app_moves_slider = FALSE;
	sink->current_operation = NULL;

	sink->icon = GTK_IMAGE (gtk_image_new ());

	box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6));
	g_object_set (G_OBJECT (box), "margin", 6, NULL);

	sink->slider = GTK_SCALE (gtk_scale_new_with_range (
	                          GTK_ORIENTATION_HORIZONTAL,
	                          0.0, 1.0, 0.1));

	gtk_scale_set_draw_value (sink->slider, 0);
	g_signal_connect (G_OBJECT (sink->slider),
	                  "value-changed",
	                  (GCallback)_update_volume_cb,
	                  sink);

	sink->title = GTK_LABEL (gtk_label_new (NULL));

	gtk_label_set_max_width_chars (sink->title, 10);
	gtk_label_set_line_wrap (sink->title, FALSE);
	gtk_label_set_ellipsize (sink->title, PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment (GTK_MISC (sink->title), 0.0, 0.5);
	g_object_set (G_OBJECT (sink->title), "width-request", 80, NULL);

	gtk_box_pack_start (box, GTK_WIDGET (sink->icon), 0, 0, 0);
	gtk_box_pack_start (box, GTK_WIDGET (sink->title), 0, 0, 0);
	gtk_box_pack_start (box, GTK_WIDGET (sink->slider), 1, 1, 0);

	gtk_container_add (GTK_CONTAINER (sink), GTK_WIDGET (box));
	gtk_widget_show_all (GTK_WIDGET (sink));
}

static void
pulse_manager_sink_class_init (PulseManagerSinkClass *class)
{
	// GtkEventBoxClass *event_box_class = G_EVENT_BOX_CLASS (class);
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->finalize = pulse_manager_sink_finalize;
}

PulseManagerSink *
pulse_manager_sink_new (pa_context *context, uint32_t index)
{
	PulseManagerSink *sink;

	sink = g_object_new (pulse_manager_sink_get_type (), NULL);

	sink->context = context;
	sink->index = index;

	return sink;
}

