#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include "application.h"

int main (int argc, char **argv)
{
	gtk_init (&argc, &argv);

	int status;
	pa_glib_mainloop *loop;
	pa_mainloop_api *api;
	pa_context *context;

	PulseManagerApp *app;

	loop = pa_glib_mainloop_new (NULL);
	api = pa_glib_mainloop_get_api (loop);
	context = pa_context_new (api, NULL);

	app = pulse_manager_app_new (context);

	status = g_application_run (G_APPLICATION (app), argc, argv);

	g_object_unref (app);
	pa_glib_mainloop_free (loop);
}
