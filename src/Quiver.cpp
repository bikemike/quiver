#include <config.h>
#include "Quiver.h"
#include <libgnomevfs/gnome-vfs.h>
#include "icons/quiver_icon.xpm"


using namespace std;

GtkTargetEntry Quiver::quiver_drag_target_table[] = {
		{ "STRING",     0, QUIVER_TARGET_STRING }, // STRING is used for legacy motif apps
		{ "text/plain", 0, QUIVER_TARGET_STRING },  // the real mime types to support
		 { "text/uri-list", 0, QUIVER_TARGET_URI },
	};


void Quiver::SignalDragDataReceived (GtkWidget *widget,GdkDragContext *drag_context, gint x,gint y,
                                            GtkSelectionData *data, guint info, guint time,gpointer user_data)
{
	gboolean retval = FALSE;
/*
  GDK_ACTION_DEFAULT = 1 << 0,
  GDK_ACTION_COPY    = 1 << 1,
  GDK_ACTION_MOVE    = 1 << 2,
  GDK_ACTION_LINK    = 1 << 3,
  GDK_ACTION_PRIVATE = 1 << 4,
  GDK_ACTION_ASK     = 1 << 5
  */
  	// we dont want to drag/drop in same widget
	//printf ("%d = %d?\n",drag_context->source_window,drag_context->dest_window);
	//printf ("%d = %d?\n",gdk_window_get_parent(drag_context->source_window),gdk_window_get_parent(drag_context->dest_window));
	//printf ("%d = %d?\n",gdk_window_get_toplevel(drag_context->source_window),gdk_window_get_toplevel(drag_context->dest_window));
  	if ( (data->length >= 0) && (data->format == 8) )
	{
		//cout << "data->length" << data->length << endl;
		//printf ("%s", (gchar *)data->data);
		string files = (gchar*)data->data;
		string separators = "\r\n";
		
		list<string> file_list;
		int n = files.length();
		int start, stop;

		start = files.find_first_not_of(separators);
		while ((start >= 0) && (start < n))
		{
			stop = files.find_first_of(separators, start);
			if ((stop < 0) || (stop > n)) stop = n;
			string item = files.substr(start, stop - start);

			file_list.push_back(item);
			
			start = files.find_first_not_of(separators, stop+1);
		}
		
		if (0 < file_list.size())
		{
			retval = TRUE;
			// if copy, we will add to the list
			if (GDK_ACTION_MOVE & drag_context->suggested_action)
			{
				// if move, we will add to the list
				m_pImageList->AddImageList(&file_list);
				CurrentImage();			
			}
			else
			{
				//create a new list
				//cout << "suggested action : GDK_ACTION_COPY! " << endl;
				m_pImageList->SetImageList(&file_list);
				CurrentImage();
			}
		}
	}
	
	gtk_drag_finish (drag_context, retval, FALSE, time);
	
}

void Quiver::signal_drag_begin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{
	((Quiver*)user_data)->SignalDragBegin(widget,drag_context,user_data);
}
void  Quiver::signal_drag_end(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{
	((Quiver*)user_data)->SignalDragEnd(widget,drag_context,user_data);
}


void  Quiver::SignalDragEnd(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{
	//re-enable drop
	gtk_drag_dest_set(m_pQuiverWindow,GTK_DEST_DEFAULT_ALL,
		quiver_drag_target_table, 3, (GdkDragAction)(GDK_ACTION_COPY|GDK_ACTION_MOVE));
}

void  Quiver::SignalDragBegin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{
	
	// disable drop 
	gtk_drag_dest_unset(m_pQuiverWindow);
	
	// TODO
	// set icon
	GdkPixbuf *thumb = m_pImageList->GetCurrent()->GetThumbnail();

	if (NULL != thumb)
	{
		gtk_drag_set_icon_pixbuf(drag_context,thumb,-2,-2);
		g_object_unref(thumb);
	}

}

void Quiver::signal_drag_data_received(GtkWidget *widget,GdkDragContext *drag_context, gint x,gint y,
                                            GtkSelectionData *data, guint info, guint time,gpointer user_data)
{
	((Quiver*)user_data)->SignalDragDataReceived(widget,drag_context,x,y,data,info,time,user_data);
}

void  Quiver::SignalDragDataDelete  (GtkWidget *widget,GdkDragContext *context,gpointer data)
{
  g_print ("Delete the data!\n");
}
void Quiver::SignalDragDataGet (GtkWidget *widget, GdkDragContext *context, 
	GtkSelectionData *selection_data, guint info, guint time,gpointer data)
{
	if (info == QUIVER_TARGET_STRING)
    {
		g_print ("drop the image uri as text/plain\n");
		if (m_pImageList->GetSize())
		{
    		gtk_selection_data_set (selection_data,
			    selection_data->target,
			    8, (const guchar*)m_pImageList->GetCurrent()->GetURI(),strlen(m_pImageList->GetCurrent()->GetURI()));
		}
	}
	else if (info == QUIVER_TARGET_URI)
	{
		g_print ("drop the image uri as text/uri\n");
		if (m_pImageList->GetSize())
		{
			//selection data set
			//context->suggested_action = GDK_ACTION_LINK;
    		gtk_selection_data_set (selection_data,
			    selection_data->target,
			    8, (const guchar*)m_pImageList->GetCurrent()->GetURI(),strlen(m_pImageList->GetCurrent()->GetURI()));
		}
	}
  	else
	{
		/*
		gtk_selection_data_set (selection_data,
				selection_data->target,
				8, (const guchar*)"I'm Data!", 9);
		*/
	}
}


void Quiver::signal_drag_data_get  (GtkWidget *widget, GdkDragContext *context, 
		GtkSelectionData *selection_data, guint info, guint time,gpointer user_data)
{
	((Quiver*)user_data)-> SignalDragDataGet  (widget,context,
		selection_data,info,time,user_data);
}

void  Quiver::signal_drag_data_delete  (GtkWidget *widget,GdkDragContext *context,gpointer user_data)
{
	((Quiver*)user_data)-> SignalDragDataDelete  (widget,context,user_data);
}

void Quiver::SetWindowTitle(string s)
{
	string title = "quiver - " + s;
	gtk_window_set_title (GTK_WINDOW(m_pQuiverWindow), title.c_str());	
}


void Quiver::CurrentImage()
{
	if (0 < m_pImageList->GetSize())
	{
		QuiverFile *f;
		f = m_pImageList->GetCurrent();
		
		stringstream ss;
		ss << " (" << m_pImageList->GetCurrentIndex() << " of " << m_pImageList->GetSize() << ")";
		SetWindowTitle(f->GetURI() + ss.str());
		
		if (NULL != f)
		{
			m_ImageLoader.LoadImage(*f);
		}
		
		// cache the next image if there is one
		if (m_pImageList->HasNext())
		{
			f = m_pImageList->PeekNext();
			if (NULL != f)
			{
				m_ImageLoader.CacheImage(*f);
			}
		}		
	}
}

void Quiver::NextImage()
{
	if (m_pImageList->HasNext())
	{
		QuiverFile *f;
		f = m_pImageList->GetNext();
		m_ImageLoader.LoadImage(*f);

		stringstream ss;
		ss << " (" << m_pImageList->GetCurrentIndex() << " of " << m_pImageList->GetSize() << ")";
		SetWindowTitle(f->GetURI() + ss.str());

		
		// cache the next image if there is one
		if (m_pImageList->HasNext())
		{
			f = m_pImageList->PeekNext();
			m_ImageLoader.CacheImage(*f);
		}		
		
	}
}
void Quiver::PreviousImage()
{
	if (m_pImageList->HasPrevious())
	{
		QuiverFile *f;
		f = m_pImageList->GetPrevious();
		m_ImageLoader.LoadImage(*f);
		
		stringstream ss;
		ss << " (" << m_pImageList->GetCurrentIndex() << " of " << m_pImageList->GetSize() << ")";
		SetWindowTitle(f->GetURI() + ss.str());
				
		// cache the previous image if there is one
		if (m_pImageList->HasPrevious())
		{
			f = m_pImageList->PeekPrevious();
			m_ImageLoader.CacheImage(*f);	
		}		
		
	}

}

gboolean Quiver::EventScroll( GtkWidget *widget, GdkEventScroll *event, gpointer data )
{
	if (GDK_SCROLL_UP == event->direction)
	{
		PreviousImage();
	}
	else if (GDK_SCROLL_DOWN == event->direction)
	{
		NextImage();
	}
	return TRUE;
}


gboolean Quiver::EventWindowState( GtkWidget *widget, GdkEventWindowState *event, gpointer data )
{
	//cout << "window state event" << endl;
	m_WindowState = event->new_window_state;
	//cout << event->new_window_state << " FS: " << GDK_WINDOW_STATE_FULLSCREEN <<endl;
	if (GDK_WINDOW_STATE_FULLSCREEN == event->new_window_state)
	{
		//cout << "window fullscreen state event" << endl;
//		gtk_window_set_decorated(GTK_WINDOW (m_pQuiverWindow),FALSE);

/*
 * 		GdkRectangle rect;
		
		int monitor = gdk_screen_get_monitor_at_window (gdk_screen_get_default (), m_pQuiverWindow->window);
		gdk_screen_get_monitor_geometry (gdk_screen_get_default (), monitor, &rect);
		gtk_window_resize(GTK_WINDOW (m_pQuiverWindow), rect.width, rect.height);
		//gtk_window_set_default_size (GTK_WINDOW (m_pQuiverWindow), rect.width, rect.height);
		gtk_window_move (GTK_WINDOW (m_pQuiverWindow), rect.x+14, rect.y+40);

		g_signal_stop_emission_by_name(widget,"window_state_event");

		//gtk_window_set_screen (GTK_WINDOW (m_pQuiverWindow), gtk_widget_get_screen (m_pQuiverWindow));
		//gtk_window_present (GTK_WINDOW (m_pQuiverWindow));		
		 */
	}
/*
	typedef struct {
  GdkEventType type;
  GdkWindow *window;
  gint8 send_event;
  GdkWindowState changed_mask;
  GdkWindowState new_window_state;
} GdkEventWindowState;

typedef enum
{
  GDK_WINDOW_STATE_WITHDRAWN  = 1 << 0,
  GDK_WINDOW_STATE_ICONIFIED  = 1 << 1,
  GDK_WINDOW_STATE_MAXIMIZED  = 1 << 2,
  GDK_WINDOW_STATE_STICKY     = 1 << 3,
  GDK_WINDOW_STATE_FULLSCREEN = 1 << 4,
  GDK_WINDOW_STATE_ABOVE      = 1 << 5,
  GDK_WINDOW_STATE_BELOW      = 1 << 6
} GdkWindowState;
*/
	return TRUE;
}
gboolean Quiver::event_window_state( GtkWidget *widget, GdkEventWindowState *event, gpointer data )
{
	return ((Quiver*)data)->EventWindowState(widget,event,data);
}

gboolean Quiver::event_scroll( GtkWidget *widget, GdkEventScroll *event, gpointer data )
{
	return ((Quiver*)data)->EventScroll(widget,event,data);
}
gboolean Quiver::EventButtonPress( GtkWidget *widget,GdkEventButton  *event,gpointer data )
{
	if (1 == event->button)
	{

	}
	else if (2 == event->button)
	{
		this->FullScreen();
	}
	return FALSE;
	
}

gboolean Quiver::EventButtonRelease ( GtkWidget *widget,GdkEventButton  *event,gpointer data )
{
	if (1 == event->button)
	{
	}
	else if (2 == event->button)
	{
		printf("fullscreen mode release\n");
		//gtk_window_fullscreen(m_pDrawingArea);
	}

	return FALSE;
	
}

gboolean Quiver::EventKeyPress( GtkWidget *widget, GdkEventKey *event, gpointer data )
{
	if (GDK_q == event->keyval || GDK_Q == event->keyval)
	{
		GdkEvent * e = gdk_event_new(GDK_DELETE);

		gboolean rval = FALSE;
		g_signal_emit_by_name(widget,"delete_event",e,&rval);
		gdk_event_free(e);
		
		if (!rval)//if (G_VALUE_HOLDS_BOOLEAN(return_value))
		{
			g_signal_emit_by_name(widget, "destroy");
		}
	}
	else if (GDK_f == event->keyval || GDK_F == event->keyval)
	{
		this->FullScreen();
	}
	else if (GDK_d == event->keyval || GDK_D == event->keyval || GDK_Delete == event->keyval )
	{
		// FIXME: create a ShowCursor method
		// and a HideCursor method
		gdk_window_set_cursor (m_pQuiverWindow->window, NULL);
		
		//delete the current images
		if (m_pImageList->GetSize())
		{
			char * for_display = gnome_vfs_format_uri_for_display(m_pImageList->GetCurrent()->GetURI());
			GtkWidget* dialog = gtk_message_dialog_new (GTK_WINDOW(widget),GTK_DIALOG_MODAL,
									GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO,"Are you sure you want to move the following image to the trash?");
			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog), for_display);
			g_free(for_display);
			gint rval = gtk_dialog_run(GTK_DIALOG(dialog));
		
			switch (rval)
			{
				case GTK_RESPONSE_YES:
				{
					//trash the file
					cout << "trashing file : " << m_pImageList->GetCurrent() << endl;
					//locate trash folder
					GnomeVFSURI * trash_vfs_uri = NULL;

					cout << "is this one val? "<< endl;
					GnomeVFSURI * near_vfs_uri = gnome_vfs_uri_new ( m_pImageList->GetCurrent()->GetURI() );
					gnome_vfs_find_directory (near_vfs_uri, GNOME_VFS_DIRECTORY_KIND_TRASH,&trash_vfs_uri, TRUE, TRUE, 0777);

					if (trash_vfs_uri != NULL) 
					{
						// we have trash
						gchar * short_name = gnome_vfs_uri_extract_short_name (near_vfs_uri);
						

						
						GnomeVFSURI *trash_file_vfs_uri = gnome_vfs_uri_append_file_name (trash_vfs_uri,short_name);
						g_free(short_name);
						gnome_vfs_uri_unref(trash_vfs_uri);
						gchar *trash_uri = gnome_vfs_uri_to_string (trash_file_vfs_uri,GNOME_VFS_URI_HIDE_NONE);
						cout << "trash uri: " << trash_uri << endl;
						g_free (trash_uri);
						
						GnomeVFSResult result = gnome_vfs_move_uri(near_vfs_uri,trash_file_vfs_uri,FALSE);
						if (GNOME_VFS_OK == result)
						{
							cout << "trashed the file " << endl;
							m_pImageList->RemoveCurrentImage();
							CurrentImage();
						}
						else
						{
							cout << "could not trash file " << endl;
						}
					}
					else
					{
						cout << "could not delete\n" << endl;
					}
					gnome_vfs_uri_unref(near_vfs_uri);
					break;
				}
				case GTK_RESPONSE_NO:
					//fall through
				default:
					// do not delete
					cout << "not trashing file : " << m_pImageList->GetCurrent() << endl;
					break;
			}
		
			gtk_widget_destroy(dialog);
		}
	
	}
	else if (GDK_z == event->keyval || GDK_Z == event->keyval)
	{
		if (Viewer::ZOOM_NONE == m_Viewer.GetZoomType())
		{
			cout << "zoom fit" << endl;
			gtk_drag_source_set (m_Viewer.GetWidget(), (GdkModifierType)(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK),
			   quiver_drag_target_table, 3, (GdkDragAction)(/*GDK_ACTION_COPY | GDK_ACTION_MOVE |*/ GDK_ACTION_LINK));
			m_Viewer.SetZoomType(Viewer::ZOOM_FIT);
			// set up dnd sourc

		}
		else
		{
			// unset dnd source
			gtk_drag_source_unset (m_Viewer.GetWidget());
			cout << "zoom 1:1" << endl;
			m_Viewer.SetZoomType(Viewer::ZOOM_NONE);
			if (!m_Viewer.GetHasFullPixbuf())
			{
				printf("not the same size\n");
				m_ImageLoader.ReloadImage(*m_pImageList->GetCurrent());
			}
		}
	}
	else if (GDK_Left == event->keyval || GDK_BackSpace == event->keyval)
	{
		PreviousImage();
	}
	else if (GDK_Right == event->keyval || GDK_space == event->keyval)
	{
		NextImage();
	}
	else if (GDK_r == event->keyval || GDK_L == event->keyval)
	{
		m_Viewer.Rotate(true);
	}
	else if (GDK_R == event->keyval || GDK_l == event->keyval)
	{
		m_Viewer.Rotate(false);
	}
	else if (GDK_v == event->keyval || GDK_H == event->keyval)
	{
		m_Viewer.Flip(false);
	}
	else if (GDK_h == event->keyval || GDK_V == event->keyval)
	{
		m_Viewer.Flip(true);
	}
	else 
	{
		printf("key pressed: 0x%04x\n",event->keyval);
	}
	
	return TRUE;
}


void Quiver::FullScreen()
{
		if (GDK_WINDOW_STATE_FULLSCREEN & m_WindowState)
		{
			gtk_window_unfullscreen(GTK_WINDOW(m_pQuiverWindow));
			// FIXME: create a ShowCursor method
			// and a HideCursor method
			gdk_window_set_cursor (m_pQuiverWindow->window, NULL);
		}
		else
		{
			// timeout to hide mouse cursor
			m_bTimeoutEventMotionNotifyRunning = true;
			g_timeout_add(1500,timeout_event_motion_notify,this);
			gtk_window_fullscreen(GTK_WINDOW(m_pQuiverWindow));
		}
}

gboolean Quiver::EventKeyRelease( GtkWidget *widget, GdkEventKey *event, gpointer data )
{
	//cout << "got key release event" << endl;
	return TRUE;
}

gboolean Quiver::event_key_press( GtkWidget *widget, GdkEventKey *event, gpointer data )
{
	return ((Quiver*)data)->EventKeyPress(widget,event,data);
}

gboolean Quiver::event_key_release( GtkWidget *widget, GdkEventKey *event, gpointer data )
{
	return ((Quiver*)data)->EventKeyRelease(widget,event,data);
}

gboolean Quiver::event_button_press ( GtkWidget *widget, GdkEventButton *event, gpointer data )
{
		return ((Quiver*)data)->EventButtonPress(widget,event,NULL);
}

gboolean  Quiver::event_button_release ( GtkWidget *widget, GdkEventButton *event, gpointer data )
{
	return ((Quiver*)data)->EventButtonRelease(widget,event,NULL);
}

gboolean Quiver::event_delete( GtkWidget *widget,GdkEvent  *event, gpointer   data )
{
	return ((Quiver*)data)->EventDelete(widget,event,data);
}

void Quiver::event_destroy( GtkWidget *widget, gpointer   data )
{
	return ((Quiver*)data)->EventDestroy(widget,data);
}

void Quiver::EventDestroy( GtkWidget *widget, gpointer   data )
{
	gtk_main_quit ();	
}

gboolean Quiver::EventDelete( GtkWidget *widget,GdkEvent  *event, gpointer   data )
{
	    /* If you return FALSE in the "delete_event" signal handler,
     * GTK will emit the "destroy" signal. Returning TRUE means
     * you don't want the window to be destroyed.
     * This is useful for popping up 'are you sure you want to quit?'
     * type dialogs. */

    /* Change TRUE to FALSE and the main window will be destroyed with
     * a "delete_event". */

	// are you sure you want to quit


	gboolean return_value = FALSE;
	SaveSettings();
	
	/*
	string s("Are you sure you want to quit?");
	GtkWidget* dialog = gtk_message_dialog_new (GTK_WINDOW(widget),GTK_DIALOG_MODAL,
							GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO,s.c_str());
	gint rval = gtk_dialog_run(GTK_DIALOG(dialog));

	switch (rval)
	{
		case GTK_RESPONSE_YES:
			return_value= FALSE;
			SaveSettings();
			break;
		case GTK_RESPONSE_NO:
			return_value = TRUE;
			break;
		default:
			return_value = TRUE;
		
	}
	
	gtk_widget_destroy(dialog);
	*/
	
    return return_value;
	
}
 


/*
gboolean Quiver::quiver_event_callback( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	printf("got an event %d\n", event->type);	
	return TRUE;
}
*/
/**
 * constructor
 * 
 */
Quiver::Quiver(list<string> images)
{
	m_pImageList = new ImageList();
	Init();
	SetImageList(images);
}

void Quiver::Init()
{
	m_bTimeoutEventMotionNotifyRunning = false;
	m_bTimeoutEventMotionNotifyMouseMoved = false;
	

	m_ImageLoader.Start();
	//initialize
			/* Create the main window */
	m_pQuiverWindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	if (LoadSettings())
	{	
		//set the size and position of the window
		//gtk_widget_set_uposition(quiver_window,m_iAppX,m_iAppY);
		gtk_window_move(GTK_WINDOW(m_pQuiverWindow),m_iAppX,m_iAppY);
		gtk_window_set_default_size (GTK_WINDOW(m_pQuiverWindow),m_iAppWidth,m_iAppHeight);

	}

	GdkPixbuf *pixbuf_icon = gdk_pixbuf_new_from_xpm_data ((const char**) quiver_icon_xpm);
	gtk_window_set_icon(GTK_WINDOW(m_pQuiverWindow),pixbuf_icon);
	g_object_unref(pixbuf_icon);

	
	/* Set up our GUI elements */
	GtkWidget *menubar;
	GtkWidget *menuitem;

	menubar =  gtk_menu_bar_new();
	
	menuitem = gtk_menu_item_new_with_mnemonic ("_File");
	gtk_menu_bar_append(menubar,menuitem);
	
	menuitem = gtk_menu_item_new_with_mnemonic ("_Edit");
	gtk_menu_bar_append(menubar,menuitem);

	menuitem = gtk_menu_item_new_with_mnemonic ("_View");
	gtk_menu_bar_append(menubar,menuitem);

	menuitem = gtk_menu_item_new_with_mnemonic ("_Tools");
	gtk_menu_bar_append(menubar,menuitem);

	menuitem = gtk_menu_item_new_with_mnemonic ("_Window");
	gtk_menu_bar_append(menubar,menuitem);



	menuitem = gtk_menu_item_new_with_mnemonic ("_Help");

	//menu_path = gtk_menu_factory_find (factory,  "<MyApp>/Help");
	//gtk_menu_item_right_justify(menu_path->widget);

	//If you do not use the MenuFactory, you should simply use:
	gtk_menu_item_right_justify(GTK_MENU_ITEM(menuitem));

	gtk_menu_bar_append(menubar,menuitem);

/*
 * 	GtkWidget *menu;
 * 	menu = gtk_menu_new();
 * 	gtk_menu_set_title(GTK_MENU(menu),"File");
	
	menuitem = gtk_menu_item_new_with_label  ("Open...");
	gtk_menu_append(menu,menuitem);
	gtk_menu_bar_append(menubar,menu);
	
 * 
 * */

	GtkWidget* statusbar;
	statusbar = gtk_statusbar_new ();
	
	GtkWidget* vbox;
	
	vbox = gtk_vbox_new(FALSE,0);
	
	
 	//gtk_widget_add_events (m_pQuiverWindow, GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);
	
	
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "window_state_event",
    			G_CALLBACK (Quiver::event_window_state), this);
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "key_press_event",
    			G_CALLBACK (Quiver::event_key_press), this);
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "key_release_event",
    			G_CALLBACK (Quiver::event_key_release), this);
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "delete_event",
    			G_CALLBACK (Quiver::event_delete), this);
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "scroll_event",
    			G_CALLBACK (Quiver::event_scroll), this);
	g_signal_connect (G_OBJECT (m_pQuiverWindow), "destroy",
				G_CALLBACK (Quiver::event_destroy), this);
	g_signal_connect (G_OBJECT (m_pQuiverWindow), "button_press_event",
				G_CALLBACK (Quiver::event_button_press), this);
	g_signal_connect (G_OBJECT (m_pQuiverWindow), "button_release_event",
				G_CALLBACK (Quiver::event_button_release), this);

	g_signal_connect (G_OBJECT (m_pQuiverWindow), "motion_notify_event",
				G_CALLBACK (Quiver::event_motion_notify), this);
				
	g_signal_connect (G_OBJECT (m_pQuiverWindow), "button_release_event",
				G_CALLBACK (Quiver::event_button_release), this);				

	
	/*
	gboolean expand,fill;
	guint padding;
	GtkPackType pack_type;

	gtk_container_add (GTK_CONTAINER (vbox),menubar);
	gtk_box_query_child_packing(GTK_BOX(vbox),menubar,&expand,&fill,&padding,&pack_type);
	gtk_box_set_child_packing(GTK_BOX(vbox),menubar,FALSE,fill,padding,pack_type);

	GtkWidget * w = m_Viewer.GetWidget();
	gtk_container_add (GTK_CONTAINER (vbox),w);

	gtk_container_add (GTK_CONTAINER (vbox),statusbar);
	gtk_box_query_child_packing(GTK_BOX(vbox),statusbar,&expand,&fill,&padding,&pack_type);
	gtk_box_set_child_packing(GTK_BOX(vbox),statusbar,FALSE,fill,padding,pack_type);

	gtk_container_add (GTK_CONTAINER (m_pQuiverWindow),vbox);
*/
	gtk_container_add (GTK_CONTAINER (m_pQuiverWindow),m_Viewer.GetWidget());
	
	/* Show the application window */
	
	gtk_container_set_border_width (GTK_CONTAINER(m_pQuiverWindow),0);
	//gtk_window_fullscreen(GTK_WINDOW(m_pQuiverWindow));

	gtk_widget_show_all (m_pQuiverWindow);
	/* Enter the main event loop, and wait for user interaction */

	//{ "application/x-rootwin-drop", 0, TARGET_ROOTWIN }

	gtk_drag_dest_set(m_pQuiverWindow,GTK_DEST_DEFAULT_ALL,
					quiver_drag_target_table, 3, (GdkDragAction)(GDK_ACTION_COPY|GDK_ACTION_MOVE));

	g_signal_connect (G_OBJECT (m_pQuiverWindow), "drag_data_received",
				GTK_SIGNAL_FUNC (Quiver::signal_drag_data_received), this);


	gtk_drag_source_set (m_Viewer.GetWidget(), (GdkModifierType)(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK),
			   quiver_drag_target_table, 3, (GdkDragAction)(/*GDK_ACTION_COPY | GDK_ACTION_MOVE | */GDK_ACTION_LINK));

	/*
	gtk_drag_source_set_icon (m_Viewer, 
				gtk_widget_get_colormap (window),
				drag_icon, drag_mask);
	*/		

	gtk_signal_connect (GTK_OBJECT (m_Viewer.GetWidget()), "drag_data_get",
		      GTK_SIGNAL_FUNC (Quiver::signal_drag_data_get), this);
	gtk_signal_connect (GTK_OBJECT (m_Viewer.GetWidget()), "drag_data_delete",
		      GTK_SIGNAL_FUNC (Quiver::signal_drag_data_delete), this);

	gtk_signal_connect (GTK_OBJECT (m_Viewer.GetWidget()), "drag_begin",
	      GTK_SIGNAL_FUNC (Quiver::signal_drag_begin), this);
	
	gtk_signal_connect (GTK_OBJECT (m_Viewer.GetWidget()), "drag_end",
	      GTK_SIGNAL_FUNC (Quiver::signal_drag_end), this);		  
/*	
	g_signal_connect (G_OBJECT (m_pQuiverWindow), "drag_drop",
				G_CALLBACK (signal_drag_drop), this);
			

	g_signal_connect (G_OBJECT (m_pQuiverWindow), "drag_motion",
				G_CALLBACK (signal_drag_motion), this);
*/
					
	m_ImageLoader.AddPixbufLoaderObserver(&m_Viewer);
}

/**
 * quiver main application loop
 */
int Quiver::Show()
{
	gtk_main ();
	
	/* The user lost interest */
	return 0;
	
}
Quiver::~Quiver()
{
	delete m_pImageList;
	//destructor
}

bool Quiver::LoadSettings()
{
	string quiver_rc = getenv("HOME") + string("/.quiver/quiver.rc");

	ifstream ifile;
	streamsize size = 1024;
	char line[1024];
	ifile.open(quiver_rc.c_str(),ifstream::in);
	
	int loaded = 0;

	if (ifile.is_open())
	{
		string x,y,width,height;
		
		while (!ifile.getline(line,size).eof() )
		{
			string variable = line;
			if ( 0 == variable.find("x=") )
			{
				x = variable.substr(2);
				m_iAppX = atoi(x.c_str());
				loaded = loaded | 1;
			}
			else if ( 0 == variable.find("y=") )
			{
				y = variable.substr(2);
				m_iAppY = atoi(y.c_str());
				loaded = loaded | 2;
			}
			else if ( 0 == variable.find("width=") )
			{
				width = variable.substr(6);	
				m_iAppWidth = atoi(width.c_str());
				loaded = loaded | 4;
			}
			else if ( 0 == variable.find("height=") )
			{
				height = variable.substr(7);
				m_iAppHeight = atoi(height.c_str());
				loaded = loaded | 8;
			}
		}
		ifile.close();
	}
	else
	{
		cout << "not able to open : " << quiver_rc << endl;
	}

	return (15 == loaded & 15);
}

void Quiver::SaveSettings()
{
	Timer t("Quiver::SaveSettings()");

	if (GDK_WINDOW_STATE_FULLSCREEN == m_WindowState)
	{
		cout << "was fullscreen" << endl;
		return;
	}

	string directory = getenv("HOME") + string("/.quiver/");
	string quiver_rc = directory + string("quiver.rc");
	
	GError *tmp_error;
	GnomeVFSHandle   *handle;
	GnomeVFSResult    result;
	
	tmp_error = NULL;
	
	// check if .quiver dir exists
	
	gchar* dir_uri = gnome_vfs_make_uri_from_shell_arg (directory.c_str());
	GnomeVFSFileInfo *dir_info = gnome_vfs_file_info_new ();
	result = gnome_vfs_get_file_info (dir_uri,dir_info,(GnomeVFSFileInfoOptions)(GNOME_VFS_FILE_INFO_DEFAULT));
	
	gnome_vfs_file_info_unref (dir_info);
	
	if (GNOME_VFS_OK != result)
	{
		// create the dir
		result = gnome_vfs_make_directory  (dir_uri, 0755);
	}
	g_free(dir_uri);
	
	if (GNOME_VFS_OK == result)
	{
		gchar* file_uri = gnome_vfs_make_uri_from_shell_arg (quiver_rc.c_str());

		result = gnome_vfs_create (&handle,file_uri,GNOME_VFS_OPEN_WRITE,false,0600);	

		if (GNOME_VFS_OK == result)
		{
			gtk_window_get_position(GTK_WINDOW(m_pQuiverWindow),&m_iAppX,&m_iAppY);
			gtk_window_get_size(GTK_WINDOW(m_pQuiverWindow),&m_iAppWidth,&m_iAppHeight);
			
			stringstream ss;			
			ss <<  "x=" << m_iAppX <<  endl;
			ss <<  "y=" << m_iAppY <<  endl;
			ss <<  "width=" << m_iAppWidth <<  endl;
			ss <<  "height=" << m_iAppHeight <<  endl;
			
			ss.str().c_str();
			GnomeVFSFileSize bytes_written;
			result = gnome_vfs_write (handle,ss.str().c_str(),ss.str().length(),&bytes_written);	
			if (GNOME_VFS_OK != result)
			{
				cout << "failed to save config file " << endl;
			}
			gnome_vfs_close(handle);
		}
		else
		{
			cout << " failed to open file for writing" << endl;
		}
		g_free(file_uri);
	}

}


void Quiver::SetImageList(list<string> &files)
{
	m_pImageList->SetImageList(&files);
	CurrentImage();
}

int main (int argc, char **argv)
{
	/* Initialize i18n support */
//	gtk_set_locale ();


  /* init threads */
	g_thread_init (NULL);
	gdk_threads_init ();
	gdk_threads_enter ();


	/* Initialize the widget set */
	gtk_init (&argc, &argv);
	
	list<string> files;
	for (int i =1;i<argc;i++)
	{	
		//printf ("command line %d : %s\n",i,argv[i]);
		files.push_back(argv[i]);
	}
	if (argc == 1)
	{	
		char buf[1024];
		getcwd(buf,sizeof(buf));
		files.push_back(buf);
	}
	//init gnome-vfs
	if (!gnome_vfs_init ()) {
		printf ("Could not initialize GnomeVFS\n");
		return 1;
	}
	
	Quiver quiver(files);
	//quiver.SetImageList(files);
	int rval = quiver.Show();	
	gdk_threads_leave();
	
	return rval;
}



gboolean Quiver::TimeoutEventMotionNotify(gpointer data)
{
	gboolean retval = TRUE;

	if (GDK_WINDOW_STATE_FULLSCREEN & m_WindowState)
	{
		if (m_bTimeoutEventMotionNotifyMouseMoved)
		{
			//do not remove the mouse cursor
			m_bTimeoutEventMotionNotifyMouseMoved = false;
		}
		else
		{
			GdkCursor *empty_cursor;
			GdkBitmap * empty_bitmap;
			char zero[] = { 0x0 };	
			GdkColor blank = { 0, 0, 0, 0 };	
		
			empty_bitmap = gdk_bitmap_create_from_data (NULL,zero,1,1);
			empty_cursor = gdk_cursor_new_from_pixmap (empty_bitmap,empty_bitmap,&blank,&blank,0,0);

			gdk_window_set_cursor (m_pQuiverWindow->window, empty_cursor);
			
			g_object_unref(empty_bitmap);
			gdk_cursor_unref (empty_cursor);
			
			//remove the mouse cursor
			retval = FALSE;
			m_bTimeoutEventMotionNotifyRunning = false;
		}
	}

	return retval;
}

gboolean Quiver::timeout_event_motion_notify (gpointer data)
{
	return ((Quiver*)data)->TimeoutEventMotionNotify(data);
}

gboolean Quiver::EventMotionNotify( GtkWidget *widget, GdkEventMotion *event, gpointer data )
{
	m_bTimeoutEventMotionNotifyMouseMoved = true;
	
	if (!m_bTimeoutEventMotionNotifyRunning)
	{
		gdk_window_set_cursor (m_pQuiverWindow->window, NULL);
		m_bTimeoutEventMotionNotifyRunning = true;
		g_timeout_add(1500,timeout_event_motion_notify,this);
	}
	return FALSE;
	
}
gboolean Quiver::event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data )
{
	return ((Quiver*)data)->EventMotionNotify(widget,event,NULL);
}

