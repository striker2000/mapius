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

GtkWidget *map;

static void
on_maps_activate (GtkWidget *widget, gchar *map_id)
{
	mapius_map_change_map (MAPIUS_MAP (map), map_id);
}

int main (int argc, char **argv, char **env)
{
	GtkWidget *window;
	GtkWidget *container;
	GtkWidget *loading_label;
	GtkWidget *zoom_label;
	GtkWidget *map_label;

	gtk_init (&argc, &argv);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (window), "Mapius");
	gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);
	gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);

	GtkAccelGroup *accel_group = gtk_accel_group_new ();
	gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

	container = gtk_grid_new();
	gtk_container_add (GTK_CONTAINER (window), container);

	map = mapius_map_new();
	gtk_widget_set_hexpand (map, TRUE);
	gtk_widget_set_vexpand (map, TRUE);
	gtk_grid_attach (GTK_GRID(container), map, 0, 1, 3, 1);

	GtkWidget *maps_menu = gtk_menu_new ();
	GSList *i;
	MapiusMapInfo *info;
	for (i = MAPIUS_MAP (map)->maps; i; i = i->next) {
		info = (MapiusMapInfo *) i->data;
		GtkWidget *menu_item = gtk_menu_item_new_with_label (info->title);
		if (info->accel_key)
			gtk_widget_add_accelerator (menu_item, "activate", accel_group, info->accel_key, info->accel_mods, GTK_ACCEL_VISIBLE);
		g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK (on_maps_activate), info->id);
		gtk_menu_shell_append (GTK_MENU_SHELL (maps_menu), menu_item);
	}
	GtkWidget *root_menu = gtk_menu_item_new_with_label ("Maps");
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (root_menu), maps_menu);
	GtkWidget *menu_bar = gtk_menu_bar_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), root_menu);
	gtk_grid_attach (GTK_GRID (container), menu_bar, 0, 0, 1, 1);

	loading_label = gtk_label_new ("");
	gtk_widget_set_halign (loading_label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (container), loading_label, 0, 2, 1, 1);
	g_signal_connect (G_OBJECT (map), "loading", G_CALLBACK (map_loading), loading_label);

	zoom_label = gtk_label_new ("");
	gtk_widget_set_halign (zoom_label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (container), zoom_label, 1, 2, 1, 1);
	g_signal_connect (G_OBJECT (map), "zoom-changed", G_CALLBACK (map_zoom_changed), zoom_label);

	map_label = gtk_label_new ("");
	gtk_widget_set_halign (map_label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (container), map_label, 2, 2, 1, 1);
	g_signal_connect (G_OBJECT (map), "map-changed", G_CALLBACK (map_map_changed), map_label);

	g_signal_connect_swapped (G_OBJECT (window), "destroy", G_CALLBACK (gtk_main_quit), NULL);

	gtk_widget_show_all(window);

	gtk_main();

	return 0;
}
