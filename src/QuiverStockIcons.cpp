#include <config.h>
#include "QuiverStockIcons.h"
#include <gtk/gtk.h>


#define ICON_DIR  QUIVER_DATADIR "/icons"

void QuiverStockIcons::Load()
{
	GdkDisplay* display = gdk_display_get_default();
	if (NULL == display)
	{
		return;
	}

	GtkIconTheme* icon_theme = gtk_icon_theme_get_for_display(display);
	gtk_icon_theme_add_search_path(icon_theme, ICON_DIR);
}
