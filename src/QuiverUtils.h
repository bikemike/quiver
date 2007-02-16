#ifndef FILE_QUIVERUTILS_H
#define FILE_QUIVERUTILS_H

#include <gtk/gtk.h>

namespace QuiverUtils
{
	GtkAction* GetAction(GtkUIManager* ui,const char * action_name);
	void SetActionsSensitive(GtkUIManager *pUIManager, gchar** actions, gint n_actions, gboolean bSensitive);
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation);
	
	void DisconnectUnmodifiedAccelerators(GtkUIManager *pUIManager);
	void ConnectUnmodifiedAccelerators(GtkUIManager *pUIManager);
}

#endif

