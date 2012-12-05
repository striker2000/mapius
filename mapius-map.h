#ifndef __MAPIUS_MAP_H__
#define __MAPIUS_MAP_H__

#include <gtk/gtk.h>

#define MAPIUS_TYPE_MAP (mapius_map_get_type ())
#define MAPIUS_MAP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), MAPIUS_TYPE_MAP, MapiusMap))

typedef struct _MapiusMap MapiusMap;
typedef struct _MapiusMapClass MapiusMapClass;
typedef struct _MapiusMapPrivate MapiusMapPrivate;
typedef struct _MapiusMapInfo MapiusMapInfo;

struct _MapiusMap
{
	GtkDrawingArea parent_instance;
	GSList *maps;
	MapiusMapPrivate *priv;
};

struct _MapiusMapClass
{
	GtkDrawingAreaClass parent_class;
};

struct _MapiusMapInfo
{
	gchar *id;
	gchar *title;
	guint accel_key;
	GdkModifierType accel_mods;
};

GType mapius_map_get_type (void);
GtkWidget *mapius_map_new();
void mapius_map_change_map (MapiusMap *map, gchar *id);

#endif
