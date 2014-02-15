#ifndef __SINK_H__
#define __SINK_H__

#include <glib.h>
#include <gtk/gtk.h>
#include <pulse/pulseaudio.h>

typedef struct
{
	GtkEventBox parent_instance;

	GtkScale *slider;
	GtkLabel *title;
	GtkImage *icon;

	gboolean app_moves_slider;

	pa_context *context;
	uint32_t index;
	const char *label;
	pa_cvolume volume;
	pa_operation *current_operation;
} PulseManagerSink;

PulseManagerSink *
pulse_manager_sink_new (pa_context *context, uint32_t index);

void
pulse_manager_sink_highlight (PulseManagerSink *sink, PulseManagerSink *old);

void
pulse_manager_sink_update (PulseManagerSink *sink, const pa_sink_input_info *info);

void
pulse_manager_sink_mute (PulseManagerSink *sink, int mute);

int
pulse_manager_sink_is_muted (PulseManagerSink *sink);

#endif // __SINK_H__
