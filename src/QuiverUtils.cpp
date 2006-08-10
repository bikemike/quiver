#include <config.h>
#include "QuiverUtils.h"


namespace QuiverUtils
{
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


	void DisconnectUnmodifiedAccelerators(GtkUIManager *pUIManager)
	{
		GList * action_groups = gtk_ui_manager_get_action_groups(pUIManager);
		GtkAction * action = NULL;
		while (NULL != action_groups)
		{
			GList *actions_list = gtk_action_group_list_actions((GtkActionGroup*)action_groups->data);
			GList *actions = actions_list;
			while (NULL != actions)
			{
				action = (GtkAction*)actions->data;
				GtkAccelKey accel_key = {0};
				
				if (gtk_accel_map_lookup_entry(gtk_action_get_accel_path(action),&accel_key))
				{
					// list of modifiers to check
					guint mask = 0;
					
					mask |= GDK_CONTROL_MASK;
					mask |= GDK_MOD1_MASK;    // normally alt
					mask |= GDK_MOD2_MASK;
					mask |= GDK_MOD3_MASK;
					mask |= GDK_MOD4_MASK;
					mask |= GDK_MOD5_MASK;

					if (0 == (mask & accel_key.accel_mods) && 0 != accel_key.accel_key)
					{
						gchar *accel_label;
						accel_label = gtk_accelerator_get_label(accel_key.accel_key,accel_key.accel_mods);
						printf("disable accel key: %s - %d\n",accel_label, accel_key.accel_key);
						gtk_action_disconnect_accelerator(action);
						g_free(accel_label);
					}
				}
				actions = g_list_next(actions);
			}
			
			g_list_free(actions_list);
			
			action_groups = g_list_next(action_groups);
		}
		
	}
	
	void ConnectUnmodifiedAccelerators(GtkUIManager *pUIManager)
	{
		GList * action_groups = gtk_ui_manager_get_action_groups(pUIManager);
		GtkAction * action = NULL;
		while (NULL != action_groups)
		{
			GList *actions_list = gtk_action_group_list_actions((GtkActionGroup*)action_groups->data);
			GList *actions = actions_list;
			while (NULL != actions)
			{
				action = (GtkAction*)actions->data;
				GtkAccelKey accel_key = {0};
				
				if (gtk_accel_map_lookup_entry(gtk_action_get_accel_path(action),&accel_key))
				{
					// list of modifiers to check
					guint mask = 0;
					
					mask |= GDK_CONTROL_MASK;
					mask |= GDK_MOD1_MASK;    // normally alt
					mask |= GDK_MOD2_MASK;
					mask |= GDK_MOD3_MASK;
					mask |= GDK_MOD4_MASK;
					mask |= GDK_MOD5_MASK;

					if (0 == (mask & accel_key.accel_mods) && 0 != accel_key.accel_key)
					{
						gchar *accel_label;
						accel_label = gtk_accelerator_get_label(accel_key.accel_key,accel_key.accel_mods);
						printf("enable accel key: %s - %d\n",accel_label, accel_key.accel_key);
						gtk_action_connect_accelerator(action);
						g_free(accel_label);
					}
				}
				actions = g_list_next(actions);
			}
			
			g_list_free(actions_list);
			
			action_groups = g_list_next(action_groups);
		}
		
	}
}

