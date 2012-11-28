#ifndef __MAP_H__
#define __MAP_H__

#include <gtk/gtk.h>

#define MAP_TYPE (map_get_type())
#define MAP(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), MAP_TYPE, Map))

typedef struct _Map Map;
typedef struct _MapClass MapClass;
typedef struct _MapPrivate MapPrivate;

struct _Map
{
	GtkDrawingArea parent_instance;
	MapPrivate *priv;
};

struct _MapClass
{
	GtkDrawingAreaClass parent_class;
};

GType map_get_type (void);
GtkWidget *map_new();

#endif
