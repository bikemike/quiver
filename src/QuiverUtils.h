#ifndef FILE_QUIVERUTILS_H
#define FILE_QUIVERUTILS_H

#include <gtk/gtk.h>

namespace QuiverUtils
{
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation);
}

#endif

