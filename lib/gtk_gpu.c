#include <string.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/eventfd.h>

#include <epoxy/gl.h>
#include <gtk/gtk.h>

#include "error.h"
#include "es2gears.h"
#include "timeval.h"
#include "virtio_thread.h"
#include "gettid.h"
#include "libvirtiolo.h"
#include "virtio_request.h"

#define UNUSED __attribute__((unused))

static const unsigned int WIDTH = 800;
static const unsigned int HEIGHT = 600;

static GtkWidget *gl_area = NULL;

struct timeval start;
static struct es2gears_state *gears;
static int efd;

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

	fprintf(stderr, "%c GL %s: %s\n", type == GL_DEBUG_TYPE_ERROR ? '*' : '-', type_string, message);
}

static pthread_mutex_t req_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static STAILQ_HEAD(, virtio_thread_request) req_queue = STAILQ_HEAD_INITIALIZER(req_queue);

static void realize(GtkWidget *widget)
{
	assert(!gears);
	trace("");

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

	unsigned long int dt_ms;
	struct timeval dt;
	struct timeval now;
	GError *gerr;
	int err;

	gtk_gl_area_make_current(area);
	gerr = gtk_gl_area_get_error(area);
	if (gerr) {
		error("gtk_gl_area_make_current(): \'%s\'", gerr->message);
		return FALSE;
	}

	err = gettimeofday(&now, NULL);
	if (err < 0) {
		error_errno("gettimeofday()");
		return FALSE;
	}

	timeval_subtract(&dt, &now, &start);

	dt_ms = timeval_to_ms(&dt);

	es2gears_idle(gears, dt_ms);
	es2gears_draw(gears);

	gerr = gtk_gl_area_get_error(area);
	if (gerr) {
		error("gtk_gl_area_make_current(): \'%s\'", gerr->message);
		return FALSE;
	}

	gtk_gl_area_queue_render(area);

	return TRUE;
}

static void on_resize(UNUSED GtkGLArea *area, int width, int height, UNUSED gpointer user_data)
{
	es2gears_reshape(gears, width, height);
}

static gboolean on_key_press(UNUSED GtkWidget *widget, GdkEventKey *event, UNUSED gpointer user_data)
{
	switch (event->keyval)
	{
	case GDK_KEY_Up:
		es2gears_special(gears, SPECIAL_UP);
		break;
	case GDK_KEY_Down:
		es2gears_special(gears, SPECIAL_DOWN);
		break;
	case GDK_KEY_Left:
		es2gears_special(gears, SPECIAL_LEFT);
		break;
	case GDK_KEY_Right:
		es2gears_special(gears, SPECIAL_RIGHT);
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

// called from the helper thread
void virtio_thread_new_request(struct virtio_thread_request *req)
{
	trace("(2) %d on %d", req->serial, gettid());

	pthread_mutex_lock(&req_queue_mutex);
	STAILQ_INSERT_TAIL(&req_queue, req, queue_entry);
	pthread_mutex_unlock(&req_queue_mutex);

	int err = eventfd_write(efd, 1);
	if (err < 0)
		error_errno("eventfd_write()");
}

static gboolean on_eventfd_ready(
	UNUSED GIOChannel *channel,
	UNUSED GIOCondition condition,
	UNUSED gpointer data)
{
	eventfd_t value;
	int err;

	err = eventfd_read(efd, &value);
	if (err < 0)
		error_errno("eventfd_read()");

	static STAILQ_HEAD(, virtio_thread_request) tmp_queue = STAILQ_HEAD_INITIALIZER(tmp_queue);

	pthread_mutex_lock(&req_queue_mutex);
	STAILQ_CONCAT(&tmp_queue, &req_queue);
	pthread_mutex_unlock(&req_queue_mutex);

	while(!STAILQ_EMPTY(&tmp_queue)) {
		struct virtio_thread_request *req = STAILQ_FIRST(&tmp_queue);
		STAILQ_REMOVE_HEAD(&tmp_queue, queue_entry);

		trace("(3) %d work on %d", req->serial, gettid());

		req->resp_len = virtio_request(req->buf, req->queue);
		virtio_thread_request_done(req);
	}

	gtk_gl_area_queue_render(GTK_GL_AREA(gl_area));

	return TRUE;
}

int main(int argc, char **argv)
{
	GtkWidget *window, *box;
	GIOChannel *channel;
	guint source_id;

	trace("main: %d", gettid());

	efd = eventfd(0, EFD_NONBLOCK);
	if (efd < 0) {
		error_errno("eventfd()");
		exit(EXIT_FAILURE);
	}

	channel = g_io_channel_unix_new(efd);
	source_id = g_io_add_watch(channel, G_IO_IN, on_eventfd_ready, NULL);

	int err = virtio_thread_start();
	if (err) {
		error("virtio_thread_start()");
		// FIXME
		exit(EXIT_FAILURE);
	}

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

	g_signal_connect(gl_area, "resize", G_CALLBACK(on_resize), NULL);
	g_signal_connect(G_OBJECT(window), "key-press-event", G_CALLBACK(on_key_press), NULL);

	/* Quit form main if got delete event */
	g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(gtk_main_quit), NULL);

	gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(gl_area), true);

	gtk_widget_show_all(GTK_WIDGET(window));

	gtk_main();

	virtio_thread_stop();

	g_source_remove(source_id);
	g_io_channel_unref(channel);
	close(efd);

	exit(EXIT_SUCCESS);
}
