#include "sink.h"

typedef struct
{
	GtkApplication parent_instance;

	pa_context *context;
	GHashTable *sinks;
	GtkWidget *window;
	GtkBox *list;

	PulseManagerSink *active_sink;
} PulseManagerApp;

PulseManagerApp *
pulse_manager_app_new (pa_context *context);

