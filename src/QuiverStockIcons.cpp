#include <config.h>
#include "QuiverStockIcons.h"
#include <gtk/gtk.h>

#include <gdk-pixbuf/gdk-pixdata.h>




#ifdef QUIVER_MAEMO

char* maemo_icons[] = 
{
	ICON_MAEMO_DEVICE,
	ICON_MAEMO_AUDIO,
	ICON_MAEMO_DOCUMENTS,
	ICON_MAEMO_GAMES,
	ICON_MAEMO_IMAGES,
	ICON_MAEMO_VIDEOS,
	ICON_MAEMO_MMC,
	ICON_MAEMO_FOLDER_CLOSED,
	ICON_MAEMO_FOLDER_OPEN,
};

static void create_maemo_icons()
{
	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();

	for (unsigned int i=0;i< G_N_ELEMENTS(maemo_icons); ++i)
	{
		gint size;
		gint* sizes_orig = gtk_icon_theme_get_icon_sizes   (icon_theme, maemo_icons[i]);
		gint* sizes = sizes_orig;
		// construct the icons
		if (NULL == sizes)
		{
			continue;
		}

		while (0 != (size = *sizes))
		{

			GdkPixbuf* expanded = gtk_icon_theme_load_icon (icon_theme,
				ICON_MAEMO_EXPANDED,
				size,
				GTK_ICON_LOOKUP_USE_BUILTIN,
				NULL);

			GdkPixbuf* collapsed = gtk_icon_theme_load_icon (icon_theme,
				ICON_MAEMO_COLLAPSED,
				size,
				GTK_ICON_LOOKUP_USE_BUILTIN,
				NULL);

			GdkPixbuf* pixbuf = gtk_icon_theme_load_icon (icon_theme,
				maemo_icons[i],
				size,
				GTK_ICON_LOOKUP_USE_BUILTIN,
				NULL);

			if (NULL != expanded && NULL != pixbuf)
			{
				GdkPixbuf* composite_pixbuf = gdk_pixbuf_copy(pixbuf);
				
				// composite the two pixbufs
				gdk_pixbuf_composite (expanded,
					composite_pixbuf,
					0,
					0,
					gdk_pixbuf_get_width(pixbuf),
					gdk_pixbuf_get_height(pixbuf),
					0,
					0,
					1.,
					1.,
					GDK_INTERP_NEAREST,
					255);
				gchar* name = g_strconcat(maemo_icons[i],MAEMO_EXPANDED,NULL);
				gtk_icon_theme_add_builtin_icon(name,size,composite_pixbuf);
				g_free(name);

				g_object_unref(composite_pixbuf);
				g_object_unref(expanded);
			}

			if (NULL != collapsed && NULL != pixbuf)
			{
				GdkPixbuf* composite_pixbuf = gdk_pixbuf_copy(pixbuf);
				
				// composite the two pixbufs
				gdk_pixbuf_composite (collapsed,
					composite_pixbuf,
					0,
					0,
					gdk_pixbuf_get_width(pixbuf),
					gdk_pixbuf_get_height(pixbuf),
					0,
					0,
					1.,
					1.,
					GDK_INTERP_NEAREST,
					255);

				
				gchar* name = g_strconcat(maemo_icons[i],MAEMO_COLLAPSED,NULL);
				gtk_icon_theme_add_builtin_icon(name,size,composite_pixbuf);
				g_free(name);

				g_object_unref(composite_pixbuf);
				g_object_unref(collapsed);
			}	

			if (NULL != pixbuf)
			{
				g_object_unref(pixbuf);
			}
			sizes++;
		}
		g_free(sizes_orig);
	}


}

#endif


void QuiverStockIcons::Load()
{
#ifdef QUIVER_MAEMO
	create_maemo_icons();
#endif
}

