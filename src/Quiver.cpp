#include <config.h>
#include "quiver-i18n.h"
#include "Quiver.h"
//#include "QuiverUI.h"
#include <libgnomevfs/gnome-vfs.h>
#include "QuiverStockIcons.h"



extern "C" {
#define JPEG_INTERNALS
#include <jpeglib.h>
}
using namespace std;

// helper functions

GtkAction* GetAction(GtkUIManager* ui,const char * action_name)
{
	GList * action_groups = gtk_ui_manager_get_action_groups(ui);
	GtkAction * action = NULL;
	while (NULL != action_groups)
	{
		action = gtk_action_group_get_action (GTK_ACTION_GROUP(action_groups->data),action_name);
		if (NULL != action)
		{
			break;
		}                      
		action_groups = g_list_next(action_groups);
	}

	return action;
}


/*
typedef struct {
  const gchar *name;
  const gchar *stock_id;
  const gchar *label;
  const gchar *accelerator;
  const gchar *tooltip;
  gint   value; 
} GtkRadioActionEntry;
*/
/*
GtkRadioActionEntry Quiver::action_entries_radio[] = {

};
*/
static void action_view_properties(GtkAction *action,gpointer data)
{
	((Quiver*)data)->ActionViewProperties(action,data);
}

void Quiver::ActionViewProperties(GtkAction *action,gpointer data)
{
	if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
	{
		gtk_widget_show(m_pNBProperties);
	}
	else
	{
		gtk_widget_hide(m_pNBProperties);
	}
	printf("hi!\n");
}

#define QUIVER_ACTION_SLIDESHOW  "SlideShow"
#define QUIVER_ACTION_FULLSCREEN "FullScreen"

#define QUIVER_ACTION_UI_MODE_VIEWER "UIModeViewer"
#define QUIVER_ACTION_UI_MODE_BROWSER "UIModeBrowser"

#define QUIVER_UI_MODE_BROWSER   1
#define QUIVER_UI_MODE_VIEWER    2

	
char * quiver_ui_main =
"<ui>"
"	<menubar name='MenubarMain'>"
"		<menu action='MenuFile'>"
"			<menuitem action='FileOpen'/>"
"			<menuitem action='FileOpenFolder'/>"
"			<menuitem action='FileOpenLocation'/>"
"			<separator/>"
"			<menuitem action='FileSave'/>"
"			<separator/>"
"			<placeholder action='FileMenuAdditions' />"
"			<separator/>"
"			<menuitem action='Quit'/>"
"		</menu>"
"		<menu action='MenuEdit'>"
"			<placeholder name='UndoRedo'/>"
"			<separator/>"
"			<placeholder name='CopyPaste'/>"
"			<separator/>"
"			<placeholder name='Selection'/>"
"			<separator/>"
"			<placeholder name='Trash'/>"
"			<separator/>"
"			<menuitem action='Preferences'/>"
"		</menu>"
"		<menu action='MenuView'>"
"			<placeholder name='UIModeItems'/>"
"			<menuitem action='ViewMenubar'/>"
"			<menuitem action='ViewToolbarMain'/>"
"			<menuitem action='ViewProperties'/>"
"			<placeholder name='UIItems'/>"
"		 	<menuitem action='ViewStatusbar'/>"
"			<separator/>"
"			<menuitem action='FullScreen'/>"
"			<menuitem action='SlideShow'/>"
"			<separator/>"
"			<placeholder name='Zoom'/>"
"			<separator/>"
"		</menu>"
"		<menu action='MenuImage'>"
"			<menuitem action='RotateCW'/>"
"			<menuitem action='RotateCCW'/>"
"			<separator/>"
"			<menuitem action='FlipH'/>"
"			<menuitem action='FlipV'/>"
"		</menu>"
"		<menu action='MenuGo'>"
"			<placeholder name='ImageNavigation'/>"
"			<separator/>"
"			<placeholder name='FolderNavigation'/>"
"			<separator/>"
"		</menu>"
"		<menu action='MenuBookmarks'>"
"			<menuitem action='BookmarksAdd'/>"
"			<menuitem action='BookmarksEdit'/>"
"		</menu>"
"		<menu action='MenuTools'>"
"		</menu>"
"		<menu action='MenuWindow'>"
"		</menu>"
"		<menu action='MenuHelp'>"
"			<menuitem action='About'/>"
"		</menu>"
"	</menubar>"
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='UIModeItems'/>"
"		<placeholder name='NavToolItems'/>"
"		<separator/>"
"		<toolitem action='FullScreen'/>"
"		<separator/>"
"		<toolitem action='SlideShow'/>"
"		<separator/>"
"		<toolitem action='ImageTrash'/>"
"		<separator/>"
"	</toolbar>"
"</ui>";


char *quiver_ui_browser =
"<ui>"
"	<menubar name='MenubarMain'>"
"		<menu action='MenuView'>"
"			<placeholder name='UIModeItems'>"
"				<separator/>"
"				<menuitem action='UIModeViewer'/>"
"				<separator/>"
"			</placeholder>"
"		</menu>"
"	</menubar>"
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='UIModeItems'>"
"			<separator/>"
"			<toolitem action='UIModeViewer'/>"
"			<separator/>"
"		</placeholder>"
"		<placeholder name='NavToolItems'>"
"			<separator/>"
"			<separator/>"
"		</placeholder>"
"	</toolbar>"
"</ui>";


char *quiver_ui_viewer =
"<ui>"
"	<menubar name='MenubarMain'>"
"		<menu action='MenuView'>"
"			<placeholder name='UIModeItems'>"
"				<separator/>"
"				<menuitem action='UIModeBrowser'/>"
"				<separator/>"
"			</placeholder>"
"		</menu>"
"	</menubar>"
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='UIModeItems'>"
"			<separator/>"
"			<toolitem action='UIModeBrowser'/>"
"			<separator/>"
"		</placeholder>"
"		<placeholder name='NavToolItems'>"
"			<separator/>"
"			<toolitem action='ImageFirst'/>"
"			<toolitem action='ImagePrevious'/>"
"			<toolitem action='ImageNext'/>"
"			<toolitem action='ImageLast'/>"
"			<separator/>"
"		</placeholder>"
"	</toolbar>"
"</ui>";

GtkToggleActionEntry Quiver::action_entries_toggle[] = {
	{ QUIVER_ACTION_FULLSCREEN, GTK_STOCK_FULLSCREEN, N_("_Full Screen"), NULL, N_("Toggle Full Screen Mode"), G_CALLBACK(action_full_screen),FALSE},
	{ QUIVER_ACTION_SLIDESHOW,QUIVER_STOCK_SLIDESHOW, N_("_Slide Show"), NULL, N_("Toggle Slide Show"), G_CALLBACK(action_slide_show),FALSE},	
	{ "ViewMenubar", GTK_STOCK_ZOOM_IN,"Menubar", "<Control><Shift>M", "Show/Hide the Menubar", G_CALLBACK(NULL),TRUE},
	{ "ViewToolbarMain", GTK_STOCK_ZOOM_IN,"Toolbar", "<Control><Shift>T", "Show/Hide the Toolbar", G_CALLBACK(NULL),TRUE},
	{ "ViewStatusbar", GTK_STOCK_ZOOM_IN,"Statusbar", "<Control><Shift>S", "Show/Hide the Statusbar", G_CALLBACK(NULL),TRUE},
	{ "ViewProperties", GTK_STOCK_PROPERTIES,"Properties", "<Alt>Return", "Show/Hide Image Properties", G_CALLBACK(action_view_properties),FALSE},
};

GtkActionEntry Quiver::action_entries[] = {
	
	{ "MenuFile", NULL, N_("_File") },
	{ "MenuZoom", NULL, N_("Zoom")},
	{ "MenuTransform", NULL, N_("Transform")},
	{ "MenuEdit", NULL, N_("_Edit") },
	{ "MenuView", NULL, N_("_View") },
	{ "MenuImage", NULL, N_("_Image") },
	{ "MenuGo", NULL, N_("_Go") },
	{ "MenuTools", NULL, N_("_Tools") },
	{ "MenuWindow", NULL, N_("_Window") },
	{ "MenuHelp", NULL, N_("_Help") },

	{ "UIModeBrowser",GTK_STOCK_SELECT_COLOR , "_Browser", "<Control>B", "Browse Images", G_CALLBACK(action_ui_mode_change)},
	{ "UIModeViewer", QUIVER_STOCK_ICON, "_Viewer", "<Control>B", "View Image", G_CALLBACK(action_ui_mode_change)},

	{ "FileOpen", GTK_STOCK_OPEN, "_Open", "<Control>O", "Open an image", G_CALLBACK(NULL)},
	{ "FileOpenFolder", GTK_STOCK_OPEN, "Open _Folder", "<Control>O", "Open a folder", G_CALLBACK( NULL )},
	{ "FileOpenLocation", GTK_STOCK_OPEN, "Open _Location", "<Control>L", "Open a location", G_CALLBACK( NULL )},
	{ "FileSave", GTK_STOCK_SAVE, "_Save", "<Control>S", "Save the Image", G_CALLBACK(action_file_save)},
	{ "Quit", GTK_STOCK_QUIT, "_Quit", "<Alt>F4", "Quit quiver", G_CALLBACK( action_quit )},

	{ "Preferences", GTK_STOCK_PREFERENCES, "_Preferences", "<Control>p", "Edit quiver preferences", G_CALLBACK(NULL)},


	{ "RotateCW", GTK_STOCK_GO_BACK, "_Rotate Clockwise", "", "Rotate Clockwise", G_CALLBACK(NULL)},
	{ "RotateCCW", GTK_STOCK_GO_BACK, "Rotate _Counterclockwise", "", "Rotate Counterclockwise", G_CALLBACK(NULL)},
	{ "FlipH", GTK_STOCK_GO_BACK, "Flip _Horizontally", "", "Flip Horizontally", G_CALLBACK(NULL)},
	{ "FlipV", GTK_STOCK_GO_BACK, "Flip _Vertically", "", "Flip Vertically", G_CALLBACK(NULL)},

/*
	{ "ImagePrevious", GTK_STOCK_GO_BACK, "_Previous Image", "BackSpace", "Go to previous image", G_CALLBACK(action_image_previous)},
	{ "ImageNext", GTK_STOCK_GO_FORWARD, "_Next Image", "space", "Go to next image", G_CALLBACK(action_image_next)},
	{ "ImageFirst", GTK_STOCK_GOTO_FIRST, "_First Image", "Home", "Go to first image", G_CALLBACK(action_image_first)},
	{ "ImageLast", GTK_STOCK_GOTO_LAST, "_Last Image", "End", "Go to last image", G_CALLBACK(action_image_last)},
*/
	{ "MenuBookmarks", NULL, "_Bookmarks" },
	{ "BookmarksAdd", GTK_STOCK_ADD, "_Add Bookmark", "", "Add a bookmark", G_CALLBACK(NULL)},
	{ "BookmarksEdit", GTK_STOCK_EDIT, "_Edit Bookmarks", "", "Edit the bookmarks", G_CALLBACK(NULL)},

	{ "About", GTK_STOCK_ABOUT, "_About", "", "About quiver", G_CALLBACK( action_about )},

};



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
				m_ImageList.Add(&file_list);
			}
			else
			{
				//create a new list
				//cout << "suggested action : GDK_ACTION_COPY! " << endl;
				m_ImageList.SetImageList(&file_list);
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
	GdkPixbuf *thumb = m_ImageList.GetCurrent().GetThumbnail();

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
		if (m_ImageList.GetSize())
		{
    		gtk_selection_data_set (selection_data,
			    selection_data->target,
			    8, (const guchar*)m_ImageList.GetCurrent().GetURI(),strlen(m_ImageList.GetCurrent().GetURI()));
		}
	}
	else if (info == QUIVER_TARGET_URI)
	{
		g_print ("drop the image uri as text/uri\n");
		if (m_ImageList.GetSize())
		{
			//selection data set
			//context->suggested_action = GDK_ACTION_LINK;
    		gtk_selection_data_set (selection_data,
			    selection_data->target,
			    8, (const guchar*)m_ImageList.GetCurrent().GetURI(),strlen(m_ImageList.GetCurrent().GetURI()));
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

void Quiver::ImageChanged()
{
	stringstream ss;
	QuiverFile f = m_ImageList.GetCurrent();
	ss << " (" << m_ImageList.GetCurrentIndex()+1 << " of " << m_ImageList.GetSize() << ")";
	string s = f.GetFileInfo()->name;
	SetWindowTitle( f.GetFilePath() );
	m_Statusbar.SetPosition(m_ImageList.GetCurrentIndex()+1,m_ImageList.GetSize());
	m_ExifView.SetQuiverFile(f);
}


gboolean Quiver::EventWindowState( GtkWidget *widget, GdkEventWindowState *event, gpointer data )
{
	gboolean bFullscreen = FALSE;
	//cout << "window state event" << endl;
	m_WindowState = event->new_window_state;
	//cout << event->new_window_state << " FS: " << GDK_WINDOW_STATE_FULLSCREEN <<endl;
	if (GDK_WINDOW_STATE_FULLSCREEN == event->new_window_state)
	{
		gtk_widget_hide(m_pToolbar);
		gtk_widget_hide(m_pMenubar);
		gtk_widget_hide(m_Statusbar.GetWidget());
		
		//m_Viewer.HideBorder();
		m_bTimeoutEventMotionNotifyRunning = true;
		g_timeout_add(1500,timeout_event_motion_notify,this);
		
		
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
		 bFullscreen = TRUE;
	}
	else
	{
		// show widgets
		gtk_widget_show(m_pToolbar);
		gtk_widget_show(m_pMenubar);
		gtk_widget_show(m_Statusbar.GetWidget());
		
		gdk_window_set_cursor (m_pQuiverWindow->window, NULL);
		//m_Viewer.ShowBorder();
	}
	
	GtkAction * action = GetAction(m_pUIManager,QUIVER_ACTION_FULLSCREEN);
	if (NULL != action)
	{

		g_signal_handlers_block_matched (action,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,this);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bFullscreen);
		g_signal_handlers_unblock_matched (action,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,this);

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

gboolean Quiver::EventButtonPress( GtkWidget *widget,GdkEventButton  *event,gpointer data )
{
	if (1 == event->button)
	{

	}
	else if (2 == event->button)
	{
		ActionFullScreen(NULL,NULL);
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
	}

	return FALSE;
	
}

gboolean Quiver::EventKeyPress( GtkWidget *widget, GdkEventKey *event, gpointer data )
{
	if ( !GTK_WIDGET_HAS_FOCUS(m_Viewer.GetWidget()) )
	{
		return FALSE;
	}
	/*
	if (GDK_q == event->keyval || GDK_Q == event->keyval)
	{
		ActionQuit(NULL,NULL);
	}*/
	/*
	else if (GDK_f == event->keyval || GDK_F == event->keyval)
	{
		ActionFullScreen(NULL,NULL);
	}*/
	/*
	else if (GDK_d == event->keyval || GDK_D == event->keyval || GDK_Delete == event->keyval )
	{
		ActionImageTrash(NULL,NULL);
	}
	*/
	/*
	else if (GDK_z == event->keyval || GDK_Z == event->keyval)
	{
		if (Viewer::ZOOM_NONE == m_Viewer.GetZoomType())
		{
			cout << "zoom fit" << endl;
			gtk_drag_source_set (m_Viewer.GetWidget(), (GdkModifierType)(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK),
			   quiver_drag_target_table, 3, (GdkDragAction)(GDK_ACTION_LINK));
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
				m_ImageLoader.ReloadImage(m_ImageList.GetCurrent());
			}
		}
	}

	else if (GDK_Left == event->keyval)
	{
		ActionImagePrevious(NULL,NULL);
	}
	else if (GDK_Right == event->keyval)
	{
		ActionImageNext(NULL,NULL);
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
	*/
	/*
	else if (GDK_s == event->keyval || GDK_S == event->keyval)
	{
		ActionSlideShow(NULL,NULL);
			
	}
	*/

	else 
	{
		printf("key pressed: 0x%04x\n",event->keyval);
	}
	
	return FALSE;
}

gboolean Quiver::EventKeyRelease( GtkWidget *widget, GdkEventKey *event, gpointer data )
{
	//cout << "got key release event" << endl;
	return FALSE;
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
Quiver::Quiver(list<string> &images) : m_BrowserEventHandler(new BrowserEventHandler(this))
{
	m_listImages = images;
	Init();
}

void Quiver::Init()
{
	m_pUIManager = NULL;

	m_iMergedViewerUI = 0;
	m_iMergedBrowserUI = 0;	

	QuiverStockIcons::Load();
	
	m_bTimeoutEventMotionNotifyRunning = false;
	m_bTimeoutEventMotionNotifyMouseMoved = false;
	
	m_bSlideshowRunning = false;
	m_iTimeoutSlideshowID = 0;

	//initialize

	m_Browser.AddEventHandler(m_BrowserEventHandler);

	/* Create the main window */
	m_pQuiverWindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	if (LoadSettings())
	{	
		//set the size and position of the window
		//gtk_widget_set_uposition(quiver_window,m_iAppX,m_iAppY);
		gtk_window_move(GTK_WINDOW(m_pQuiverWindow),m_iAppX,m_iAppY);
		gtk_window_set_default_size (GTK_WINDOW(m_pQuiverWindow),m_iAppWidth,m_iAppHeight);

	}

	GError *tmp_error;
	tmp_error = NULL;
	

	GdkPixdata pixdata;
	gdk_pixdata_deserialize (&pixdata,sizeof(quiver_icon),quiver_icon,&tmp_error);
	GdkPixbuf *pixbuf_icon = gdk_pixbuf_from_pixdata(&pixdata,FALSE,&tmp_error);
	
	gtk_window_set_default_icon     (pixbuf_icon);
	
	g_object_unref(pixbuf_icon);

	/* Set up GUI elements */

	m_pUIManager = gtk_ui_manager_new();
	gtk_ui_manager_set_add_tearoffs (m_pUIManager,TRUE);

	gtk_ui_manager_add_ui_from_string(m_pUIManager,
			quiver_ui_main,
			strlen(quiver_ui_main),
			&tmp_error);

	guint n_entries = G_N_ELEMENTS (action_entries);

	
	GtkActionGroup* actions = gtk_action_group_new ("Actions");
	
	gtk_action_group_add_actions(actions, action_entries, n_entries, this);
                                 
	gtk_action_group_add_toggle_actions(actions,
										action_entries_toggle, 
										G_N_ELEMENTS (action_entries_toggle),
										this);
	/* FIXME: i think there's a bug with the user_data when passed in using
	 * g_signal_connect_data in the following function.
	gtk_action_group_add_radio_actions(actions,
										action_entries_radio, 
										G_N_ELEMENTS (action_entries_radio),
										1,
										NULL,
										NULL);										
	*/
	gtk_ui_manager_insert_action_group (m_pUIManager,actions,0);
                                             
	g_signal_connect (m_pUIManager, "connect_proxy",
		G_CALLBACK (signal_connect_proxy), this);
	g_signal_connect (m_pUIManager, "disconnect_proxy",
		G_CALLBACK (signal_disconnect_proxy), this);
                                             
	gtk_window_add_accel_group (GTK_WINDOW(m_pQuiverWindow),
								gtk_ui_manager_get_accel_group(m_pUIManager));
	

	m_Browser.SetUIManager(m_pUIManager);
	m_Viewer.SetUIManager(m_pUIManager);
	//GTK_WIDGET_UNSET_FLAGS(toolbar,GTK_CAN_FOCUS);

 	//gtk_widget_add_events (m_pQuiverWindow, GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);
	
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "window_state_event",
    			G_CALLBACK (Quiver::event_window_state), this);
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "key_press_event",
    			G_CALLBACK (Quiver::event_key_press), this);
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "key_release_event",
    			G_CALLBACK (Quiver::event_key_release), this);
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "delete_event",
    			G_CALLBACK (Quiver::event_delete), this);
    	/*
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "scroll_event",
    			G_CALLBACK (Quiver::event_scroll), this);
    	*/
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


	/* the layout for the gui is as follows:
	 * 
	 * gtkwinodw
	 * -> vbox
	 *   -> menubar
	 *   -> toolbar
	 *   -> hpaned (main gui area)
	 *     -> hbox
	 *       -> browser  ( browser xor viewer visible at any given time)
	 *       -> viewer 
	 *     -> notebook  for image properties (file,exif,db)
	 *   -> status bar
	 */
	GtkWidget* statusbar;
	GtkWidget* vbox;
	GtkWidget* hpaned_main_area;
	GtkWidget* hbox_browser_viewer_container;
	
	vbox = gtk_vbox_new(FALSE,0);
	hpaned_main_area = gtk_hpaned_new();
	
	hbox_browser_viewer_container = gtk_hbox_new(FALSE,0);
	m_pNBProperties = gtk_notebook_new();
	
	gtk_widget_set_no_show_all(m_pNBProperties,TRUE);
	
	//FIXME: temp notebook stuff
	gtk_notebook_append_page(GTK_NOTEBOOK(m_pNBProperties),gtk_drawing_area_new(),gtk_label_new("File"));
	//gtk_notebook_append_page(GTK_NOTEBOOK(m_pNBProperties),gtk_drawing_area_new(),gtk_label_new("Exif"));
	gtk_notebook_append_page(GTK_NOTEBOOK(m_pNBProperties),m_ExifView.GetWidget(),gtk_label_new("Exif"));
	gtk_notebook_append_page(GTK_NOTEBOOK(m_pNBProperties),gtk_drawing_area_new(),gtk_label_new("IPTC"));
	gtk_notebook_append_page(GTK_NOTEBOOK(m_pNBProperties),gtk_drawing_area_new(),gtk_label_new("Database"));
	gtk_notebook_popup_enable(GTK_NOTEBOOK(m_pNBProperties));
	gtk_notebook_set_scrollable (GTK_NOTEBOOK(m_pNBProperties),TRUE);
	
	statusbar =  m_Statusbar.GetWidget();
	m_pMenubar = gtk_ui_manager_get_widget (m_pUIManager,"/ui/MenubarMain");
	m_pToolbar = gtk_ui_manager_get_widget (m_pUIManager,"/ui/ToolbarMain");

	// pack the browser and viewer area
	gtk_box_pack_start (GTK_BOX (hbox_browser_viewer_container), m_Browser.GetWidget(), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox_browser_viewer_container), m_Viewer.GetWidget(), TRUE, TRUE, 0);

	if (1 < m_listImages.size())
	{
		// must do the following to hide the widget on initial
		// display of app
		ShowBrowser();
	}
	else
	{

		ShowViewer();
	}

	// pack the hpaned (main gui area)
	gtk_paned_pack1(GTK_PANED(hpaned_main_area),hbox_browser_viewer_container,TRUE,TRUE);
	gtk_paned_pack2(GTK_PANED(hpaned_main_area),m_pNBProperties,FALSE,FALSE);

	// pack the main gui ara with the rest of the gui compoents
	//gtk_container_add (GTK_CONTAINER (vbox),menubar);
	gtk_box_pack_start (GTK_BOX (vbox), m_pMenubar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), m_pToolbar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hpaned_main_area, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox),statusbar , FALSE, FALSE, 0);

	// add the gui elements to the main window
	gtk_container_add (GTK_CONTAINER (m_pQuiverWindow),vbox);


	// FIXME: viewer should focus when it is displayed
	//	gtk_widget_grab_focus (m_Viewer.GetWidget());




	/* Show the application window */
	
	gtk_container_set_border_width (GTK_CONTAINER(m_pQuiverWindow),0);
	
	/*
	 * FIXME
	gtk_drag_dest_set(m_pQuiverWindow,GTK_DEST_DEFAULT_ALL,
					quiver_drag_target_table, 3, (GdkDragAction)(GDK_ACTION_COPY|GDK_ACTION_MOVE));
	*/
/*
	g_signal_connect (G_OBJECT (m_pQuiverWindow), "drag_data_received",
				GTK_SIGNAL_FUNC (Quiver::signal_drag_data_received), this);


	gtk_drag_source_set (m_Viewer.GetWidget(), (GdkModifierType)(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK),
			   quiver_drag_target_table, 3, (GdkDragAction)(
	//GDK_ACTION_COPY | GDK_ACTION_MOVE | 	GDK_ACTION_LINK));
*/

	/*
	gtk_drag_source_set_icon (m_Viewer, 
				gtk_widget_get_colormap (window),
				drag_icon, drag_mask);
	*/		
/*
	gtk_signal_connect (GTK_OBJECT (m_Viewer.GetWidget()), "drag_data_get",
		      GTK_SIGNAL_FUNC (Quiver::signal_drag_data_get), this);
	gtk_signal_connect (GTK_OBJECT (m_Viewer.GetWidget()), "drag_data_delete",
		      GTK_SIGNAL_FUNC (Quiver::signal_drag_data_delete), this);

	gtk_signal_connect (GTK_OBJECT (m_Viewer.GetWidget()), "drag_begin",
	      GTK_SIGNAL_FUNC (Quiver::signal_drag_begin), this);
	
	gtk_signal_connect (GTK_OBJECT (m_Viewer.GetWidget()), "drag_end",
	      GTK_SIGNAL_FUNC (Quiver::signal_drag_end), this);		  
*/
/*	
	g_signal_connect (G_OBJECT (m_pQuiverWindow), "drag_drop",
				G_CALLBACK (signal_drag_drop), this);
			

	g_signal_connect (G_OBJECT (m_pQuiverWindow), "drag_motion",
				G_CALLBACK (signal_drag_motion), this);
*/
					
	g_idle_add(idle_quiver_init,this);
	
}

/**
 * quiver main application loop
 */
int Quiver::Show(QuiverAppMode mode )
{
	m_QuiverAppMode = mode;
	
	if (QUIVER_APP_MODE_SCREENSAVER != m_QuiverAppMode)
	{
		gtk_widget_show_all (m_pQuiverWindow);
	}
	gtk_main ();
	
	/* The user lost interest */
	return 0;
	
}
Quiver::~Quiver()
{
	//destructor
}

bool Quiver::LoadSettings()
{
	
	string gtk_rc = getenv("HOME") + string("/.quiver/gtkrc");
	gtk_rc_parse (gtk_rc.c_str());
	
		
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
	m_ImageList.SetImageList(&files);
}

int main (int argc, char **argv)
{
	
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	Quiver::QuiverAppMode mode = Quiver::QUIVER_APP_MODE_NORMAL;

  /* init threads */
	g_thread_init (NULL);
	gdk_threads_init ();
	gdk_threads_enter ();


	/* Initialize the widget set */
	gtk_init (&argc, &argv);
	
	list<string> files;
	for (int i =1;i<argc;i++)
	{	
		string s = argv[i];
		if ("-root" == s)
		{
			mode = Quiver::QUIVER_APP_MODE_SCREENSAVER;
			// no display
		}
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
	int rval = quiver.Show(mode);	
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
			gdk_threads_enter();
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
			
			gdk_threads_leave();
		}
	}

	return retval;
}

gboolean Quiver::idle_quiver_init (gpointer data)
{
	return ((Quiver*)data)->IdleQuiverInit(data);
}
gboolean Quiver::IdleQuiverInit(gpointer data)
{
	// put process intenstive startup code in here 
	// (loading image list, setting first image)

	printf("initialized\n");

	SetImageList(m_listImages);
	m_Browser.SetImageList(m_ImageList);
	m_Viewer.SetImageList(m_ImageList);

	if (0 == m_iMergedBrowserUI)
	{
		ShowViewer();
	}
	else
	{
		ShowBrowser();
	}

	return FALSE; // return false so it is never called again
}


gboolean Quiver::timeout_advance_slideshow (gpointer data)
{
	return ((Quiver*)data)->TimeoutAdvanceSlideshow(data);
}

gboolean Quiver::TimeoutAdvanceSlideshow(gpointer data)
{
	printf("timeout reached\n");

	// reset timout id
	m_iTimeoutSlideshowID = 0;
	// advance slideshow

	//SlideshowAddTimeout();
	
	gdk_threads_enter();
//	ActionImageNext(NULL,NULL);
	gdk_threads_leave();

	if (!m_ImageList.HasNext())
	{
		gdk_threads_enter();
		SlideshowStop();
		gdk_threads_leave();
	}

	return FALSE;
}

void Quiver::SlideshowStart()
{
	m_bSlideshowRunning = true;
	SlideshowAddTimeout();
}

void Quiver::SlideshowAddTimeout()
{
	if (SlideshowRunning())
	{
		if (!m_iTimeoutSlideshowID)
		{
			m_iTimeoutSlideshowID = g_timeout_add(2000,timeout_advance_slideshow,this);
		}
	}
}
void Quiver::SlideshowStop()
{
	if (0 != m_iTimeoutSlideshowID)
	{
		g_source_remove (m_iTimeoutSlideshowID);
	}
	// reset timout id
	m_iTimeoutSlideshowID = 0;
	m_bSlideshowRunning = false;
	// re-enable the slideshow button if it was disabled

	GtkAction * action = GetAction(m_pUIManager,QUIVER_ACTION_SLIDESHOW);
	if (NULL != action)
	{
		g_signal_handlers_block_matched (action,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,this);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),FALSE);
		g_signal_handlers_unblock_matched (action,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,this);
	}
}

bool Quiver::SlideshowRunning()
{
	return m_bSlideshowRunning;
}

void Quiver::SignalClosed(GdkPixbufLoader *loader)
{
	/*FIXME m_Statusbar.SetZoomPercent((int)m_Viewer.GetZoomLevel());*/
	SlideshowAddTimeout();
}
void Quiver::SetPixbuf(GdkPixbuf*pixbuf)
{
	/*FIXME
	m_Statusbar.SetZoomPercent((int)m_Viewer.GetZoomLevel());
	*/
	SlideshowAddTimeout();
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



//==============================================================================
//== gtk_ui_manager signals ====================================================
//==============================================================================

	// gtk_ui_manager signals
void Quiver::signal_connect_proxy (GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data)
{
	return ((Quiver*)data)->SignalConnectProxy(manager,action,proxy,data);
}
void Quiver::signal_disconnect_proxy (GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data)
{
	return ((Quiver*)data)->SignalDisconnectProxy(manager,action,proxy,data);
}

void Quiver::signal_item_select (GtkItem *proxy,gpointer data)
{
	return ((Quiver*)data)->SignalItemSelect(proxy,data);
}
void Quiver::signal_item_deselect (GtkItem *proxy,gpointer data)
{
	return ((Quiver*)data)->SignalItemDeselect(proxy,data);
}


void Quiver::SignalConnectProxy(GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data)
{
	if (GTK_IS_ITEM(proxy))
	{
		g_signal_connect (proxy, "select",G_CALLBACK (signal_item_select), data);
		g_signal_connect (proxy, "deselect",G_CALLBACK (signal_item_deselect), data);
	}
}
void Quiver::SignalDisconnectProxy(GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data)
{
	/*
	g_signal_handlers_disconnect_by_func (proxy, G_CALLBACK (signal_item_select), data);
	g_signal_handlers_disconnect_by_func (proxy, G_CALLBACK (signal_item_deselect), data);
	*/
}

void Quiver::SignalItemSelect (GtkItem *proxy,gpointer data)
{
	GtkAction *action;
	char      *message;
	
	action = (GtkAction*)g_object_get_data (G_OBJECT (proxy),  "gtk-action");
	g_return_if_fail (action != NULL);
	
	g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
	if (message)
	{
		m_Statusbar.PushText(message);
		g_free (message);
	}
	
}
void Quiver::SignalItemDeselect (GtkItem *proxy,gpointer data)
{
	GtkAction *action;
	char      *message;
	
	action = (GtkAction*)g_object_get_data (G_OBJECT (proxy),  "gtk-action");
	g_return_if_fail (action != NULL);
	
	g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
	if (message)
	{
		m_Statusbar.PopText();
		g_free (message);
	}
}

//==============================================================================
//== action c callbacks ========================================================
//==============================================================================

void Quiver::action_file_save(GtkAction *action,gpointer data)
{
	return ((Quiver*)data)->ActionFileSave(action,data);
}


void Quiver::action_quit(GtkAction *action,gpointer data)
{
	return ((Quiver*)data)->ActionQuit(action,data);
}	

void Quiver::action_full_screen(GtkAction *action,gpointer data)
{
	return ((Quiver*)data)->ActionFullScreen(action,data);
}

void Quiver::action_slide_show(GtkAction *action,gpointer data)
{
	return ((Quiver*)data)->ActionSlideShow(action,data);
}

void Quiver::action_image_trash(GtkAction *action,gpointer data)
{
	return ((Quiver*)data)->ActionImageTrash(action,data);
}

void Quiver::action_about(GtkAction *action,gpointer data)
{
	return ((Quiver*)data)->ActionAbout(action,data);
}

void Quiver::action_ui_mode_change(GtkAction *action,gpointer data)
{
	return ((Quiver*)data)->ActionUIModeChange(action,data);
}



//==============================================================================
//== action c++ callbacks ======================================================
//==============================================================================
typedef enum {
	JCOPYOPT_NONE,		/* copy no optional markers */
	JCOPYOPT_COMMENTS,	/* copy only comment (COM) markers */
	JCOPYOPT_ALL		/* copy all optional markers */
} JCOPY_OPTION;

/* Copy markers saved in the given source object to the destination object.
 * This should be called just after jpeg_start_compress() or
 * jpeg_write_coefficients().
 * Note that those routines will have written the SOI, and also the
 * JFIF APP0 or Adobe APP14 markers if selected.
 */

GLOBAL(void)
jcopy_markers_execute (j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
		       JCOPY_OPTION option)
{
  jpeg_saved_marker_ptr marker;

  /* In the current implementation, we don't actually need to examine the
   * option flag here; we just copy everything that got saved.
   * But to avoid confusion, we do not output JFIF and Adobe APP14 markers
   * if the encoder library already wrote one.
   */
  for (marker = srcinfo->marker_list; marker != NULL; marker = marker->next) {
    if (dstinfo->write_JFIF_header &&
	marker->marker == JPEG_APP0 &&
	marker->data_length >= 5 &&
	GETJOCTET(marker->data[0]) == 0x4A &&
	GETJOCTET(marker->data[1]) == 0x46 &&
	GETJOCTET(marker->data[2]) == 0x49 &&
	GETJOCTET(marker->data[3]) == 0x46 &&
	GETJOCTET(marker->data[4]) == 0)
      continue;			/* reject duplicate JFIF */
    if (dstinfo->write_Adobe_marker &&
	marker->marker == JPEG_APP0+14 &&
	marker->data_length >= 5 &&
	GETJOCTET(marker->data[0]) == 0x41 &&
	GETJOCTET(marker->data[1]) == 0x64 &&
	GETJOCTET(marker->data[2]) == 0x6F &&
	GETJOCTET(marker->data[3]) == 0x62 &&
	GETJOCTET(marker->data[4]) == 0x65)
      continue;			/* reject duplicate Adobe */
#ifdef NEED_FAR_POINTERS
    /* We could use jpeg_write_marker if the data weren't FAR... */
    {
      unsigned int i;
      jpeg_write_m_header(dstinfo, marker->marker, marker->data_length);
      for (i = 0; i < marker->data_length; i++)
	jpeg_write_m_byte(dstinfo, marker->data[i]);
    }
#else
    jpeg_write_marker(dstinfo, marker->marker,
		      marker->data, marker->data_length);
#endif
  }
}

#ifndef SAVE_MARKERS_SUPPORTED
#error please add a #define JPEG_INTERNALS above jpeglib.h include
#endif
GLOBAL(void)
jcopy_markers_setup (j_decompress_ptr srcinfo, JCOPY_OPTION option)
{
#ifdef SAVE_MARKERS_SUPPORTED
  int m;

  /* Save comments except under NONE option */
  if (option != JCOPYOPT_NONE) {
    jpeg_save_markers(srcinfo, JPEG_COM, 0xFFFF);
  }
  /* Save all types of APPn markers iff ALL option */
  if (option == JCOPYOPT_ALL) {
    for (m = 0; m < 16; m++)
      jpeg_save_markers(srcinfo, JPEG_APP0 + m, 0xFFFF);
  }
#endif /* SAVE_MARKERS_SUPPORTED */
}


static int save_jpeg_file(string filename,ExifData *exifData)
{
/*
int jpeg_transform_files(char *infile, char *outfile,
			 JXFORM_CODE transform,
			 unsigned char *comment,
			 char *thumbnail, int tsize,
			 unsigned int flags)
{
*/
	int rc;
	FILE *in;
	FILE *out;

	string strOriginalFile = filename + "_original.jpg";
	const char *infile  = strOriginalFile.c_str();
	const char *outfile = filename.c_str();
	rename(filename.c_str(),strOriginalFile.c_str());

	/* open infile */
	in = fopen(infile,"r");
	if (NULL == in) {
	//fprintf(stderr,"open %s: %s\n",infile,strerror(errno));
	return -1;
	}

	/* open outfile */
	out = fopen(outfile,"w");
	if (NULL == out) {
	//fprintf(stderr,"open %s: %s\n",outfile,strerror(errno));
	fclose(in);
	return -1;
	}

	/* go! */

	//rc = jpeg_transform_fp(in,out,transform,comment,thumbnail,tsize,flags);
	//

	struct jpeg_decompress_struct src;
	struct jpeg_compress_struct   dst;

	struct jpeg_error_mgr jdsterr;
	//struct longjmp_error_mgr jsrcerr;

	src.err = jpeg_std_error(&jdsterr);
	/* setup src */
	/*
	jsrcerr.jpeg.error_exit = longjmp_error_exit;
	if (setjmp(jsrcerr.setjmp_buffer))
		printf("ouch\n");
	*/


	jpeg_create_decompress(&src);
	jpeg_stdio_src(&src, in);



	/* setup dst */
	dst.err = jpeg_std_error(&jdsterr);
	jpeg_create_compress(&dst);
	jpeg_stdio_dest(&dst, out);

	/* transform image */

	jvirt_barray_ptr * src_coef_arrays;
	jvirt_barray_ptr * dst_coef_arrays;
	//jpeg_transform_info transformoption;


	jcopy_markers_setup(&src, JCOPYOPT_ALL);
	if (JPEG_HEADER_OK != jpeg_read_header(&src, TRUE))
	return -1;

	/* do exif updating */
	jpeg_saved_marker_ptr mark;
	//ExifData *ed = NULL;
	unsigned char *data;
	unsigned int  size;

	for (mark = src.marker_list; NULL != mark; mark = mark->next) {
		printf("searching...\n");
		if (mark->marker == JPEG_APP0 +1)
		{
			printf("found exif marker!\n");
			break;
		}
		continue;
	}


	if (NULL == mark) {
		mark = (jpeg_marker_struct*)src.mem->alloc_large((j_common_ptr)&src,JPOOL_IMAGE,sizeof(*mark));
		memset(mark,0,sizeof(*mark));
		mark->marker = JPEG_APP0 +1;
		mark->next   = src.marker_list->next;
		src.marker_list->next = mark;
	}

	/* build new exif data block */
	exif_data_save_data(exifData,&data,&size);
	//exif_data_unref(ed);

	/* update jpeg APP1 (EXIF) marker */
	mark->data = (JOCTET*)src.mem->alloc_large((j_common_ptr)&src,JPOOL_IMAGE,size);
	mark->original_length = size;
	mark->data_length = size;
	memcpy(mark->data,data,size);
	free(data);

	/* Any space needed by a transform option must be requested before
	 * jpeg_read_coefficients so that memory allocation will be done right.
	 */
	//jtransform_request_workspace(&src, &transformoption);
	src_coef_arrays = jpeg_read_coefficients(&src);
	jpeg_copy_critical_parameters(&src, &dst);

	/*dst_coef_arrays = jtransform_adjust_parameters
	(&src, &dst, src_coef_arrays, &transformoption);
	*/
	/* Start compressor (note no image data is actually written here) */
	jpeg_write_coefficients(&dst, src_coef_arrays);

	/* Copy to the output file any extra markers that we want to preserve */
	jcopy_markers_execute(&src, &dst, JCOPYOPT_ALL);

	/* Execute image transformation, if any */
	/*
	jtransform_execute_transformation(src, dst,
					  src_coef_arrays,
					  &transformoption);
   */
	/* Finish compression and release memory */
	jpeg_finish_compress(&dst);
	jpeg_finish_decompress(&src);


	/* cleanup */
	jpeg_destroy_decompress(&src);
	jpeg_destroy_compress(&dst);

	fclose(in);
	fclose(out);

	return rc;


}

static void update_exif_orientation(ExifData *exifData,int value)
{
	ExifEntry *e;

	/* If the entry doesn't exist, create it. */
	e = exif_content_get_entry (exifData->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
	if (!e) 
	{
		e = exif_entry_new ();
		exif_content_add_entry (exifData->ifd[EXIF_IFD_0], e);
		exif_entry_initialize (e, EXIF_TAG_ORIENTATION);
	}

	/* Now set the value and save the data. */
	exif_set_short (e->data , exif_data_get_byte_order (exifData), value);
}


void Quiver::ActionFileSave(GtkAction *action,gpointer data)
{
	/*
	 * FIXME
	printf("save the file! aww yeah baby\n");
	QuiverFile quiverFile;
	quiverFile = m_ImageList.GetCurrent();
	int old_orientation, new_orientation;

	old_orientation = quiverFile.GetOrientation();
	new_orientation = m_Viewer.GetCurrentOrientation();
	
	ExifData *exif_data = quiverFile.GetExifData();
	if (exif_data && old_orientation != new_orientation)
	{
		// hmm...must have a suitable file
		printf("we certainly should save the file\n");
		update_exif_orientation(exif_data,new_orientation);
		save_jpeg_file(quiverFile.GetFilePath(),exif_data);
	}
	*/
}
void Quiver::ShowViewer()
{
	GError *tmp_error;
	tmp_error = NULL;
	gtk_ui_manager_remove_ui(m_pUIManager,m_iMergedBrowserUI);
	m_iMergedBrowserUI = 0;
	if (0 == m_iMergedViewerUI)
		m_iMergedViewerUI = gtk_ui_manager_add_ui_from_string(m_pUIManager,
			quiver_ui_viewer,
			strlen(quiver_ui_viewer),
			&tmp_error);
	m_Browser.Hide();
	m_Viewer.Show();
	// clear the screen of any old image
	//FIXME 
	//m_Viewer.SetPixbuf(NULL);

	printf("%d is the merge id\n",m_iMergedViewerUI);
}
void Quiver::ShowBrowser()
{
	GError *tmp_error;
	tmp_error = NULL;

	gtk_ui_manager_remove_ui(m_pUIManager,m_iMergedViewerUI);
	m_iMergedViewerUI = 0;
	if (0 == m_iMergedBrowserUI)
		m_iMergedBrowserUI = gtk_ui_manager_add_ui_from_string(m_pUIManager,
			quiver_ui_browser,
			strlen(quiver_ui_browser),
			&tmp_error);
	printf("%d is the merge id\n",m_iMergedBrowserUI);
	m_Viewer.Hide();
	m_Browser.Show();
}
void Quiver::ActionUIModeChange(GtkAction *action,gpointer data)
{
	printf("%s\n",gtk_action_get_name(action));

	if (0 == strcmp( gtk_action_get_name(action),QUIVER_ACTION_UI_MODE_BROWSER) )
	{
		ShowBrowser();
	}
	else
	{
		ShowViewer();
	}						
}

void Quiver::ActionAbout(GtkAction *action,gpointer data)
{
	char * authors[] = {"mike morrison <mike_morrison@alumni.uvic.ca>",NULL};
	char * artists[] = {"mike morrison <mike_morrison@alumni.uvic.ca>",NULL};
	char * documenters[] = {"mike morrison <mike_morrison@alumni.uvic.ca>",NULL};
	gtk_show_about_dialog(GTK_WINDOW(m_pQuiverWindow),
		"name",GETTEXT_PACKAGE,
		"version",PACKAGE_VERSION,
		"copyright","copyright (c) 2005\nmike morrison",
		"comments","a gtk image viewer",
		"authors",authors,
		"artists",artists,
		"documenters",documenters,
		"website",PACKAGE_BUGREPORT,
		"website-label","quiver website",
		NULL);
}



void Quiver::ActionQuit(GtkAction *action,gpointer data)
{
	GdkEvent * e = gdk_event_new(GDK_DELETE);

	gboolean rval = FALSE;
	g_signal_emit_by_name(m_pQuiverWindow,"delete_event",e,&rval);
	gdk_event_free(e);
	
	if (!rval)//if (G_VALUE_HOLDS_BOOLEAN(return_value))
	{
		g_signal_emit_by_name(m_pQuiverWindow, "destroy");
	}
}

void Quiver::ActionFullScreen(GtkAction *action,gpointer data)
{
	printf("action fullscreen!\n");
	if (GDK_WINDOW_STATE_FULLSCREEN & m_WindowState)
	{
		gtk_window_unfullscreen(GTK_WINDOW(m_pQuiverWindow));
		// FIXME: create a ShowCursor method
		// and a HideCursor method
	}
	else
	{
		// timeout to hide mouse cursor
		gtk_window_fullscreen(GTK_WINDOW(m_pQuiverWindow));
	}
}

void Quiver::ActionSlideShow(GtkAction *action,gpointer data)
{
	if (SlideshowRunning())
	{
		SlideshowStop();
	}
	else
	{
		SlideshowStart();
	}
}

void Quiver::ActionImageTrash(GtkAction *action,gpointer data)
{
	// FIXME: create a ShowCursor method
	// and a HideCursor method
	gdk_window_set_cursor (m_pQuiverWindow->window, NULL);
	
	//delete the current images
	if (m_ImageList.GetSize())
	{
		char * for_display = gnome_vfs_format_uri_for_display(m_ImageList.GetCurrent().GetURI());
		GtkWidget* dialog = gtk_message_dialog_new (GTK_WINDOW(m_pQuiverWindow),GTK_DIALOG_MODAL,
								GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO,_("Are you sure you want to move the following image to the trash?"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog), for_display);
		g_free(for_display);
		gint rval = gtk_dialog_run(GTK_DIALOG(dialog));
	
		switch (rval)
		{
			case GTK_RESPONSE_YES:
			{
				//trash the file
				cout << "trashing file : " << m_ImageList.GetCurrent().GetURI() << endl;
				//locate trash folder
				GnomeVFSURI * trash_vfs_uri = NULL;

				cout << "is this one val? "<< endl;
				GnomeVFSURI * near_vfs_uri = gnome_vfs_uri_new ( m_ImageList.GetCurrent().GetURI() );
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
						m_ImageList.Remove(m_ImageList.GetCurrentIndex());

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
				cout << "not trashing file : " << m_ImageList.GetCurrent().GetURI() << endl;
				break;
		}
	
		gtk_widget_destroy(dialog);
	}
}

void Quiver::BrowserEventHandler::HandleSelectionChanged(BrowserEventPtr event_ptr)
{
	list<int> selection = parent->m_Browser.GetSelection();
	list<int>::iterator itr;
	
	unsigned long long total_size = 0;
	
	// n items selected (xx kb)
	for (itr = selection.begin(); selection.end() != itr; ++itr)
	{
		if (*itr < parent->m_ImageList.GetSize())
		{
			QuiverFile f = parent->m_ImageList[*itr];
			total_size += f.GetFileSize();
		}
	}
	
	parent->m_Statusbar.SetZoomPercent(-1);
	parent->m_Statusbar.SetLoadTime(-1);
	
	char status_text[256];
	sprintf(status_text,"%d items selected (%d bytes)",selection.size(),total_size);

	parent->m_Statusbar.SetText(status_text);
}

void Quiver::BrowserEventHandler::HandleItemActivated(BrowserEventPtr event_ptr)
{
	parent->ShowViewer();
}

void Quiver::BrowserEventHandler::HandleCursorChanged(BrowserEventPtr event_ptr)
{
	parent->ImageChanged();
}









