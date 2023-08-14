/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _GUI_SUPPORT_H
#define _GUI_SUPPORT_H

#include <gtk/gtk.h>

typedef struct _Radios Radios;

extern gint		screen_width, screen_height;
/* For Xinerama */
extern GdkRectangle	*monitor_geom;
/* Smallest monitor - use for sizing windows */
extern gint		monitor_width, monitor_height;

typedef void (*HelpFunc)(gpointer data);
typedef const char *ParseFunc(gchar *line);

void gui_store_screen_geometry(GdkScreen *screen);

void gui_support_init(void);
void report_error(const char *message, ...);
void info_message(const char *message, ...);

void delayed_error(const char *error, ...);
gboolean load_file(const char *pathname, char **data_out, long *length_out);
GtkWidget *new_help_button(HelpFunc show_help, gpointer data);
void parse_file(const char *path, ParseFunc *parse_line);
gboolean get_pointer_xy(int *x, int *y);
void centre_window(GdkWindow *window, int x, int y);
void destroy_on_idle(GtkWidget *widget);
gint rox_spawn(const gchar *dir, const gchar **argv);
GtkWidget *button_new_mixed(const char *stock, const char *message);
GtkWidget *button_new_image_text(GtkWidget *image, const char *message);
void entry_set_error(GtkWidget *entry, gboolean error);
void tooltip_show(guchar *text);
void tooltip_prime(GSourceFunc callback, GObject *object);
gboolean confirm(const gchar *message, const gchar *stock, const gchar *action);

Radios *radios_new(void (*changed)(Radios *, gpointer data), gpointer data);
void radios_add(Radios *radios, const gchar *tip, gint value,
		const gchar *label, ...);
void radios_pack(Radios *radios, GtkBox *box);
void radios_set_value(Radios *radios, gint value);
gint radios_get_value(Radios *radios);
GList *uri_list_to_glist(const char *uri_list);
void make_heading(GtkWidget *label, double scale_factor);
void allow_right_click(GtkWidget *button);
GdkPixbuf *create_spotlight_pixbuf(GdkPixbuf *src, GdkColor *color);
GtkBuilder *get_gtk_builder(gchar **ids);
void add_stock_to_menu_item(GtkWidget *item, const char *stock);
gchar get_mnemonic(gchar *text, gchar *assigned);
gchar *add_mnemonic(gchar *text, gchar *assigned);


//for migration
GtkAllocation alloc(void *widget);

#define VBOX(d) GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d)))
#define STYLE(w) gtk_widget_get_style(w)
#define gdkwin(w) gtk_widget_get_window(GTK_WIDGET(w))
#define BINC(b) gtk_bin_get_child(GTK_BIN(b))

#define vadjv(c) gtk_adjustment_get_value(c->vadj)
#define vadjh(c) gtk_adjustment_get_height(c->vadj)
#define vadjpage(c) gtk_adjustment_get_page_size(c->vadj)
#define vadjupper(c) gtk_adjustment_get_upper(c->vadj)

#if GTK_MAJOR_VERSION >= 3
#define OW_CLASS(c) GTK_WIDGET_CLASS(c)
#define OWObject GObject
#define geometry(win, x, y, w, h) gdk_window_get_geometry(win, x, y, w, h);


typedef enum
{
GDK_SET,
GDK_INVERT,

} GdkFunction;
#else
#define OW_CLASS(c) GTK_OBJECT_CLASS(c)
#define OWObject GtkObject
#define geometry(win, x, y, w, h) gdk_window_get_geometry(win, x, y, w, h, NULL);
#endif



#endif /* _GUI_SUPPORT_H */
