#include <config.h>
#include "QuiverUtils.h"

static bool g_bAcceleratorsDisabled = false;

#define N_LOOPS 10

namespace QuiverUtils
{
	/* GtkUIManager and GtkAction are deprecated in GTK4.
	   These functions need to be reimplemented using GActionMap, GAction, GMenuModel, etc.
	   For now, they are commented out to allow compilation to proceed.
	*/
/*
	GtkAction* GetAction(GtkUIManager* ui,const char * action_name)
	{
		// In GTK4, you would typically get a GActionMap from a GtkWidget (e.g. GtkApplicationWindow)
		// or GtkApplication and then lookup the GAction.
		// GActionMap *action_map = gtk_widget_get_action_map(GTK_WIDGET(widget_with_map), name);
		// GAction *action = g_action_map_lookup_action(action_map, action_name);
		// return G_IS_ACTION(action) ? action : NULL; // This would return GAction*, not GtkAction*
		return NULL; // Placeholder
	}


	void SetActionsSensitive(GtkUIManager *pUIManager, const gchar** actions, gint n_actions, gboolean bSensitive)
	{
		gint i;
		for ( i = 0; i < n_actions; i++)
		{
			// GAction* action = GetActionFromSomewhere(actions[i]); // New helper needed
			// if (action) {
			// 	 g_simple_action_set_enabled(G_SIMPLE_ACTION(action), bSensitive);
			// }
		}
	}
*/

	GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation)
	{
		//printf("orientation is: %d\n",orientation);

		/*
		  1        2       3      4         5            6           7          8
		888888  888888      88  88      8888888888  88                  88  8888888888
		88          88      88  88      88  88      88  88          88  88      88  88
		8888      8888    8888  8888    88          8888888888  8888888888          88
		88          88      88  88
		88          88  888888  888888
		1 = no rotation
		2 = flip h
		3 = rotate 180
		4 = flip v
		5 = flip v, rotate 90
		6 = rotate 90
		7 = flip v, rotate 270
		8 = rotae 270


		*/
		//get rotaiton

		GdkPixbuf * modified = NULL;

		switch (orientation)
		{
			case 1:
				//1 = no rotation
				break;
			case 2:
				//2 = flip h
				modified = gdk_pixbuf_flip(pixbuf,TRUE);
				break;
			case 3:
				//3 = rotate 180
				modified = gdk_pixbuf_rotate_simple(pixbuf,(GdkPixbufRotation)180);
				break;
			case 4:
				//4 = flip v
				modified = gdk_pixbuf_flip(pixbuf,FALSE);
				break;
			case 5:
				//5 = flip v, rotate 90
				{
					GdkPixbuf *tmp = gdk_pixbuf_flip(pixbuf,FALSE);
					modified = gdk_pixbuf_rotate_simple(tmp,GDK_PIXBUF_ROTATE_CLOCKWISE);
					g_object_unref(tmp);
				}
				break;
			case 6:
				modified = gdk_pixbuf_rotate_simple(pixbuf,GDK_PIXBUF_ROTATE_CLOCKWISE);
				break;
			case 7:
				//7 = flip v, rotate 270
				{
					GdkPixbuf *tmp = gdk_pixbuf_flip(pixbuf,FALSE);
					modified = gdk_pixbuf_rotate_simple(tmp,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
					g_object_unref(tmp);
				}
				break;
			case 8:
				//8 = rotae 270
				modified = gdk_pixbuf_rotate_simple(pixbuf,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
			default:
				break;
		}
		return modified;
	}

/*
	void DisconnectUnmodifiedAccelerators(GtkUIManager *pUIManager)
	{
		// GtkUIManager is deprecated. Accelerators are handled differently in GTK4,
		// often via GtkApplication, GtkBuilder, or GtkMenuModel.
		// This function likely needs to be removed or completely rethought.
		// For example, one might iterate GActions and remove their accelerators
		// from the GtkApplication or specific widgets if they were added programmatically.
	}

	void ConnectUnmodifiedAccelerators(GtkUIManager *pUIManager)
	{
		// Similar to DisconnectUnmodifiedAccelerators, this is deprecated.
		// Accelerators would be (re)connected using GTK4 mechanisms.
	}
*/
}
