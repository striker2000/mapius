#include <gtk/gtk.h>

#include "mapius-map.h"

static void
map_loading (MapiusMap *map, guint cnt, GtkWidget *label)
{
	gchar *str = g_strdup_printf ("Loading: %d", cnt);
	gtk_label_set_text (GTK_LABEL (label), str);
	g_free (str);
}

static void
map_zoom_changed (MapiusMap *map, guint zoom, GtkWidget *label)
{
	gchar *str = g_strdup_printf ("Zoom: %d", zoom);
	gtk_label_set_text (GTK_LABEL (label), str);
	g_free (str);
}

static void
map_map_changed (MapiusMap *map, gchar *title, GtkWidget *label)
{
	gchar *str = g_strdup_printf ("Map: %s", title);
	gtk_label_set_text (GTK_LABEL (label), str);
	g_free (str);
}

int main (int argc, char **argv, char **env)
{
	GtkWidget *window;
	GtkWidget *container;
	GtkWidget *map;
	GtkWidget *loading_label;
	GtkWidget *zoom_label;
	GtkWidget *map_label;

	gtk_init (&argc, &argv);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (window), "Mapius");
	gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);
	gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);

	container = gtk_grid_new();
	gtk_container_add (GTK_CONTAINER (window), container);

	map = mapius_map_new();
	gtk_widget_set_hexpand (map, TRUE);
	gtk_widget_set_vexpand (map, TRUE);
	gtk_grid_attach (GTK_GRID(container), map, 0, 0, 3, 1);

	loading_label = gtk_label_new ("");
	gtk_widget_set_halign (loading_label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (container), loading_label, 0, 1, 1, 1);
	g_signal_connect (G_OBJECT (map), "loading", G_CALLBACK (map_loading), loading_label);

	zoom_label = gtk_label_new ("");
	gtk_widget_set_halign (zoom_label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (container), zoom_label, 1, 1, 1, 1);
	g_signal_connect (G_OBJECT (map), "zoom-changed", G_CALLBACK (map_zoom_changed), zoom_label);

	map_label = gtk_label_new ("");
	gtk_widget_set_halign (map_label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (container), map_label, 2, 1, 1, 1);
	g_signal_connect (G_OBJECT (map), "map-changed", G_CALLBACK (map_map_changed), map_label);

	g_signal_connect_swapped (G_OBJECT (window), "destroy", G_CALLBACK (gtk_main_quit), NULL);

	gtk_widget_show_all(window);

	gtk_main();

	return 0;
}
