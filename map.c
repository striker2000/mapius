#include <Python.h>
#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <proj_api.h>

#include "map.h"

typedef struct
{
	gchar *id;
	gchar *title;
	gchar *format;
	projPJ proj;
	PyObject *module;
	PyObject *url_func;
} MapInfo;

struct _MapPrivate
{
	guint center_x;
	guint center_y;
	guint click_x;
	guint click_y;
	guint zoom;
	GHashTable *tiles;
	GHashTable *loading;
	SoupSession *soup_session;
	gboolean button_press;
	guint current_ts;
	projPJ spherical_mercator_proj;
	projPJ ellipse_mercator_proj;
	projPJ latlong_proj;
	projPJ current_proj;
	GHashTable *maps;
	MapInfo *current_map;
};

typedef struct
{
	GdkPixbuf *pixbuf;
	guint ts;
} Tile;

typedef struct
{
	Map *map;
	gchar *folder;
	gchar *filename;
	guint tile_x;
	guint tile_y;
} TileInfo;

G_DEFINE_TYPE (Map, map, GTK_TYPE_DRAWING_AREA);

static gchar *get_tile_url (MapInfo *map_info, int zoom, int x, int y);
static void map_init_maps (MapPrivate *priv);
static void map_tile_free (Tile *tile);
static void map_init (Map *map);
static void map_local_tile_loaded (GObject *stream, GAsyncResult *res, gpointer data);
static void map_local_tile_opened (GObject *file, GAsyncResult *res, gpointer data);
static void map_tile_loaded (SoupSession *session, SoupMessage *msg, gpointer data);
static gboolean map_tile_purge_check (gchar *key, Tile *tile, gpointer data);
static gboolean map_draw (GtkWidget *widget, cairo_t *cr);
static void map_change_zoom (GtkWidget *widget, int dx, int dy, gboolean up);
static gboolean map_change_map (GtkWidget *widget, int keyval);
static gboolean map_key_press (GtkWidget *widget, GdkEventKey *event);
static gboolean map_button_press (GtkWidget *widget, GdkEventButton *event);
static gboolean map_button_release (GtkWidget *widget, GdkEventButton *event);
static gboolean map_motion_notify (GtkWidget *widget, GdkEventMotion *event);
static gboolean map_scroll (GtkWidget *widget, GdkEventScroll *event);

GtkWidget *
map_new()
{
	return g_object_new (MAP_TYPE, NULL);
}

static void
map_class_init (MapClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	g_type_class_add_private (klass, sizeof (MapPrivate));

	widget_class->draw = map_draw;
	widget_class->key_press_event = map_key_press;
	widget_class->button_press_event = map_button_press;
	widget_class->button_release_event = map_button_release;
	widget_class->motion_notify_event = map_motion_notify;
	widget_class->scroll_event = map_scroll;

	g_signal_new ("loading", MAP_TYPE,
		G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
		g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 1, G_TYPE_UINT);

	g_signal_new ("zoom-changed", MAP_TYPE,
		G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
		g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 1, G_TYPE_UINT);

	g_signal_new ("map-changed", MAP_TYPE,
		G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
		g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static gchar *
get_tile_url (MapInfo *map_info, int zoom, int x, int y)
{
	PyObject *value = PyObject_CallFunction (map_info->url_func, "(iii)", x, y, zoom);
	if (!value) {
		PyErr_Print();
		g_error ("get_tile_url failed");
		return NULL;
	}

	gchar *result = g_strdup (PyString_AsString (value));
	Py_DECREF (value);

	return result;
}

static void
map_init_maps (MapPrivate *priv)
{
	PyObject *name, *module, *func, *value;
	GError *err = NULL;
	GDir *dir;
	const gchar *fn;
	gchar *map_id;

	Py_Initialize();
	PySys_SetPath ("maps");

	dir = g_dir_open ("maps", 0, &err);
	if (err) {
		g_error ("Error opening maps directory");
	}

	g_debug ("Loading maps");

	priv->maps = g_hash_table_new_full (g_int_hash, g_str_equal, g_free, NULL);

	while ((fn = g_dir_read_name (dir))) {
		if (!g_str_has_suffix (fn, ".py"))
			continue;

		map_id = g_strndup (fn, strlen(fn) - 3);

		g_debug ("  %s", map_id);

		name = PyString_FromString (map_id);
		module = PyImport_Import (name);
		Py_DECREF (name);

		if (!module) {
			PyErr_Print();
			g_warning ("Error loading map '%s'", map_id);
			continue;
		}

		func = PyObject_GetAttrString (module, "url");
		if (!func) {
			g_warning ("No 'url' function for map '%s'\n", map_id);
			continue;
		}

		value = PyObject_GetAttrString (module, "title");
		if (!value) {
			g_warning ("No title for map '%s'", map_id);
			continue;
		}
		gchar *title = g_strdup (PyString_AsString (value));
		Py_DECREF (value);

		value = PyObject_GetAttrString (module, "key");
		if (!value) {
			g_warning ("No key for map '%s'", map_id);
			continue;
		}
		gchar *key = g_strdup (PyString_AsString (value));
		Py_DECREF (value);

		gint *keyval = g_new (gint, 1);
		*keyval = gdk_keyval_from_name (key);
		if (*keyval == GDK_KEY_VoidSymbol || *keyval == 0) {
			g_warning ("Unknown key '%s' for map '%s'", key, map_id);
			continue;
		}

		value = PyObject_GetAttrString (module, "format");
		if (!value) {
			g_warning ("No format for map '%s'", map_id);
			continue;
		}
		gchar *format = g_strdup (PyString_AsString (value));
		Py_DECREF (value);

		value = PyObject_GetAttrString (module, "proj");
		if (!value) {
			g_warning ("No projection for map '%s'", map_id);
			continue;
		}
		if (!PyInt_Check (value)) {
			g_warning ("Bad projection for map '%s'", map_id);
			Py_DECREF (value);
			continue;
		}
		int epsg = PyInt_AsLong (value);
		Py_DECREF (value);

		projPJ proj;
		if (epsg == 3857) {
			proj = priv->spherical_mercator_proj;
		}
		else if (epsg == 3395) {
			proj = priv->ellipse_mercator_proj;
		}
		else {
			g_warning ("Unknown projection %d for map '%s'", epsg, map_id);
			continue;
		}

		MapInfo *map_info = g_new (MapInfo, 1);
		map_info->id = map_id;
		map_info->title = title;
		map_info->format = format;
		map_info->proj = proj;
		map_info->module = module;
		map_info->url_func = func;

		g_hash_table_insert (priv->maps, keyval, map_info);

		if (g_strcmp0 (map_id, "osmmapMapnik") == 0)
			priv->current_map = map_info;
	}
}

static void
map_tile_free (Tile *tile)
{
	g_object_unref (tile->pixbuf);
	g_free (tile);
}

static void
map_init (Map *map)
{
	MapPrivate *priv;

	priv = G_TYPE_INSTANCE_GET_PRIVATE (map, MAP_TYPE, MapPrivate);
	priv->center_x = 128;
	priv->center_y = 128;
	priv->zoom = 0;
	priv->tiles = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) map_tile_free);
	priv->loading = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	priv->soup_session = soup_session_async_new_with_options (
		SOUP_SESSION_MAX_CONNS_PER_HOST, 5,
		SOUP_SESSION_USER_AGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/536.11 (KHTML, like Gecko) Ubuntu/12.04 Chromium/20.0.1132.47 Chrome/20.0.1132.47 Safari/536.11",
		NULL);
	priv->current_ts = 0;
	priv->spherical_mercator_proj = pj_init_plus ("+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +a=6378137 +b=6378137 +towgs84=0,0,0,0,0,0,0 +units=m +no_defs");
	priv->ellipse_mercator_proj = pj_init_plus ("+proj=merc +lon_0=0 +k=1 +x_0=0 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m +no_defs");
	priv->latlong_proj = pj_init_plus ("+proj=latlong +ellps=WGS84");

	map_init_maps(priv);

	map->priv = priv;

	gtk_widget_add_events (
		GTK_WIDGET (map),
		GDK_POINTER_MOTION_HINT_MASK
		| GDK_POINTER_MOTION_MASK
		| GDK_BUTTON_PRESS_MASK
		| GDK_BUTTON_RELEASE_MASK
		| GDK_SCROLL_MASK
		| GDK_KEY_PRESS_MASK
	);
	gtk_widget_set_can_focus (GTK_WIDGET (map), TRUE);
}

static void
map_local_tile_loaded (GObject *stream, GAsyncResult *res, gpointer data)
{
	TileInfo *info = (TileInfo *) data;

	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_finish (res, NULL);
	if (pixbuf) {
		Tile *tile = g_new (Tile, 1);
		tile->pixbuf = pixbuf;
		tile->ts = info->map->priv->current_ts;
		g_hash_table_insert (info->map->priv->tiles, g_strdup (info->filename), tile);

		gtk_widget_queue_draw (GTK_WIDGET (info->map));
	}

	g_object_unref (stream);
	g_free (info->folder);
	g_free (info->filename);
	g_free (info);
}

static void
map_local_tile_opened (GObject *file, GAsyncResult *res, gpointer data)
{
	TileInfo *info = (TileInfo *) data;

	GFileInputStream *stream = g_file_read_finish (G_FILE(file), res, NULL);
	if (stream) {
		gdk_pixbuf_new_from_stream_async (G_INPUT_STREAM (stream), NULL, map_local_tile_loaded, info);
	}
	else if (!g_hash_table_lookup (info->map->priv->loading, info->filename)) {
		gchar *url = get_tile_url (info->map->priv->current_map, info->map->priv->zoom, info->tile_x, info->tile_y);

		SoupMessage *msg = soup_message_new ("GET", url);
		g_assert (msg != NULL);
		soup_session_queue_message (info->map->priv->soup_session, msg, map_tile_loaded, info);

		g_hash_table_insert (info->map->priv->loading, g_strdup (info->filename), msg);

		g_signal_emit_by_name (info->map, "loading", g_hash_table_size (info->map->priv->loading));

		g_free (url);
	}

	g_object_unref (file);
}

static void
map_tile_loaded (SoupSession *session, SoupMessage *msg, gpointer data)
{
	TileInfo *info = (TileInfo *) data;
	FILE *file;

	g_debug ("%s: %d %s", info->filename, msg->status_code, msg->reason_phrase);

	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		if (g_mkdir_with_parents (info->folder, 0755) == 0) {
			file = fopen (info->filename, "wb");
			if (file != NULL) {
				fwrite (msg->response_body->data, 1, msg->response_body->length, file);
				fclose (file);
				gtk_widget_queue_draw (GTK_WIDGET (info->map));
			}
		} else {
			g_warning ("Error creating tile download directory: %s", info->folder);
		}
	}

	g_hash_table_remove (info->map->priv->loading, info->filename);
	g_signal_emit_by_name (info->map, "loading", g_hash_table_size (info->map->priv->loading));

	g_free (info->folder);
	g_free (info->filename);
	g_free (info);
}

static gboolean
map_tile_purge_check (gchar *key, Tile *tile, gpointer data)
{
	return ((MapPrivate *) data)->current_ts - tile->ts > 2;
}

static gboolean
map_draw (GtkWidget *widget, cairo_t *cr)
{
	MapPrivate *priv = MAP (widget)->priv;
	guint center_x, center_y;
	gint offset_x, offset_y;
	guint min_x, max_x, min_y, max_y;
	guint max_size;
	guint tile_x, tile_y;
	gint draw_x, draw_y;
	gchar *filename;
	Tile *tile;

	center_x = gtk_widget_get_allocated_width (widget) / 2;
	center_y = gtk_widget_get_allocated_height (widget) / 2;

	offset_x = center_x - priv->center_x;
	offset_y = center_y - priv->center_y;

	min_x = priv->center_x > center_x ? (priv->center_x - center_x) / 256 : 0;
	min_y = priv->center_y > center_y ? (priv->center_y - center_y) / 256 : 0;
	max_x = (priv->center_x + center_x) / 256;
	max_y = (priv->center_y + center_y) / 256;
	max_size = pow (2, priv->zoom) - 1;
	if (max_x > max_size)
		max_x = max_size;
	if (max_y > max_size)
		max_y = max_size;

	draw_y = min_y * 256 + offset_y;
	for (tile_y = min_y; tile_y <= max_y; tile_y++) {
		draw_x = min_x * 256 + offset_x;
		for (tile_x = min_x; tile_x <= max_x; tile_x++) {
			filename = g_strdup_printf (
				"cache/%s/%d/%d/%d.%s",
				priv->current_map->id,
				priv->zoom,
				tile_x,
				tile_y,
				priv->current_map->format
			);
			tile = g_hash_table_lookup (priv->tiles, filename);
			if (tile) {
				g_free (filename);

				gdk_cairo_set_source_pixbuf (cr, tile->pixbuf, draw_x, draw_y);
				cairo_paint (cr);

				tile->ts = priv->current_ts;
			}
			else {
				GFile *file = g_file_new_for_path (filename);

				TileInfo *info = g_new0 (TileInfo, 1);
				info->map = MAP (widget);
				info->folder = g_strdup_printf (
					"cache/%s/%d/%d",
					priv->current_map->id,
					priv->zoom,
					tile_x
				);
				info->filename = filename;
				info->tile_x = tile_x;
				info->tile_y = tile_y;

				g_file_read_async (file, G_PRIORITY_DEFAULT, NULL, map_local_tile_opened, info);

				guint scaled_zoom;
				guint scale;
				for (scale = 2, scaled_zoom = priv->zoom - 1; scale <= 256 && scaled_zoom > 0; scale *= 2, scaled_zoom--) {
					filename = g_strdup_printf (
						"cache/%s/%d/%d/%d.%s",
						priv->current_map->id,
						scaled_zoom,
						tile_x / scale,
						tile_y / scale,
						priv->current_map->format
					);
					tile = g_hash_table_lookup (priv->tiles, filename);
					g_free (filename);
					if (tile) {
						guint size = 256 / scale;
						GdkPixbuf *area = gdk_pixbuf_new_subpixbuf (
							tile->pixbuf,
							tile_x % scale * size,
							tile_y % scale * size,
							size,
							size
						);
						GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple (area, 256, 256, GDK_INTERP_NEAREST);
						gdk_cairo_set_source_pixbuf (cr, scaled_pixbuf, draw_x, draw_y);
						cairo_paint (cr);
						g_object_unref (area);
						g_object_unref (scaled_pixbuf);
						break;
					}
				}
			}
			draw_x += 256;
		}
		draw_y += 256;
	}

	cairo_set_line_width (cr, 1);
	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_move_to (cr, center_x - 4.5, center_y + 0.5);
	cairo_line_to (cr, center_x + 5.5, center_y + 0.5);
	cairo_move_to (cr, center_x + 0.5, center_y - 4.5);
	cairo_line_to (cr, center_x + 0.5, center_y + 5.5);
	cairo_stroke (cr);

	if (g_hash_table_size (priv->tiles) > 500) {
		g_debug ("Purging tiles");
		guint res = g_hash_table_foreach_remove (priv->tiles, (GHRFunc) map_tile_purge_check, priv);
		g_debug ("Removed %d tiles, left %d", res, g_hash_table_size (priv->tiles));
	}

	return FALSE;
}

static void
map_change_zoom (GtkWidget *widget, int dx, int dy, gboolean up)
{
	MapPrivate *priv = MAP (widget)->priv;

	if (up) {
		if (priv->zoom >= 24)
			return;
		priv->zoom++;
		if (dx || dy) {
			priv->center_x = (priv->center_x + dx) * 2 - dx;
			priv->center_y = (priv->center_y + dy) * 2 - dy;
		}
		else {
			priv->center_x *= 2;
			priv->center_y *= 2;
		}
	}
	else {
		if (priv->zoom <= 0)
			return;
		priv->zoom--;
		if (dx || dy) {
			priv->center_x = (priv->center_x + dx) / 2 - dx;
			priv->center_y = (priv->center_y + dy) / 2 - dy;
		}
		else {
			priv->center_x /= 2;
			priv->center_y /= 2;
		}
	}

	soup_session_abort (priv->soup_session);
	g_hash_table_remove_all (priv->loading);

	priv->current_ts++;

	g_signal_emit_by_name (widget, "zoom-changed", priv->zoom);
}

static gboolean
map_change_map (GtkWidget *widget, int keyval)
{
	MapPrivate *priv = MAP (widget)->priv;

	MapInfo *map_info = g_hash_table_lookup (priv->maps, &keyval);
	if (map_info) {
		if (map_info->proj != priv->current_map->proj) {
			guint size = pow (2, priv->zoom + 7);

			double x = (priv->center_x - size) * 20037508.34 / size;
			double y = (size - priv->center_y) * 20037508.34 / size;

			pj_transform (priv->current_map->proj, priv->latlong_proj, 1, 1, &x, &y, NULL);
			pj_transform (priv->latlong_proj, map_info->proj, 1, 1, &x, &y, NULL);

			priv->center_x = x * size / 20037508.34 + size;
			priv->center_y = size - y * size / 20037508.34;
		}

		priv->current_map = map_info;

		soup_session_abort (priv->soup_session);
		g_hash_table_remove_all (priv->loading);

		priv->current_ts++;

		g_signal_emit_by_name (widget, "map-changed", map_info->title);
	}

	return map_info ? TRUE : FALSE;
}

static gboolean
map_key_press (GtkWidget *widget, GdkEventKey *event)
{
	MapPrivate *priv = MAP (widget)->priv;

	if (event->keyval == GDK_KEY_Left) {
		priv->center_x -= 64;
	}
	else if (event->keyval == GDK_KEY_Right) {
		priv->center_x += 64;
	}
	else if (event->keyval == GDK_KEY_Up) {
		priv->center_y -= 64;
	}
	else if (event->keyval == GDK_KEY_Down) {
		priv->center_y += 64;
	}
	else if (event->keyval == GDK_KEY_Page_Up) {
		map_change_zoom (widget, 0, 0, TRUE);
	}
	else if (event->keyval == GDK_KEY_Page_Down) {
		map_change_zoom (widget, 0, 0, FALSE);
	}
	else if (!map_change_map (widget, event->keyval)) {
		return FALSE;
	}

	gtk_widget_queue_draw (widget);

	return TRUE;
}

static gboolean
map_button_press (GtkWidget *widget, GdkEventButton *event)
{
	MapPrivate *priv = MAP (widget)->priv;

	priv->click_x = priv->center_x + event->x;
	priv->click_y = priv->center_y + event->y;

	priv->button_press = TRUE;

	return TRUE;
}

static gboolean
map_button_release (GtkWidget *widget, GdkEventButton *event)
{
	MapPrivate *priv = MAP (widget)->priv;

	priv->button_press = FALSE;

	gtk_widget_queue_draw (widget);

	return TRUE;
}

static gboolean
map_motion_notify (GtkWidget *widget, GdkEventMotion *event)
{
	MapPrivate *priv = MAP (widget)->priv;
	gint x, y;

	if (!priv->button_press)
		return FALSE;

	gdk_window_get_device_position (event->window, event->device, &x, &y, NULL);
	priv->center_x = priv->click_x - x;
	priv->center_y = priv->click_y - y;

	gtk_widget_queue_draw (widget);

	return TRUE;
}

static gboolean
map_scroll (GtkWidget *widget, GdkEventScroll *event)
{
	guint width, height;
	gint dx, dy;

	width = gtk_widget_get_allocated_width (widget);
	height = gtk_widget_get_allocated_height (widget);

	dx = event->x - width / 2;
	dy = event->y - height / 2;

	if (event->direction == GDK_SCROLL_UP) {
		map_change_zoom (widget, dx, dy, TRUE);
		gtk_widget_queue_draw (widget);
	}
	else if (event->direction == GDK_SCROLL_DOWN) {
		map_change_zoom (widget, dx, dy, FALSE);
		gtk_widget_queue_draw (widget);
	}

	return TRUE;
}
