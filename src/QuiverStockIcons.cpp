#include <config.h>
#include "QuiverStockIcons.h"
#include <gtk/gtk.h>
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


void QuiverStockIcons::Load()
{
	//FIXME: we should load icons of different sizes
	GtkIconFactory* factory = gtk_icon_factory_new();	
	GdkPixbuf * pixbuf;
	GtkIconSet* icon_set;
	GError *error;
	error = NULL;
	
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
