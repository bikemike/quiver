#ifndef FILE_QUIVERUTILS_H
#define FILE_QUIVERUTILS_H

#include <gtk/gtk.h>

namespace QuiverUtils
{
	// GtkAction* GetAction(GtkUIManager* ui,const char * action_name); // GtkUIManager/GtkAction deprecated
	// void SetActionsSensitive(GtkUIManager *pUIManager, const gchar** actions, gint n_actions, gboolean bSensitive); // GtkUIManager/GtkAction deprecated
	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation);
	
	// void DisconnectUnmodifiedAccelerators(GtkUIManager *pUIManager); // GtkUIManager deprecated
	// void ConnectUnmodifiedAccelerators(GtkUIManager *pUIManager); // GtkUIManager deprecated

    // Placeholder for new action/menu utilities if needed.
    // For example:
    // GAction* get_action_from_widget(GtkWidget* widget, const char* action_name);
    // void set_action_enabled(GtkWidget* widget, const char* action_name, gboolean enabled);
}

#endif

