#ifndef FILE_QUIVER_STOCK_ICONS_H
#define FILE_QUIVER_STOCK_ICONS_H

#include <gdk-pixbuf/gdk-pixdata.h>


#include "icons/quiver_icon.pixdata"
#include "icons/quiver_slideshow.pixdata"
#include "icons/quiver_browser_icon.pixdata"

#define QUIVER_STOCK_ICON      "quiver-icon"
#define QUIVER_STOCK_SLIDESHOW "quiver-slideshow"
#define QUIVER_STOCK_BROWSER   "quiver-browser-icon"


class QuiverStockIcons
{
public:
	static void Load();		
};

#endif
