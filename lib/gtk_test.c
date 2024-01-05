#include <string.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <sys/time.h>

#include <epoxy/gl.h>
#include <gtk/gtk.h>

#include "error.h"
#include "es2gears.h"
#include "timeval.h"

#define UNUSED __attribute__((unused))

unsigned int WIDTH = 800;
unsigned int HEIGHT = 600;

static GtkWidget *gl_area = NULL;

struct timeval start;
static struct es2gears_state *gears;

void GLAPIENTRY
MessageCallback(UNUSED GLenum source,
		GLenum type,
		UNUSED GLuint id,
		UNUSED GLenum severity,
		UNUSED GLsizei length,
		const GLchar* message,
		UNUSED const void* userParam )
{
	const char *type_string = "???";

#define _X(x) case GL_DEBUG_TYPE_ ## x: type_string = #x; break;
	switch (type) {
	_X(ERROR)		// An error, typically from the API
	_X(DEPRECATED_BEHAVIOR)	// Some behavior marked deprecated has been used
	_X(UNDEFINED_BEHAVIOR)	// Something has invoked undefined behavior
	_X(PORTABILITY)		// Some functionality the user relies upon is not portable
	_X(PERFORMANCE)		// Code has triggered possible performance issues
	_X(MARKER)		// Command stream annotation
	_X(PUSH_GROUP)		// Group pushing
	_X(POP_GROUP)		// Group popping
	_X(OTHER)		// Some type that isn't one of these
	default: type_string = "???"; break;
	}
#undef _X

	fprintf(stderr, "* GL %s: %s\n", type_string, message);
}

static void realize(GtkWidget *widget)
{
	assert(!gears);
	trace("BEGIN realize()");

	gtk_gl_area_make_current(GTK_GL_AREA(widget));
	GError *gerr = gtk_gl_area_get_error(GTK_GL_AREA(widget));
	if (gerr) {
		error("gtk_gl_area_make_current(): \'%s\'", gerr->message);
		return;
	}

	const GLubyte *cs;
	cs = glGetString(GL_VENDOR);
	trace("GL_VENDOR: %s", cs);
	cs = glGetString(GL_RENDERER);
	trace("GL_RENDERER: %s", cs);
	cs = glGetString(GL_VERSION);
	trace("GL_VERSION: %s", cs);

	gears = es2gears_init();
	if (!gears) {
		error("es2gears_init()");
		return;
	}

	es2gears_reshape(gears, WIDTH, HEIGHT);

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(MessageCallback, 0);

	int err = gettimeofday(&start, NULL);
	if (err < 0) {
		error_errno("gettimeofday()");
		return;
	}

	es2gears_draw(gears);
}

static void unrealize(GtkWidget *widget)
{
	assert(gears);
	trace("");

	gtk_gl_area_make_current(GTK_GL_AREA(widget));
	GError *err = gtk_gl_area_get_error(GTK_GL_AREA(widget));
	if (err) {
		error("gtk_gl_area_make_current(): \'%s\'", err->message);
		return;
	}

	es2gears_done(gears);
	gears = NULL;
}

static gboolean render(GtkGLArea *area, UNUSED GdkGLContext *context)
{
	assert(gears);

	GError *gerr = gtk_gl_area_get_error(area);
	if (gerr) {
		error("gtk_gl_area_make_current(): \'%s\'", gerr->message);
		return FALSE;
	}

	struct timeval now;
	unsigned long int dt_ms;

	int err = gettimeofday(&now, NULL);
	if (err < 0) {
		error_errno("gettimeofday()");
		return FALSE;
	}

	struct timeval dt;
	timeval_subtract(&dt, &now, &start);

	dt_ms = timeval_to_ms(&dt);

	gerr = gtk_gl_area_get_error(area);
	if (gerr) {
		error("gtk_gl_area_make_current(): \'%s\'", gerr->message);
		return FALSE;
	}

	es2gears_idle(gears, dt_ms);
	es2gears_draw(gears);

	gtk_gl_area_queue_render(area);

	return TRUE;
}

// static void
// on_axis_value_change (void)
// {
//   gtk_widget_queue_draw (gl_area);
// }

int main(int argc, char **argv)
{
	GtkWidget *window, *box;

	trace("");

	/* initialize gtk */
	gtk_init(&argc, &argv);

	/* Create new top level window. */
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW(window),WIDTH,HEIGHT);
	gtk_window_set_title(GTK_WINDOW(window), "GL Area");
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, FALSE);
	g_object_set(box, "margin", 12, NULL);
	gtk_box_set_spacing(GTK_BOX (box), 6);
	gtk_container_add (GTK_CONTAINER(window), box);

	gl_area = gtk_gl_area_new();
	gtk_box_pack_start(GTK_BOX(box), gl_area, 1, 1, 0);

	/*
	 * We need to initialize and free GL resources, so we use
	 * the realize and unrealize signals on the widget
	 */
	g_signal_connect (gl_area, "realize", G_CALLBACK (realize), NULL);
	g_signal_connect (gl_area, "unrealize", G_CALLBACK (unrealize), NULL);

	/* The main "draw" call for GtkGLArea */
	g_signal_connect (gl_area, "render", G_CALLBACK (render), NULL);

	/* Quit form main if got delete event */
	g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(gtk_main_quit), NULL);


	gtk_gl_area_set_use_es(GTK_GL_AREA(gl_area), true);
	gtk_gl_area_set_required_version(GTK_GL_AREA(gl_area), 2, 0);

	gtk_widget_show_all(GTK_WIDGET(window));
	gtk_main();

	exit(EXIT_SUCCESS);
}
