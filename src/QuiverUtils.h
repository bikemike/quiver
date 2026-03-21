#ifndef FILE_QUIVERUTILS_H
#define FILE_QUIVERUTILS_H

#include <gtk/gtk.h>

namespace QuiverUtils
{
	gpointer* GetAction(gpointer* ui,const char * action_name);
	void SetActionsSensitive(gpointer *pUIManager, const gchar** actions, gint n_actions, gboolean bSensitive);
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation);
	
	void DisconnectUnmodifiedAccelerators(gpointer *pUIManager);
	void ConnectUnmodifiedAccelerators(gpointer *pUIManager);
}

#endif

