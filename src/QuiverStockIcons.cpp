#include <config.h>
#include "QuiverStockIcons.h"
#include <gtk/gtk.h>

#include <gdk-pixbuf/gdk-pixdata.h>
/*
  GTK_ICON_SIZE_INVALID,
  GTK_ICON_SIZE_MENU,
  GTK_ICON_SIZE_SMALL_TOOLBAR,
  GTK_ICON_SIZE_LARGE_TOOLBAR,
  GTK_ICON_SIZE_BUTTON,
  GTK_ICON_SIZE_DND,
  GTK_ICON_SIZE_DIALOG
*/


#include "icons/quiver_icon_app.pixdata"
#include "icons/quiver_icon_slideshow.pixdata"
#include "icons/quiver_icon_browser.pixdata"
#include "icons/quiver_icon_rotate_cw.pixdata"
#include "icons/quiver_icon_rotate_ccw.pixdata"

typedef struct
{
	const char * id;
	const guint8* data;
	const guint size;
} icon;

icon icons[] = 
{
	{QUIVER_STOCK_APP,quiver_icon_app,sizeof(quiver_icon_app)},
	{QUIVER_STOCK_SLIDESHOW, quiver_icon_slideshow,sizeof(quiver_icon_slideshow)},
	{QUIVER_STOCK_BROWSER, quiver_icon_browser,sizeof(quiver_icon_browser)},
	{QUIVER_STOCK_ROTATE_CW, quiver_icon_rotate_cw,sizeof(quiver_icon_rotate_cw)},
	{QUIVER_STOCK_ROTATE_CCW, quiver_icon_rotate_ccw,sizeof(quiver_icon_rotate_ccw)},
};


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
	GError *error = NULL;
	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();

	for (unsigned int i=0;i< G_N_ELEMENTS(maemo_icons); ++i)
	{
		gint size;
		gint* sizes_orig = gtk_icon_theme_get_icon_sizes   (icon_theme, maemo_icons[i]);
		gint* sizes = sizes_orig;
		// construct the icons
		while (0 != (size = *sizes))
		{
			GdkPixbuf* expanded = gtk_icon_theme_load_icon (icon_theme,
				ICON_MAEMO_EXPANDED,
				size,
				GTK_ICON_LOOKUP_USE_BUILTIN,
				&error);

			GdkPixbuf* collapsed = gtk_icon_theme_load_icon (icon_theme,
				ICON_MAEMO_COLLAPSED,
				size,
				GTK_ICON_LOOKUP_USE_BUILTIN,
				&error);

			GdkPixbuf* pixbuf = gtk_icon_theme_load_icon (icon_theme,
				maemo_icons[i],
				size,
				GTK_ICON_LOOKUP_USE_BUILTIN,
				&error);

			if (NULL != expanded)
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
				gchar* name = g_strconcat(maemo_icons[i],MAEMO_EXPANDED);
				gtk_icon_theme_add_builtin_icon(name,size,composite_pixbuf);
				g_free(name);

				g_object_unref(composite_pixbuf);
				g_object_unref(expanded);
			}

			if (NULL != collapsed)
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

				
				gchar* name = g_strconcat(maemo_icons[i],MAEMO_COLLAPSED);
				gtk_icon_theme_add_builtin_icon(name,size,composite_pixbuf);
				g_free(name);

				g_object_unref(composite_pixbuf);
				g_object_unref(collapsed);
			}	

			g_object_unref(pixbuf);
			sizes++;
		}
		g_free(sizes_orig);
	}


}

#endif


void QuiverStockIcons::Load()
{
	//FIXME: we should load icons of different sizes
	GtkIconFactory* factory = gtk_icon_factory_new();	
	GdkPixbuf * pixbuf;
	GtkIconSet* icon_set;
	GError *error;
	error = NULL;
	
#ifdef QUIVER_MAEMO
	create_maemo_icons();
#endif

	for (unsigned int i=0;i< G_N_ELEMENTS(icons); ++i)
	{
		//pixbuf = gdk_pixbuf_new_from_inline (-1, icons[i].icon,FALSE,NULL);

		GdkPixdata pixdata;
		gdk_pixdata_deserialize (&pixdata,icons[i].size,icons[i].data,&error);
		pixbuf = gdk_pixbuf_from_pixdata(&pixdata,FALSE,&error);
/*		pixbuf = gdk_pixbuf_new_from_xpm_data    ((const char**)icons[i].xpm);
 */
		
		icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
		gtk_icon_factory_add(factory,icons[i].id,icon_set);
		
		
		gtk_icon_set_unref(icon_set);
		g_object_unref(pixbuf);
	}
	gtk_icon_factory_add_default (factory);
	g_object_unref(factory);
}

