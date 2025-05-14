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

/* pixmaps.c - code for handling pixbufs (despite the name!) */

#include "config.h"
#define PIXMAPS_C

/* Remove pixmaps from the cache when they haven't been accessed for
 * this period of time (seconds).
 */

#define PIXMAP_PURGE_TIME 60 * 60 * 4
#define PIXMAP_THUMB_SIZE  256
#define PIXMAP_THUMB_TOO_OLD_TIME  5

#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include "global.h"

#include "fscache.h"
#include "support.h"
#include "gui_support.h"
#include "pixmaps.h"
#include "main.h"
#include "dir.h"
#include "diritem.h"
#include "choices.h"
#include "options.h"
#include "action.h"
#include "type.h"

GFSCache *pixmap_cache = NULL;

static GHashTable *orders = NULL;

static const char * bad_xpm[] = {
"12 12 3 1",
" 	c #000000000000",
".	c #FFFF00000000",
"x	c #FFFFFFFFFFFF",
"            ",
" ..xxxxxx.. ",
" ...xxxx... ",
" x...xx...x ",
" xx......xx ",
" xxx....xxx ",
" xxx....xxx ",
" xx......xx ",
" x...xx...x ",
" ...xxxx... ",
" ..xxxxxx.. ",
"            "};

MaskedPixmap *im_error;
MaskedPixmap *im_unknown;

MaskedPixmap *im_appdir;

MaskedPixmap *im_dirs;

GtkIconSize mount_icon_size = -1;

int small_height = 0;
int small_width = 0;
int thumb_size = PIXMAP_THUMB_SIZE;

gchar *thumb_dir = "normal";

Option o_pixmap_thumb_file_size;
Option o_video_thumbnailer;
Option o_purge_days;


typedef struct _ChildThumbnail ChildThumbnail;

/* There is one of these for each active child process */
struct _ChildThumbnail {
	gchar	 *path;
	gchar	 *rpath;
	struct timespec orgtime;
	GFunc	 callback;
	gpointer data;
	pid_t	 child;
	guint	 timeout;
	guint	 order;
};
static guint next_order = 0;

static const char *stocks[] = {
	ROX_STOCK_SHOW_DETAILS,
	ROX_STOCK_SHOW_HIDDEN,
	ROX_STOCK_MOUNT,
	ROX_STOCK_MOUNTED,
	ROX_STOCK_SYMLINK,
	ROX_STOCK_XATTR,
};

/* Static prototypes */

static void load_default_pixmaps(void);
static gint purge_pixmaps(gpointer data);
static MaskedPixmap *image_from_file(const char *path);
static MaskedPixmap *get_bad_image(void);
static GdkPixbuf *get_thumbnail_for(const char *path, gboolean *forcheck);
static void ordered_update(ChildThumbnail *info);
static void thumbnail_done(ChildThumbnail *info);
static void create_thumbnail(const gchar *path, MIME_type *type);
static GList *thumbs_purge_cache(Option *option, xmlNode *node, guchar *label);
static gchar *thumb_path_mk(const gchar *path);
static gchar *thumbnail_program(MIME_type *type);
static void make_dir_thumb(const gchar *path);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/
static char *thumbdirpath;
static const char *thumbdir()
{
	return thumbdirpath ?: (thumbdirpath =
		g_build_filename(g_get_user_cache_dir(), "thumbnails", thumb_dir, NULL));
}
static void set_thumb_size()
{
	switch (thumb_size = o_pixmap_thumb_file_size.int_value) {
	case 512:
		thumb_dir = "huge";
		break;
	case 256:
		thumb_dir = "large";
		break;
	case 128:
		thumb_dir = "normal";
		break;
	case 64:
		thumb_dir = "small";
		break;
	default:
		thumb_dir = "fail";
	}

	g_free(thumbdirpath);
	thumbdirpath = NULL;
}
static void options_changed()
{
	if (o_pixmap_thumb_file_size.has_changed)
		set_thumb_size();
}

void pixmaps_init(void)
{
	GtkIconFactory *factory;
	int i;

	option_add_int(&o_pixmap_thumb_file_size, "thumb_file_size", PIXMAP_THUMB_SIZE);
	option_add_string(&o_video_thumbnailer, "video_thumbnailer", "ffmpegthumbnailer -i \"$1\" -o \"$2\" -s $3");
	option_add_int(&o_purge_days, "purge_days", 90);
	option_add_notify(options_changed);

#if GTK_MAJOR_VERSION >= 3
#else
	gtk_widget_push_colormap(gdk_rgb_get_colormap());
#endif

	pixmap_cache = g_fscache_new((GFSLoadFunc) image_from_file, NULL, NULL);
	orders = g_hash_table_new_full(
			g_str_hash, g_str_equal, g_free, NULL);

	g_timeout_add(PIXMAP_PURGE_TIME / 2 * 1000, purge_pixmaps, NULL);

	factory = gtk_icon_factory_new();
	for (i = 0; i < G_N_ELEMENTS(stocks); i++)
	{
		GdkPixbuf *pixbuf;
		GError *error = NULL;
		gchar *path;
		GtkIconSet *iset;
		const gchar *name = stocks[i];

		path = g_strconcat(app_dir, "/images/", name, ".png", NULL);
		pixbuf = gdk_pixbuf_new_from_file(path, &error);
		if (!pixbuf)
		{
			g_warning("%s", error->message);
			g_error_free(error);
			pixbuf = gdk_pixbuf_new_from_xpm_data(bad_xpm);
		}
		g_free(path);

		iset = gtk_icon_set_new_from_pixbuf(pixbuf);
		g_object_unref(G_OBJECT(pixbuf));
		gtk_icon_factory_add(factory, name, iset);
		gtk_icon_set_unref(iset);
	}
	gtk_icon_factory_add_default(factory);

	mount_icon_size = gtk_icon_size_register("rox-mount-size", 14, 14);

	load_default_pixmaps();
	set_thumb_size();
	option_register_widget("thumbs-purge-cache", thumbs_purge_cache);
}

/* Load image <appdir>/images/name.png.
 * Always returns with a valid image.
 */
MaskedPixmap *load_pixmap(const char *name)
{
	guchar *path;
	MaskedPixmap *retval;

	path = g_strconcat(app_dir, "/images/", name, ".png", NULL);
	retval = image_from_file(path);
	g_free(path);

	if (!retval)
		retval = get_bad_image();

	return retval;
}

/* Create a MaskedPixmap from a GTK stock ID. Always returns
 * a valid image.
 */
static MaskedPixmap *mp_from_stock(const char *stock_id, int size)
{
	GtkIconSet *icon_set;
	GdkPixbuf  *pixbuf;
	MaskedPixmap *retval;

	/*icon_set = gtk_icon_factory_lookup_default(stock_id);*/
	icon_set = gtk_style_lookup_icon_set(gtk_widget_get_default_style(),
					     stock_id);
	if (!icon_set)
		return get_bad_image();

	pixbuf = gtk_icon_set_render_icon(icon_set,
                                     gtk_widget_get_default_style(), /* Gtk bug */
                                     GTK_TEXT_DIR_LTR,
                                     GTK_STATE_NORMAL,
                                     size,
                                     NULL,
                                     NULL);
	retval = masked_pixmap_new(pixbuf);
	g_object_unref(pixbuf);

	return retval;
}

void pixmap_make_small(MaskedPixmap *mp)
{
	if (mp->sm_pixbuf)
		return;

	g_return_if_fail(mp->src_pixbuf != NULL);

	mp->sm_pixbuf = scale_pixbuf(mp->src_pixbuf,
					small_width, small_height);

	if (!mp->sm_pixbuf)
	{
		mp->sm_pixbuf = mp->src_pixbuf;
		g_object_ref(mp->sm_pixbuf);
	}

	mp->sm_width = gdk_pixbuf_get_width(mp->sm_pixbuf);
	mp->sm_height = gdk_pixbuf_get_height(mp->sm_pixbuf);
}

/* -1:not thumb target 0:not created 1:created and loaded */
gint pixmap_check_thumb(const gchar *rpath)
{
	gboolean check = TRUE;
	get_thumbnail_for(rpath, &check);

	if (check)
		return 1;

	MIME_type *type = type_from_path(rpath);
	if (type)
	{
		gchar *thumb_prog = NULL;
		if (!strcmp(type->media_type, "image")
		|| (!strcmp(type->media_type, "video") && *o_video_thumbnailer.value)
		|| (thumb_prog = thumbnail_program(type)))
		{
			g_free(thumb_prog);
			return 0;
		}
	}

	return -1;
}


static int thumb_prog_timeout(ChildThumbnail *info)
{
	info->timeout = 0;
	kill(info->child, 9);
	return FALSE;
}

/* Load image 'path' in the background and insert into pixmap_cache.
 * Call callback(data, path) when done (path is NULL => error).
 * If the image is already uptodate, or being created already, calls the
 * callback right away.
 */
void pixmap_background_thumb(const char *path, GFunc callback, gpointer data)
{
	static guint ordered_num;

	pid_t		child;
	ChildThumbnail *info;
	MIME_type      *type;
	gchar          *thumb_prog;

	static char *rpath;
	g_free(rpath);
	rpath = pathdup(path);

	gboolean check = TRUE;
	get_thumbnail_for(rpath, &check);

	if (check)
	{
		callback(data, (gpointer)path);
		return;
	}

	/* Is it currently being created? */
	if (g_hash_table_lookup(orders, rpath))
	{/* Thumbnail is known, or being created */
		callback(data, NULL);
		//append to last for sym links what sharing the thumb
		info = g_new0(ChildThumbnail, 1);
		info->path = g_strdup(path);
		info->rpath = g_strdup(rpath);
		info->order = ordered_num++;
		ordered_update(info);
		return;
	}
	g_hash_table_add(orders, g_strdup(rpath));

	/* Not in memory, nor in the thumbnails directory.  We need to
	 * generate it */

	type = type_from_path(rpath);
	if (!type)
		type = text_plain;

	thumb_prog = thumbnail_program(type);


	/* Only attempt to load 'images' types ourselves */
	if (thumb_prog == NULL
	&&  strcmp(type->media_type, "image")
	&& (strcmp(type->media_type, "video") || !*o_video_thumbnailer.value)
	) {
		callback(data, NULL);
		return;		/* Don't know how to handle this type */
	}

	struct stat orginfo;
	if (mc_stat(path, &orginfo) != 0)
	{
		callback(data, NULL);
		return;
	}

	info = g_new(ChildThumbnail, 1);
	info->path = g_strdup(path);
	info->rpath = g_strdup(rpath);
	info->orgtime = orginfo.st_ctim;
	info->callback = callback;
	info->data = data;
	info->timeout = 0;
	info->order = ordered_num++;

	child = fork();
	if (child == -1)
	{
		g_free(thumb_prog);
		delayed_error("fork(): %s", g_strerror(errno));
		callback(data, NULL);
		return;
	}

	if (child == 0)
	{
		/* We are the child process.  (We are sloppy with freeing
		   memory, but since we go away very quickly, that's ok.) */
		if (thumb_prog)
		{
			execl(thumb_prog, thumb_prog, rpath,
					thumb_path_mk(rpath),
					g_strdup_printf("%d", thumb_size),
					NULL);

			_exit(1);
		}

		if (!strcmp(type->media_type, "video") && *o_video_thumbnailer.value)
		{
			execlp("sh", "sh", "-c", o_video_thumbnailer.value, "sh",
					rpath,
					thumb_path_mk(rpath),
					g_strdup_printf("%d", thumb_size),
					NULL);

			_exit(1);
		}
		else
			create_thumbnail(rpath, type);
		_exit(0);
	}

	g_free(thumb_prog);
	info->child = child;
	info->timeout = g_timeout_add_seconds(14,
			(GSourceFunc) thumb_prog_timeout, info);
	on_child_death(child, (CallbackFn) thumbnail_done, info);
}

/*
 * Return the thumbnail for a file, only if available.
 */
GdkPixbuf *pixmap_try_thumb(const gchar *rpath)
{
	gboolean b = FALSE;
	return get_thumbnail_for(rpath, &b);
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Create a thumbnail file for this image */
static void save_thumbnail(const char *pathname, GdkPixbuf *full)
{
	GdkPixbuf *thumb = scale_pixbuf(full, thumb_size, thumb_size);

	gchar *path = pathdup(pathname);
	char *thumbpath = thumb_path_mk(path);
	g_free(path);

	mode_t old_mask = umask(0077);
	//At least we don't need extensions being '.jpg'
	gdk_pixbuf_save(thumb, thumbpath, "jpeg", NULL,
			"quality", "77",
			NULL);
	umask(old_mask);

	g_object_unref(thumb);
	g_free(thumbpath);
}

static gchar *_thumb_path(const char *path, bool mkdirif)
{
	gchar *uri, *md5;
	gchar *ans;

	uri = g_filename_to_uri(path, NULL, NULL);
	if(!uri)
	       uri = g_strconcat("file://", path, NULL);
	md5 = md5_hash(uri);
	g_free(uri);

	const char *dir = thumbdir();
	ans = g_strdup_printf("%s/%s.jpg", dir, md5);
	g_free(md5);

	if (mkdirif && !g_file_test(dir, G_FILE_TEST_EXISTS))
		g_mkdir_with_parents(dir, 0700);

	return ans; /* This return is used unlink! Be carefull */
}
static gchar *thumb_path_mk(const char *path)
{
	return _thumb_path(path, true);
}

/* Return a program to create thumbnails for files of this type.
 * NULL to try to make it ourself (using gdk).
 * g_free the result.
 */
static gchar *thumbnail_program(MIME_type *type)
{
	gchar *leaf;
	gchar *path;

	if (!type)
		return NULL;

	leaf = g_strconcat(type->media_type, "_", type->subtype, NULL);
	path = choices_find_xdg_path_load(leaf, "MIME-thumb");
	g_free(leaf);
	if (path)
	{
		return path;
	}

	path = choices_find_xdg_path_load(type->media_type, "MIME-thumb");

	return path;
}

/* Load path and create the thumbnail
 * file. Parent will notice when we die.
 */
static void create_thumbnail(const gchar *path, MIME_type *type)
{
	//rox_pixbuf can make image from files being load
	GdkPixbuf *image = gdk_pixbuf_new_from_file_at_scale(path,
			thumb_size, thumb_size, TRUE, NULL);

	if (image)
	{
		save_thumbnail(path, image);
		g_object_unref(image);
	}
}

char *pixmap_make_thumb_path(const char *path)
{
	return _thumb_path(path, false);
}

static void make_dir_thumb(const gchar *path)
{
	gchar *dir = g_path_get_dirname(path);
	gchar *dir_thumb_path = pixmap_make_thumb_path(dir);
	GdkPixbuf *image = gdk_pixbuf_new_from_file(dir_thumb_path, NULL);
	if (image)
	{
		g_object_unref(image);
	}
	else
	{
		unlink(dir_thumb_path); //////////////////////////

		char *thumb_path = pixmap_make_thumb_path(path);
		char *rel_path = get_relative_path(dir_thumb_path, thumb_path);

		if (symlink(rel_path, dir_thumb_path) == 0)
			dir_force_update_path(dir, TRUE);

		g_free(rel_path);
		g_free(thumb_path);
	}
	g_free(dir_thumb_path);
	g_free(dir);
}

static void ordered_update(ChildThumbnail *info)
{
	static GSList *done_stack = NULL;
	done_stack = g_slist_prepend(done_stack, info);

	if (next_order < info->order) return;

	GSList *n = done_stack;
	while (n)
	{
		ChildThumbnail *li = n->data;
		if (li->order > next_order)
		{
			n = n->next;
			continue;
		}

		if (!li->data)
			dir_force_update_path(li->path, TRUE);
		make_dir_thumb(li->path);

		g_free(li->path);
		g_free(li->rpath);
		g_free(li);

		next_order++;
		n = done_stack =
			g_slist_delete_link(done_stack, n);
	}
}


static void do_nothing(void *v, void *v2) {}
static gboolean retry_thumb(char *path)
{
	pixmap_background_thumb(path, do_nothing, NULL);
	g_free(path);
	return FALSE;
}

#define SPECCMP(left, cmp, right) (\
	left.tv_sec == right.tv_sec ? \
		left.tv_nsec cmp right.tv_nsec : left.tv_sec cmp right.tv_sec)
static void thumbnail_done(ChildThumbnail *info)
{
	if (info->timeout)
		g_source_remove(info->timeout);

	bool ok = false;
	struct stat tinfo, nowinfo;
	char *tpath = _thumb_path(info->rpath, false);
	if (mc_stat(tpath, &tinfo) == 0 && SPECCMP(tinfo.st_ctim, >=, info->orgtime))
	{
		if (mc_stat(info->rpath, &nowinfo) == 0
				&& SPECCMP(nowinfo.st_ctim, !=, info->orgtime))
		{ //file is changed from start time
			g_timeout_add(900, (GSourceFunc) retry_thumb, g_strdup(info->rpath));
		}
		ok = true;
	}
	else
		symlink("/dev/null", tpath);

	g_hash_table_remove(orders, info->rpath);
	info->callback(info->data, ok ? info->path : NULL);
	ordered_update(info);

	g_free(tpath);
}


/* Check if we have an up-to-date thumbnail for this image.
 * If so, return it. Otherwise, returns NULL.
 */
static GdkPixbuf *get_thumbnail_for(const char *rpath, gboolean *forcheck)
{
	char *thumb_path = pixmap_make_thumb_path(rpath);
	char *dir = NULL;
	GdkPixbuf *thumb = NULL;

	struct stat info, thumbinfo;
	if (mc_lstat(rpath     , &info     ) != 0)
		goto out;

	if (mc_lstat(thumb_path, &thumbinfo) != 0)
	{
		if (*forcheck)
		{
			struct stat info1;
			if (mc_stat(rpath, &info1) != 0 || info1.st_size == 0)
				goto out;
		}

		dir = g_path_get_dirname(rpath);
		if (strcmp(dir, thumbdir()))
			goto err;
		else if (!*forcheck)
			thumb = gdk_pixbuf_new_from_file_at_scale(rpath,
					thumb_size, thumb_size, TRUE, NULL);

		goto out;
	}

	if (!*forcheck)
		thumbinfo.st_ctim.tv_sec++; //one sec older file is valid

	if (SPECCMP(info.st_ctim, >, thumbinfo.st_ctim))
	{
		unlink(thumb_path);
		goto err;
	}

	if (!*forcheck)
		thumb = gdk_pixbuf_new_from_file(thumb_path, NULL);
	else if (S_ISLNK(thumbinfo.st_mode) && mc_stat(thumb_path, &thumbinfo))
	{//broken link
		unlink(thumb_path);
		goto err;
	}
	goto out;

err:
	*forcheck = FALSE;

out:
	g_free(thumb_path);
	g_free(dir);
	return thumb;
}

/* Load the image 'path' and return a pointer to the resulting
 * MaskedPixmap. NULL on failure.
 * Doesn't check for thumbnails (this is for small icons).
 */
static MaskedPixmap *image_from_file(const char *path)
{
	GdkPixbuf	*pixbuf;
	MaskedPixmap	*image;
	GError		*error = NULL;

	pixbuf = gdk_pixbuf_new_from_file(path, &error);
	if (!pixbuf)
	{
		g_warning("%s\n", error->message);
		g_error_free(error);
		return NULL;
	}

	image = masked_pixmap_new(pixbuf);

	g_object_unref(pixbuf);

	return image;
}

/* Load this icon named by this .desktop file from the current theme.
 * NULL on failure.
 */
MaskedPixmap *pixmap_from_desktop_file(const char *path)
{
	GError *error = NULL;
	MaskedPixmap *image = NULL;
	char *icon = NULL;

	icon = get_value_from_desktop_file(path,
					"Desktop Entry", "Icon", &error);

//	GKeyFile *key_file = g_key_file_new();
//	if (g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &error))
//		icon = g_key_file_get_string(key_file, "Desktop Entry", "Icon", &error);
//	g_key_file_unref(key_file);

	if (error)
	{
		g_warning("Failed to parse .desktop file '%s':\n%s",
				path, error->message);
		goto err;
	}
	if (!icon)
		goto err;

	if (icon[0] == '/')
		image = image_from_file(icon);
	else
	{
		GdkPixbuf *pixbuf;
		int tmp_fd;
		char *extension;

		/* For some unknown reason, some icon names have extensions.
		 * Remove them.
		 */
		extension = strrchr(icon, '.');
		if (extension && (strcmp(extension, ".png") == 0
						|| strcmp(extension, ".xpm") == 0
						|| strcmp(extension, ".svg") == 0))
		{
			*extension = '\0';
		}

		/* SVG reader is very noisy, so redirect stderr to stdout */
		tmp_fd = dup(2);
		dup2(1, 2);
		pixbuf = theme_load_icon(icon, thumb_size, 0, NULL);
		dup2(tmp_fd, 2);
		close(tmp_fd);

		if (pixbuf == NULL)
			goto err;	/* Might just not be in the theme */

		image = masked_pixmap_new(pixbuf);
		g_object_unref(pixbuf);
	}
err:
	if (error != NULL)
		g_error_free(error);
	if (icon != NULL)
		g_free(icon);
	return image;
}

/* Scale src down to fit in max_w, max_h and return the new pixbuf.
 * If src is small enough, then ref it and return that.
 */
GdkPixbuf *scale_pixbuf(GdkPixbuf *src, int max_w, int max_h)
{
	int	w, h;

	w = gdk_pixbuf_get_width(src);
	h = gdk_pixbuf_get_height(src);

	if (w <= max_w && h <= max_h)
	{
		g_object_ref(src);
		return src;
	}
	else
	{
		float scale_x = ((float) w) / max_w;
		float scale_y = ((float) h) / max_h;
		float scale = MAX(scale_x, scale_y);
		int dest_w = w / scale;
		int dest_h = h / scale;

		return gdk_pixbuf_scale_simple(src,
						MAX(dest_w, 1),
						MAX(dest_h, 1),
						GDK_INTERP_BILINEAR);
	}
}

/* Return a pointer to the (static) bad image. The ref counter will ensure
 * that the image is never freed.
 */
static MaskedPixmap *get_bad_image(void)
{
	GdkPixbuf *bad;
	MaskedPixmap *mp;

	bad = gdk_pixbuf_new_from_xpm_data(bad_xpm);
	mp = masked_pixmap_new(bad);
	g_object_unref(bad);

	return mp;
}

/* Called now and then to clear out old pixmaps */
static gint purge_pixmaps(gpointer data)
{
	g_fscache_purge(pixmap_cache, PIXMAP_PURGE_TIME);
	return TRUE;
}

static gpointer parent_class;

static void masked_pixmap_finialize(GObject *object)
{
	MaskedPixmap *mp = (MaskedPixmap *) object;

	if (mp->src_pixbuf)
	{
		g_object_unref(mp->src_pixbuf);
		mp->src_pixbuf = NULL;
	}

	if (mp->pixbuf)
	{
		g_object_unref(mp->pixbuf);
		mp->pixbuf = NULL;
	}

	if (mp->sm_pixbuf)
	{
		g_object_unref(mp->sm_pixbuf);
		mp->sm_pixbuf = NULL;
	}

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void masked_pixmap_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = (GObjectClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);

	object->finalize = masked_pixmap_finialize;
}

static void masked_pixmap_init(GTypeInstance *object, gpointer gclass)
{
	MaskedPixmap *mp = (MaskedPixmap *) object;

	mp->src_pixbuf = NULL;

	mp->huge_width = -1;
	mp->huge_height = -1;

	mp->pixbuf = NULL;
	mp->width = -1;
	mp->height = -1;

	mp->sm_pixbuf = NULL;
	mp->sm_width = -1;
	mp->sm_height = -1;
}

static GType masked_pixmap_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (MaskedPixmapClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			masked_pixmap_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(MaskedPixmap),
			0,			/* n_preallocs */
			masked_pixmap_init
		};

		type = g_type_register_static(G_TYPE_OBJECT, "MaskedPixmap",
					      &info, 0);
	}

	return type;
}

MaskedPixmap *masked_pixmap_new(GdkPixbuf *src)
{
	MaskedPixmap *mp;
	GdkPixbuf	*normal_pixbuf;

	g_return_val_if_fail(src != NULL, NULL);

	normal_pixbuf = scale_pixbuf(src, ICON_WIDTH, ICON_HEIGHT);
	g_return_val_if_fail(normal_pixbuf != NULL, NULL);

	mp = g_object_new(masked_pixmap_get_type(), NULL);

	mp->huge_width = gdk_pixbuf_get_width(src);
	mp->huge_height = gdk_pixbuf_get_height(src);
	if (mp->huge_width <= thumb_size && mp->huge_height <= thumb_size)
	{
		g_object_ref(src);
		mp->src_pixbuf = src;
	}
	else
	{
		mp->src_pixbuf = scale_pixbuf(src, thumb_size, thumb_size);
		mp->huge_width = gdk_pixbuf_get_width(mp->src_pixbuf);
		mp->huge_height = gdk_pixbuf_get_height(mp->src_pixbuf);
	}

	mp->pixbuf = normal_pixbuf;
	mp->width = gdk_pixbuf_get_width(normal_pixbuf);
	mp->height = gdk_pixbuf_get_height(normal_pixbuf);

	return mp;
}

/* Load all the standard pixmaps. Also sets the default window icon. */
static GdkPixbuf *winicon;
static void load_default_pixmaps(void)
{
	GError *error = NULL;

	im_error = mp_from_stock(GTK_STOCK_DIALOG_WARNING,
				 GTK_ICON_SIZE_DIALOG);
	im_unknown = mp_from_stock(GTK_STOCK_DIALOG_QUESTION,
				   GTK_ICON_SIZE_DIALOG);

	im_dirs = load_pixmap("dirs");
	im_appdir = load_pixmap("application");

	winicon = gtk_icon_theme_load_icon(
		gtk_icon_theme_get_default(), APPNAME, thumb_size, 0, &error);

	if (winicon)
		gtk_window_set_default_icon(winicon);
	else
	{
		g_warning("%s\n", error->message);
		g_error_free(error);
	}
}

/* Also purges memory cache */
static void purge_disk_cache(GtkWidget *button, gpointer data)
{
	char *path;
	GList *list = NULL;
	DIR *dir;
	struct dirent *ent;

	path = g_strconcat(thumbdir(), "/", NULL);

	dir = opendir(path);
	if (!dir)
	{
		report_error(_("Can't delete thumbnails in %s:\n%s"),
				path, g_strerror(errno));
		goto out;
	}

	time_t checktime = o_purge_days.int_value ?
		time(0) - (o_purge_days.int_value * 3600 * 24): 0;
	struct stat info;

	while ((ent = readdir(dir)))
	{
		if (ent->d_name[0] == '.')
			continue;

		if (o_purge_days.int_value
				&& !mc_lstat(make_path(path, ent->d_name), &info)
				&& info.st_atime > checktime)
			continue;

		list = g_list_prepend(list,
				      g_strconcat(path, ent->d_name, NULL));
	}

	closedir(dir);

	if (list)
	{
		action_delete(list);
		destroy_glist(&list);
	}
	else
		info_message(_("There are no thumbnails to delete"));
out:
	g_free(path);
}

static GList *thumbs_purge_cache(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *button, *align;

	g_return_val_if_fail(option == NULL, NULL);

	align = gtk_alignment_new(0, 0.5, 0, 0);
	button = button_new_mixed(GTK_STOCK_CLEAR,
				  _("Purge thumbnails disk cache"));
	gtk_container_add(GTK_CONTAINER(align), button);
	g_signal_connect(button, "clicked", G_CALLBACK(purge_disk_cache), NULL);

	return g_list_append(NULL, align);
}


static cairo_status_t suf_to_bufcb(void *p,
		const unsigned char *data, unsigned int len)
{
	g_memory_input_stream_add_data((GMemoryInputStream *)p,
			g_memdup(data, len), len, g_free);
	return CAIRO_STATUS_SUCCESS;
}
static GdkPixbuf *suf_to_buf(cairo_surface_t *suf)
{
	GInputStream *st = g_memory_input_stream_new();
	cairo_surface_write_to_png_stream(suf, suf_to_bufcb, st);
	GdkPixbuf *ret = gdk_pixbuf_new_from_stream(st, NULL, NULL);
	g_object_unref(st);
	return ret;
}
GdkPixbuf *pixmap_make_lined(GdkPixbuf *src, GdkColor *colour)
{
	if (!(src = src ?: winicon)) return NULL;

	int height = gdk_pixbuf_get_height(src);
	int width  = gdk_pixbuf_get_width(src);

	cairo_surface_t *suf = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, width, height);

	cairo_t *cr = cairo_create(suf);

	gdk_cairo_set_source_pixbuf(cr, src, 0, 0);
	cairo_paint(cr);

	gdouble base = height / 14.0;
	gdk_cairo_set_source_color(cr, colour);
	cairo_set_line_width(cr, base * 2);
	cairo_move_to(cr, 0    , height - base * 1.2);
	cairo_line_to(cr, width, height - base * 1.2);
	cairo_stroke(cr);

	cairo_set_line_width(cr, base);
	cairo_move_to(cr, 0    , height - base * 3.4);
	cairo_line_to(cr, width, height - base * 3.4);

	cairo_stroke(cr);

//	GdkPixbuf *ret = gdk_pixbuf_get_from_surface(suf);
	GdkPixbuf *ret = suf_to_buf(suf);

	cairo_destroy(cr);
	cairo_surface_destroy(suf);

	return ret;
}
