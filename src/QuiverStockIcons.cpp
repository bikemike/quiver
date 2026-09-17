#include <config.h>
#include "QuiverStockIcons.h"
#include <gtk/gtk.h>


void QuiverStockIcons::Load()
{
	GdkDisplay* display = gdk_display_get_default();
	if (NULL == display)
	{
		return;
	}

	GtkIconTheme* icon_theme = gtk_icon_theme_get_for_display(display);
	std::string icon_dir = quiver_get_resource_path("icons");
	gtk_icon_theme_add_search_path(icon_theme, icon_dir.c_str());
}
