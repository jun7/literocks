/*
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2006, Thomas Leonard and others (see changelog for details).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* gui_support.c - general (GUI) support routines */

#include "config.h"

#include <errno.h>

#include <gdk/gdkkeysyms.h>
#include <string.h>

#include "global.h"

#include "main.h"
#include "gui_support.h"
#include "support.h"

gint	screen_width, screen_height;

GdkRectangle	*monitor_geom = NULL;
gint		monitor_width, monitor_height;

static GtkWidget *current_dialog = NULL;

static GtkWidget *tip_widget = NULL;
static time_t tip_time = 0; 	/* Time tip widget last closed */
static gint tip_timeout = 0;	/* When primed */

/* Static prototypes */
static void run_error_info_dialog(GtkMessageType type, const char *message,
				  va_list args);

void gui_store_screen_geometry(GdkScreen *screen)
{
	gint mon, n_monitors;

	screen_width = gdk_screen_get_width(screen);
	screen_height = gdk_screen_get_height(screen);

	monitor_width = monitor_height = G_MAXINT;
	n_monitors = gdk_screen_get_n_monitors(screen);
	if (monitor_geom)
		g_free(monitor_geom);
	monitor_geom = g_new(GdkRectangle, n_monitors ? n_monitors : 1);

	if (n_monitors)
	{
		for (mon = 0; mon < n_monitors; ++mon)
		{
			gdk_screen_get_monitor_geometry(screen, mon,
					&monitor_geom[mon]);
			if (monitor_geom[mon].width < monitor_width)
				monitor_width = monitor_geom[mon].width;
			if (monitor_geom[mon].height < monitor_height)
				monitor_height = monitor_geom[mon].height;
		}
	}
	else
	{
		n_monitors = 1;
		monitor_geom[0].x = monitor_geom[0].y = 0;
		monitor_width = monitor_geom[0].width = screen_width;
		monitor_height = monitor_geom[0].height = screen_height;
	}

}

void gui_support_init()
{
	gpointer klass;

	gui_store_screen_geometry(gdk_screen_get_default());

	/* Work around the scrollbar placement bug */
	klass = g_type_class_ref(gtk_scrolled_window_get_type());
	((GtkScrolledWindowClass *) klass)->scrollbar_spacing = 0;
	/* (don't unref, ever) */
}

/* Open a modal dialog box showing a message.
 * The user can choose from a selection of buttons at the bottom.
 * Returns -1 if the window is destroyed, or the number of the button
 * if one is clicked (starting from zero).
 *
 * If a dialog is already open, returns -1 without waiting AND
 * brings the current dialog to the front.
 *
 * Each button has two arguments, a GTK_STOCK icon and some text. If the
 * text is NULL, the stock's text is used.
 */
static int get_choice(const char *title,
	       const char *message,
	       int number_of_buttons, ...)
{
	GtkWidget	*dialog;
	GtkWidget	*button = NULL;
	int		i, retval;
	va_list	ap;

	if (current_dialog)
	{
		gtk_widget_hide(current_dialog);
		gtk_widget_show(current_dialog);
		return -1;
	}

	current_dialog = dialog = gtk_message_dialog_new(NULL,
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_QUESTION,
					GTK_BUTTONS_NONE,
					"%s", message);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

	va_start(ap, number_of_buttons);

	for (i = 0; i < number_of_buttons; i++)
	{
		const char *stock = va_arg(ap, char *);
		const char *text = va_arg(ap, char *);

		if (text)
			button = button_new_mixed(stock, text);
		else
			button = gtk_button_new_from_stock(stock);

		gtk_widget_set_can_default(button, TRUE);
		gtk_widget_show(button);

		gtk_dialog_add_action_widget(GTK_DIALOG(current_dialog),
						button, i);
	}

	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

	gtk_dialog_set_default_response(GTK_DIALOG(dialog), i - 1);

	va_end(ap);

	retval = gtk_dialog_run(GTK_DIALOG(dialog));
	if (retval == GTK_RESPONSE_NONE)
		retval = -1;
	gtk_widget_destroy(dialog);

	current_dialog = NULL;

	return retval;
}

void info_message(const char *message, ...)
{
        va_list args;

	va_start(args, message);

	run_error_info_dialog(GTK_MESSAGE_INFO, message, args);
}

/* Display a message in a window with "ROX-Filer" as title */
void report_error(const char *message, ...)
{
	va_list args;

	va_start(args, message);

	run_error_info_dialog(GTK_MESSAGE_ERROR, message, args);
}


static gboolean error_idle_cb(gpointer data)
{
	char	**error = (char **) data;

	report_error("%s", *error);
	null_g_free(error);

	one_less_window();
	return FALSE;
}

/* Display an error with "ROX-Filer" as title next time we are idle.
 * If multiple errors are reported this way before the window is opened,
 * all are displayed in a single window.
 * If an error is reported while the error window is open, it is discarded.
 */
void delayed_error(const char *error, ...)
{
	static char *delayed_error_data = NULL;
	char *old, *new;
	va_list args;

	g_return_if_fail(error != NULL);

	old = delayed_error_data;

	va_start(args, error);
	new = g_strdup_vprintf(error, args);
	va_end(args);

	if (old)
	{
		delayed_error_data = g_strconcat(old,
				_("\n---\n"),
				new, NULL);
		g_free(old);
		g_free(new);
	}
	else
	{
		delayed_error_data = new;
		g_idle_add(error_idle_cb, &delayed_error_data);

		number_of_windows++;
	}
}

/* Load the file into memory. Return TRUE on success.
 * Block is zero terminated (but this is not included in the length).
 */
gboolean load_file(const char *pathname, char **data_out, long *length_out)
{
	gsize len;
	GError *error = NULL;

	if (!g_file_get_contents(pathname, data_out, &len, &error))
	{
		delayed_error("%s", error->message);
		g_error_free(error);
		return FALSE;
	}

	if (length_out)
		*length_out = len;
	return TRUE;
}

GtkWidget *new_help_button(HelpFunc show_help, gpointer data)
{
	GtkWidget	*b, *icon;

	b = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
	icon = gtk_image_new_from_stock(GTK_STOCK_HELP,
					GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_container_add(GTK_CONTAINER(b), icon);
	g_signal_connect_swapped(b, "clicked", G_CALLBACK(show_help), data);

	gtk_widget_set_can_focus(b, FALSE);

	return b;
}

/* Read file into memory. Call parse_line(guchar *line) for each line
 * in the file. Callback returns NULL on success, or an error message
 * if something went wrong. Only the first error is displayed to the user.
 */
void parse_file(const char *path, ParseFunc *parse_line)
{
	char		*data;
	long		length;
	gboolean	seen_error = FALSE;

	if (load_file(path, &data, &length))
	{
		char *eol;
		const char *error;
		char *line = data;
		int  line_number = 1;

		if (strncmp(data, "<?xml ", 6) == 0)
		{
			delayed_error(_("Attempt to read an XML file as "
					"a text file. File '%s' may be "
					"corrupted."), path);
			return;
		}

		while (line && *line)
		{
			eol = strchr(line, '\n');
			if (eol)
				*eol = '\0';

			error = parse_line(line);

			if (error && !seen_error)
			{
				delayed_error(
		_("Error in '%s' file at line %d: "
		"\n\"%s\"\n"
		"This may be due to upgrading from a previous version of "
		APPNAME". Open the Options window and try changing something "
		"and then changing it back (causing the file to be resaved).\n"
		"Further errors will be ignored."),
					path,
					line_number,
					error);
				seen_error = TRUE;
			}

			if (!eol)
				break;
			line = eol + 1;
			line_number++;
		}
		g_free(data);
	}
}

/* Returns the position of the pointer.
 * TRUE if any modifier keys or mouse buttons are pressed.
 */
gboolean get_pointer_xy(int *x, int *y)
{
	unsigned int mask;

	gdk_window_get_pointer(NULL, x, y, &mask);

	return mask != 0;
}


#define DECOR_BORDER 32

/* Centre the window at these coords */
void centre_window(GdkWindow *window, int x, int y)
{
	int	w, h;
	int m;

	g_return_if_fail(window != NULL);

	m = gdk_screen_get_monitor_at_point(gdk_screen_get_default(), x, y);
	w = gdk_window_get_width(window);
	h = gdk_window_get_height(window);

	x -= w / 2;
	y -= h / 2;

	gdk_window_move(window,
		CLAMP(x, DECOR_BORDER + monitor_geom[m].x,
			monitor_geom[m].x + monitor_geom[m].width
			- w - DECOR_BORDER),
		CLAMP(y, DECOR_BORDER + monitor_geom[m].y,
			monitor_geom[m].y + monitor_geom[m].height
			- h - DECOR_BORDER));
}

static void run_error_info_dialog(GtkMessageType type, const char *message,
				  va_list args)
{
	GtkWidget *dialog;
	gchar *s;

	g_return_if_fail(message != NULL);

	s = g_strdup_vprintf(message, args);
	va_end(args);

	dialog = gtk_message_dialog_new(NULL,
					GTK_DIALOG_MODAL,
					type,
					GTK_BUTTONS_OK,
					"%s", s);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	g_free(s);
}


static gboolean idle_destroy_cb(GtkWidget *widget)
{
	g_object_unref(widget);
	gtk_widget_destroy(widget);
	return FALSE;
}

/* Destroy the widget in an idle callback */
void destroy_on_idle(GtkWidget *widget)
{
	g_object_ref(widget);
	g_idle_add((GSourceFunc) idle_destroy_cb, widget);
}

/* Spawn a child process (as spawn_full), and report errors.
 * Returns the child's PID on succes, or 0 on failure.
 */
gint rox_spawn(const gchar *dir, const gchar **argv)
{
	GError	*error = NULL;
	gint pid = 0;

	if (!g_spawn_async_with_pipes(dir, (gchar **) argv, NULL,
			G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL |
			G_SPAWN_SEARCH_PATH,
			NULL, NULL,		/* Child setup fn */
			&pid,			/* Child PID */
			NULL, NULL, NULL,	/* Standard pipes */
			&error))
	{
		delayed_error("%s", error ? error->message : "(null)");
		g_error_free(error);

		return 0;
	}

	return pid;
}

GtkWidget *button_new_image_text(GtkWidget *image, const char *message)
{
	GtkWidget *button, *align, *hbox, *label;

	button = gtk_button_new();
	label = gtk_label_new_with_mnemonic(message);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), button);

	hbox = gtk_hbox_new(FALSE, 2);

	align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);

	gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(button), align);
	gtk_container_add(GTK_CONTAINER(align), hbox);
	gtk_widget_show_all(align);

	return button;
}

GtkWidget *button_new_mixed(const char *stock, const char *message)
{
	return button_new_image_text(gtk_image_new_from_stock(stock,
					       GTK_ICON_SIZE_BUTTON),
					message);
}

/* Highlight entry in red if 'error' is TRUE */
void entry_set_error(GtkWidget *entry, gboolean error)
{
	const GdkColor red = {0, 0xffff, 0, 0};
	const GdkColor white = {0, 0xffff, 0xffff, 0xffff};

	gtk_widget_modify_text(entry, GTK_STATE_NORMAL, error ? &red : NULL);
	gtk_widget_modify_base(entry, GTK_STATE_NORMAL, error ? &white : NULL);
}



/* Draw the black border */
static gint tooltip_draw(GtkWidget *w)
{
#if GTK_MAJOR_VERSION >= 3
#else
	gdk_draw_rectangle(w->window, w->style->fg_gc[w->state], FALSE, 0, 0,
			alloc(w).width - 1, alloc(w).height - 1);
#endif

	return FALSE;
}

/* When the tips window closed, record the time. If we try to open another
 * tip soon, it will appear more quickly.
 */
static void tooltip_destroyed(gpointer data)
{
	time(&tip_time);
}

/* Display a tooltip-like widget near the pointer with 'text'. If 'text' is
 * NULL, close any current tooltip.
 */
void tooltip_show(guchar *text)
{
	GtkWidget *label;
	int	x, y, py;
	int	w, h;
	int m;

	if (tip_timeout)
	{
		g_source_remove(tip_timeout);
		tip_timeout = 0;
	}

	if (tip_widget)
	{
		gtk_widget_destroy(tip_widget);
		tip_widget = NULL;
	}

	if (!text)
		return;

	/* Show the tip */
	tip_widget = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_widget_set_app_paintable(tip_widget, TRUE);
	gtk_widget_set_name(tip_widget, "gtk-tooltips");

	g_signal_connect_swapped(tip_widget, "expose_event",
			G_CALLBACK(tooltip_draw), tip_widget);

	label = gtk_label_new(text);
	gtk_misc_set_padding(GTK_MISC(label), 4, 2);
	gtk_container_add(GTK_CONTAINER(tip_widget), label);
	gtk_widget_show(label);
	gtk_widget_realize(tip_widget);

	w = alloc(tip_widget).width;
	h = alloc(tip_widget).height;
	gdk_window_get_pointer(NULL, &x, &py, NULL);

	m = gdk_screen_get_monitor_at_point(gdk_screen_get_default(), x, py);

	x -= w / 2;
	y = py + 12; /* I don't know the pointer height so I use a constant */

	/* Now check for screen boundaries */
	x = CLAMP(x, monitor_geom[m].x,
			monitor_geom[m].x + monitor_geom[m].width - w);
	y = CLAMP(y, monitor_geom[m].y,
			monitor_geom[m].y + monitor_geom[m].height - h);

	/* And again test if pointer is over the tooltip window */
	if (py >= y && py <= y + h)
		y = py - h - 2;
	gtk_window_move(GTK_WINDOW(tip_widget), x, y);
	gtk_widget_show(tip_widget);

	g_signal_connect_swapped(tip_widget, "destroy",
			G_CALLBACK(tooltip_destroyed), NULL);
	time(&tip_time);
}

/* Call callback(user_data) after a while, unless cancelled.
 * Object is refd now and unref when cancelled / after callback called.
 */
void tooltip_prime(GSourceFunc callback, GObject *object)
{
	time_t  now;
	int	delay;

	g_return_if_fail(tip_timeout == 0);

	time(&now);
	delay = now - tip_time > 2 ? 1000 : 200;

	g_object_ref(object);
	tip_timeout = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE,
					 delay,
					 callback,
					 object,
					 g_object_unref);
}


/* Confirm the action with the user. If action is NULL, the text from stock
 * is used.
 */
gboolean confirm(const gchar *message, const gchar *stock, const gchar *action)
{
	return get_choice(APPNAME, message, 2,
			  GTK_STOCK_CANCEL, NULL,
			  stock, action) == 1;
}

struct _Radios {
	GList *widgets;

	void (*changed)(Radios *, gpointer data);
	gpointer changed_data;
};

/* Create a new set of radio buttons.
 * Use radios_add to add options, then radios_pack to put them into something.
 * The radios object will self-destruct with the first widget it contains.
 * changed(data) is called (if not NULL) when pack is called, and on any
 * change after that.
 */
Radios *radios_new(void (*changed)(Radios *, gpointer data), gpointer data)
{
	Radios *radios;

	radios = g_new(Radios, 1);

	radios->widgets = NULL;
	radios->changed = changed;
	radios->changed_data = data;

	return radios;
}

static void radios_free(GtkWidget *radio, Radios *radios)
{
	g_return_if_fail(radios != NULL);

	g_list_free(radios->widgets);
	g_free(radios);
}

void radios_add(Radios *radios, const gchar *tip, gint value,
		const gchar *label, ...)
{
	GtkWidget *radio;
	GSList *group = NULL;
	gchar *s;
	va_list args;

	g_return_if_fail(radios != NULL);
	g_return_if_fail(label != NULL);

	va_start(args, label);
	s = g_strdup_vprintf(label, args);
	va_end(args);

	if (radios->widgets)
	{
		GtkRadioButton *first = GTK_RADIO_BUTTON(radios->widgets->data);
		group = gtk_radio_button_get_group(first);
	}

	radio = gtk_radio_button_new_with_label(group, s);
	gtk_label_set_line_wrap(GTK_LABEL(gtk_bin_get_child(GTK_BIN(radio))), TRUE);
	gtk_widget_show(radio);
	if (tip)
		gtk_widget_set_tooltip_text(radio, tip);
	if (!group)
		g_signal_connect(G_OBJECT(radio), "destroy",
				G_CALLBACK(radios_free), radios);

	radios->widgets = g_list_prepend(radios->widgets, radio);
	g_object_set_data(G_OBJECT(radio), "rox-radios-value",
			  GINT_TO_POINTER(value));
}

static void radio_toggled(GtkToggleButton *button, Radios *radios)
{
	g_return_if_fail(radios != NULL);

	if (button && !gtk_toggle_button_get_active(button))
		return;	/* Stop double-notifies */

	if (radios->changed)
		radios->changed(radios, radios->changed_data);
}

void radios_pack(Radios *radios, GtkBox *box)
{
	GList *next;

	g_return_if_fail(radios != NULL);

	for (next = g_list_last(radios->widgets); next; next = next->prev)
	{
		GtkWidget *button = GTK_WIDGET(next->data);

		gtk_box_pack_start(box, button, FALSE, TRUE, 0);
		g_signal_connect(button, "toggled",
				G_CALLBACK(radio_toggled), radios);
	}
	radio_toggled(NULL, radios);
}

void radios_set_value(Radios *radios, gint value)
{
	GList *next;

	g_return_if_fail(radios != NULL);

	for (next = radios->widgets; next; next = next->next)
	{
		GtkToggleButton *radio = GTK_TOGGLE_BUTTON(next->data);
		int radio_value;

		radio_value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(radio),
						"rox-radios-value"));

		if (radio_value == value)
		{
			gtk_toggle_button_set_active(radio, TRUE);
			return;
		}
	}

	g_warning("Value %d not in radio group!", value);
}

gint radios_get_value(Radios *radios)
{
	GList *next;

	g_return_val_if_fail(radios != NULL, -1);

	for (next = radios->widgets; next; next = next->next)
	{
		GtkToggleButton *radio = GTK_TOGGLE_BUTTON(next->data);

		if (gtk_toggle_button_get_active(radio))
			return GPOINTER_TO_INT(g_object_get_data(
					G_OBJECT(radio), "rox-radios-value"));
	}

	g_warning("Nothing in the radio group is selected!");

	return -1;
}

/* Convert a list of URIs as a string into a GList of EscapedPath URIs.
 * No unescaping is done.
 * Lines beginning with # are skipped.
 * The text block passed in is zero terminated (after the final CRLF)
 */
GList *uri_list_to_glist(const char *uri_list)
{
	GQueue gq = G_QUEUE_INIT;

	while (*uri_list)
	{
		char	*linebreak;
		int	length;

		linebreak = strchr(uri_list, 13);

		if (!linebreak || linebreak[1] != 10)
		{
			/* If this is the first, append it anyway (Firefox
			 * 3.5) */
			if (!gq.length && uri_list[0] != '#')
				g_queue_push_tail(&gq, g_strdup(uri_list));
			else
				g_warning("uri_list_to_glist: %s",
						_("Incorrect or missing line "
							"break in text/uri-list data"));
			return gq.head;
		}

		length = linebreak - uri_list;

		if (length && uri_list[0] != '#')
			g_queue_push_tail(&gq, g_strndup(uri_list, length));

		uri_list = linebreak + 2;
	}

	return gq.head;
}

/* Make the name bolder and larger.
 * scale_factor can be PANGO_SCALE_X_LARGE, etc.
 */
void make_heading(GtkWidget *label, double scale_factor)
{
	PangoAttribute *attr;
	PangoAttrList *list;

	list = pango_attr_list_new();

	attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
	attr->start_index = 0;
	attr->end_index = -1;
	pango_attr_list_insert(list, attr);

	attr = pango_attr_scale_new(scale_factor);
	attr->start_index = 0;
	attr->end_index = -1;
	pango_attr_list_insert(list, attr);

	gtk_label_set_attributes(GTK_LABEL(label), list);
	pango_attr_list_unref(list);
}

static gint button3_button_pressed(GtkButton *button,
				GdkEventButton *event,
				gpointer date)
{
	if (event->button == 3)
	{
		gtk_grab_add(GTK_WIDGET(button));
		gtk_button_pressed(button);

		return TRUE;
	}

	return FALSE;
}

static gint button3_button_released(GtkButton *button,
				GdkEventButton *event,
				FilerWindow *filer_window)
{
	if (event->button == 3)
	{
		gtk_grab_remove(GTK_WIDGET(button));
		gtk_button_released(button);

		return TRUE;
	}

	return FALSE;
}

void allow_right_click(GtkWidget *button)
{
	g_signal_connect(button, "button_press_event",
		G_CALLBACK(button3_button_pressed), NULL);
	g_signal_connect(button, "button_release_event",
		G_CALLBACK(button3_button_released), NULL);
}

/* Create a new pixbuf by colourizing 'src' to 'color'. If the function fails,
 * 'src' will be returned (with an increased reference count, so it is safe to
 * g_object_unref() the return value whether the function fails or not).
 */
GdkPixbuf *create_spotlight_pixbuf(GdkPixbuf *src, GdkColor *color)
{
	guchar opacity = 88;//127;//192;
	guchar alpha = 255 - opacity;
	GdkPixbuf *dst;
	GdkColorspace colorspace;
	int width, height, src_rowstride, dst_rowstride, x, y;
	int n_channels, bps;
	int r, g, b;
	guchar *spixels, *dpixels, *src_pixels, *dst_pixels;
	gboolean has_alpha;

	has_alpha = gdk_pixbuf_get_has_alpha(src);
	colorspace = gdk_pixbuf_get_colorspace(src);
	n_channels = gdk_pixbuf_get_n_channels(src);
	bps = gdk_pixbuf_get_bits_per_sample(src);

	if ((colorspace != GDK_COLORSPACE_RGB) ||
	    (!has_alpha && n_channels != 3) ||
	    (has_alpha && n_channels != 4) ||
	    (bps != 8))
		goto error;

	width = gdk_pixbuf_get_width(src);
	height = gdk_pixbuf_get_height(src);

	dst = gdk_pixbuf_new(colorspace, has_alpha, bps, width, height);
	if (dst == NULL)
		goto error;

	src_pixels = gdk_pixbuf_get_pixels(src);
	dst_pixels = gdk_pixbuf_get_pixels(dst);
	src_rowstride = gdk_pixbuf_get_rowstride(src);
	dst_rowstride = gdk_pixbuf_get_rowstride(dst);

	if (color == NULL)
		goto error;
	r = opacity * (color->red >> 8);
	g = opacity * (color->green >> 8);
	b = opacity * (color->blue >> 8);

	for (y = 0; y < height; y++)
	{
		spixels = src_pixels + y * src_rowstride;
		dpixels = dst_pixels + y * dst_rowstride;
		for (x = 0; x < width; x++)
		{
			*dpixels++ = (*spixels++ * alpha + r) >> 8;
			*dpixels++ = (*spixels++ * alpha + g) >> 8;
			*dpixels++ = (*spixels++ * alpha + b) >> 8;
			if (has_alpha)
				*dpixels++ = *spixels++;
		}

	}
	return dst;

error:
	g_object_ref(src);
	return src;
}

/* Load the Templates.ui file and build a component. */
GtkBuilder *get_gtk_builder(gchar **ids)
{
	GError	*error = NULL;
	char *path;
	GtkBuilder *builder = NULL;

	builder = gtk_builder_new();
	gtk_builder_set_translation_domain(builder, APPNAME);

	path = g_build_filename(app_dir, "Templates.ui", NULL);
	if (!gtk_builder_add_objects_from_file(builder, path, ids, &error))
	{
		g_warning("Failed to load builder file %s: %s",
				path, error->message);
		g_error_free(error);
	}

	g_free(path);

	return builder;
}

void add_stock_to_menu_item(GtkWidget *item, const char *stock)
{
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
			gtk_image_new_from_stock(stock, GTK_ICON_SIZE_MENU));
}

/* assigned needs be filled NULL and length is 27 */
gchar get_mnemonic(gchar *text, gchar *assigned)
{
	gchar * tmp, *t;
	gchar ret = '\0';

	t = strdup(text);
	tmp = t;
	while ((tmp = g_strstr_len(tmp, -1, "__")) != NULL)
		tmp[0] = tmp[1] = 'a';

	tmp = g_strstr_len(t, -1, "_");
	if (tmp && strlen(tmp) >= 2)
		ret = g_ascii_tolower(tmp[1]);

	g_free(t);

	if (assigned &&
		ret != '\0' &&
		ret >= GDK_KEY_a &&
		ret <= GDK_KEY_z)
	{
		for (; ; assigned+=1)
		{
			if (assigned[0] == '\0')
				assigned[0] = ret;
			if (assigned[0] == ret)
				break;
		}
	}

	return ret;
}
/* return val is static.
 * assigned needs be filled NULL and length is 27 */
gchar *add_mnemonic(gchar *text, gchar *assigned)
{
	static gchar *ret;
	const gchar *as = assigned;
	gchar *tmp;
	gchar lc;
	int rlen, alen, i, j, k = 0;
	gboolean found;

	g_free(ret);
	ret = strdup(text);

	rlen = strlen(ret);
	alen = strlen(as);
	for (i = 0; i < rlen; i++)
		if (ret[i] == '_')
		{
			ret[i] = '\0';
			tmp = ret;
			i ++;
			ret = g_strconcat(ret, "__", ret + i, NULL);
			rlen ++;
			g_free(tmp);
		}

	/* If there is no unique then first [a-z] */
	while (k < 2)
	{
		for (i = 0; i < rlen; i++)
		{
			lc = g_ascii_tolower(ret[i]);
			found = FALSE;
			if (k == 0)
				for (j = 0; j < alen; j++)
					if (as[j] == lc)
					{
						found = TRUE;
						break;
					}

			if (!found &&
				lc >= GDK_KEY_a &&
				lc <= GDK_KEY_z)
			{
				if (k ==0)
					assigned[alen] = lc;

				tmp = g_strndup(ret, i);
				ret = g_strconcat(tmp, "_", ret + i, NULL);
				g_free(tmp);

				k++;
				break;
			}
		}

		k++;
	}

	return ret;
}

GtkAllocation alloc(void *widget)
{
	GtkAllocation allocation;
	gtk_widget_get_allocation(widget, &allocation);
	return allocation;
}
