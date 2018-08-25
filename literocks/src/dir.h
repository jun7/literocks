/*
 * ROX-Filer, filer for the ROX desktop project
 * Thomas Leonard, <tal197@users.sourceforge.net>
 */


#ifndef _DIR_H
#define _DIR_H

#include <sys/types.h>
#include <dirent.h>

#include <signal.h>
#include <fcntl.h>

#define DIR_NOTIFY_TIME_FOR_SORT_DATA 1111
#define DIR_NOTIFY_TIME 111

typedef enum {
	DIR_START_SCAN,    /* 0 Set 'scanning' indicator */
	DIR_END_SCAN,      /* 1 Clear 'scanning' indicator */
	DIR_ADD,           /* 2 Add the listed items to the display */
	DIR_REMOVE,        /* 3 Remove listed items from display */
	DIR_UPDATE,        /* 4 Redraw these items */
	DIR_UPDATE_EXA,    /* 5 Redraw these items */
	DIR_UPDATE_ICON,   /* 6 Redraw these items */
	DIR_ERROR_CHANGED, /* 7 Check dir->error */
	DIR_QUEUE_INTERESTING,  /* 8 Call dir_queue_recheck */
} DirAction;

typedef struct _DirUser DirUser;
typedef void (*DirCallback)(Directory *dir,
			DirAction action,
			void *items,
			gpointer data);

extern GFSCache *dir_cache;

struct _DirUser
{
	DirCallback	callback;
	gpointer	data;
};

typedef struct _DirectoryClass DirectoryClass;

struct _DirectoryClass {
	GObjectClass parent;
};

struct _Directory
{
	GObject object;

	char	*pathname;	/* Internal use only */
	GList	*users;		/* Functions to call on update */
	char	*error;		/* NULL => no error */

	struct stat	stat_info;	/* Internal use */

	guint		notify_active;	/* Notify timeout is running */
	int			notify_time;	/* Time of Notify timeout */
	gint		idle_callback;	/* Idle callback ID */
	gboolean	in_scan_thread, req_scan_off, req_notify;
	GThread		*t_scan;

	GMutex		mutex;
	GMutex		mergem;
	GString		*strbuf;

	GHashTable 	*known_items;	/* What our users know about */
	GPtrArray	*new_items;	/* New items to add in */
	GPtrArray	*up_items;	/* Items to redraw */
	GPtrArray	*exa_items;	/* Items to redraw */
	GHashTable 	*gone_items;	/* Items removed */

	GPtrArray	*recheck_list;	/* Items to check on callback */
	int rechecki;
	GPtrArray	*examine_list;	/* Items to examine on callback */
	int examinei;

	gboolean	have_scanned;	/* TRUE after first complete scan */
	gboolean	scanning;	/* TRUE if we sent DIR_START_SCAN */

	/* Indicates that the directory needs to be rescanned.
	 * This is cleared when scanning starts, and set when the fscache
	 * detects that the directory needs to be rescanned and is already
	 * scanning.
	 *
	 * If scanning finishes when this is set, or if someone attaches
	 * and scanning is not in progress, a rescan is triggered.
	 */
	gboolean	needs_update;

	gint		rescan_timeout;	/* See dir_rescan_soon() */

	GFileMonitor *monitor;
};

void dir_init(void);
Directory *dir_new(const char *pathname);
void dir_attach(Directory *dir, DirCallback callback, gpointer data);
void dir_detach(Directory *dir, DirCallback callback, gpointer data);
void dir_update(Directory *dir, gchar *pathname);
void refresh_dirs(const char *path);
void dir_check_this(const guchar *path);
DirItem *dir_update_item(Directory *dir, const gchar *leafname);
void dir_merge_new(Directory *dir);
void dir_force_update_path(const gchar *path, gboolean icon);
void dir_drop_all_notifies(void);
void dir_queue_recheck(Directory *dir, DirItem *item);
void dir_stop(void); /* stop all scan thread */

#endif /* _DIR_H */
