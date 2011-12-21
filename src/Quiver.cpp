#include <config.h>

#include "quiver-i18n.h"

#include <gst/gst.h>

#include "Quiver.h"

#ifdef QUIVER_MAEMO
#ifdef HAVE_HILDON_1
#include <hildon/hildon-program.h>
#else
#include <hildon-widgets/hildon-program.h>
#endif
#include <libosso.h>
#endif

#include <gdk-pixbuf/gdk-pixbuf-animation.h>
//#include "QuiverUI.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <boost/algorithm/string.hpp>

#include <errno.h>

#include "QuiverStockIcons.h"

#include "IBrowserEventHandler.h"
#include "IViewerEventHandler.h"
#include "IPreferencesEventHandler.h"
#include "IImageListEventHandler.h"
#include "IBookmarksEventHandler.h"


#include "QuiverUtils.h"

#include "QuiverPrefs.h"
#include "PreferencesDlg.h"

#include "SaveImageTask.h"
#include "AdjustDateDlg.h"
#include "AdjustDateTask.h"

#include "OrganizeDlg.h"
#include "OrganizeTask.h"

#include "RenameDlg.h"
#include "RenameTask.h"

#include "Bookmarks.h"
#include "BookmarksDlg.h"
#include "BookmarkAddEditDlg.h"

#include "DonateDlg.h"

#include "TaskManager.h"
#include "TaskManagerDlg.h"

#include "ThreadUtil.h"

#include "ExternalTools.h"
#include "ExternalToolsDlg.h"
#include "IExternalToolsEventHandler.h"

#include "ImageSaveManager.h"

// globals needed for preferences

gchar g_szConfigDir[256]      = "";
gchar g_szConfigFilePath[256] = "";

#ifdef QUIVER_MAEMO
osso_context_t* osso_context  = NULL;
#endif

using namespace std;

// helper functions


static void quiver_action_handler_cb(GtkAction *action, gpointer data);
static void quiver_radio_action_handler_cb(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static gboolean quiver_window_button_press ( GtkWidget *widget, GdkEventButton *event, gpointer data );

// gtk_ui_manager signals
static void signal_connect_proxy (GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data);
static void signal_disconnect_proxy (GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data);
static void signal_item_select (GtkItem *proxy,gpointer data);
static void signal_item_deselect (GtkItem *proxy,gpointer data);

#ifdef QUIVER_MAEMO
static void notify_gtk_enable_accels_changed (GObject *gobject, GParamSpec *arg1, gpointer user_data);
#endif
static gboolean event_window_state( GtkWidget *widget, GdkEventWindowState *event, gpointer data );
static gboolean timeout_event_motion_notify (gpointer data);
static gboolean event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data );

static gboolean timeout_keep_screen_on (gpointer data);

#ifdef QUIVER_MAEMO
static void mime_open_handler (gpointer raw_data, int argc, char **argv);
#endif

class QuiverImpl
{
public:
// methods
	QuiverImpl(Quiver *parent);
	~QuiverImpl();

	void LoadBookmarks();
	void LoadExternalTools();
	
	void Save();
	void SaveAs();
	
	bool CanClose();

	void UpdateUI();
	
// member variables
	Quiver *m_pQuiver;

	BrowserPtr m_BrowserPtr;
	ViewerPtr m_ViewerPtr;
	ExifView m_ExifView;
	
	StatusbarPtr m_StatusbarPtr;

	BookmarksPtr m_BookmarksPtr;
	ExternalToolsPtr m_ExternalToolsPtr;

	GtkWidget *m_pQuiverWindow;

	GtkWidget *m_pMenubar;
	GtkWidget *m_pToolbar;
	GtkWidget *m_pNBProperties;
	GtkWidget* m_pHPanedMainArea;
	
	guint m_iMergedViewerUI;
	guint m_iMergedBrowserUI;
	
	guint m_iMergedExternalToolsUI;
	guint m_iMergedBookmarksUI;
	
	bool m_bSlideShowRestoreFromFS;
			
	ImageListPtr m_ImageListPtr;
	
	int m_iAppX;
	int m_iAppY;
	int m_iAppWidth;
	int m_iAppHeight;
	
	bool m_bInitialized;
	
	bool m_bTimeoutEventMotionNotifyRunning;
	bool m_bTimeoutEventMotionNotifyMouseMoved;
	
	guint m_iTimeoutMouseMotionNotify;

	guint m_iTimeoutKeepScreenOn;
	
	QuiverFile m_CurrentQuiverFile;

	GdkWindowState m_WindowState;

	static GtkTargetEntry quiver_drag_target_table[];
	// drag/drop targets
	enum {
    	QUIVER_TARGET_STRING,
		QUIVER_TARGET_URI
  	};

	//gui actions
	static GtkActionEntry action_entries[];
	static GtkToggleActionEntry action_entries_toggle[];
	static GtkRadioActionEntry sort_radio_action_entries[];
	
	GtkUIManager* m_pUIManager;
	
	bool m_bListImagesRecursive;
	std::list<std::string> m_listImages;

// nested classes

	//class BrowserEventHandler;
	class BrowserEventHandler : public IBrowserEventHandler
	{
	public:
		BrowserEventHandler(QuiverImpl *parent){this->parent = parent;};
		virtual void HandleSelectionChanged(BrowserEventPtr event_ptr);
		virtual void HandleItemActivated(BrowserEventPtr event_ptr);
		virtual void HandleCursorChanged(BrowserEventPtr event_ptr);
	private:
		QuiverImpl *parent;
	};
	
	//class ViewerEventHandler;
	class ViewerEventHandler : public IViewerEventHandler
	{
	public:
		ViewerEventHandler(QuiverImpl *parent){this->parent = parent;};
		virtual void HandleItemClicked(ViewerEventPtr event_ptr);
		virtual void HandleItemActivated(ViewerEventPtr event_ptr);
		virtual void HandleCursorChanged(ViewerEventPtr event_ptr);
		virtual void HandleSlideShowStarted(ViewerEventPtr event_ptr);
		virtual void HandleSlideShowStopped(ViewerEventPtr event_ptr);
	private:
		QuiverImpl *parent;
	};
	
	class ImageListEventHandler : public IImageListEventHandler
	{
	public:
		ImageListEventHandler(QuiverImpl *parent){this->parent = parent;};
		virtual void HandleContentsChanged(ImageListEventPtr event);
		virtual void HandleCurrentIndexChanged(ImageListEventPtr event) ;
		virtual void HandleItemAdded(ImageListEventPtr event);
		virtual void HandleItemRemoved(ImageListEventPtr event);
		virtual void HandleItemChanged(ImageListEventPtr event);
	private:
		QuiverImpl *parent;
	};

	class PreferencesEventHandler : public IPreferencesEventHandler
	{
	public:
		PreferencesEventHandler(QuiverImpl* parent) {this->parent = parent;};
		virtual void HandlePreferenceChanged(PreferencesEventPtr event);
	private:
		QuiverImpl* parent;
	};

	class BookmarksEventHandler : public IBookmarksEventHandler
	{
	public:
		BookmarksEventHandler(QuiverImpl* parent) {this->parent = parent;};
		virtual void HandleBookmarkChanged(BookmarksEventPtr event);
	private:
		QuiverImpl* parent;
	};
	
	class ExternalToolsEventHandler : public IExternalToolsEventHandler
	{
	public:
		ExternalToolsEventHandler(QuiverImpl* parent) {this->parent = parent;};
		virtual void HandleExternalToolChanged(ExternalToolsEventPtr event);
	private:
		QuiverImpl* parent;
	};

	IBrowserEventHandlerPtr m_BrowserEventHandler;
	IViewerEventHandlerPtr m_ViewerEventHandler;
	IPreferencesEventHandlerPtr m_PreferencesEventHandler;
	IImageListEventHandlerPtr m_ImageListEventHandler;
	IBookmarksEventHandlerPtr m_BookmarksEventHandler;
	IExternalToolsEventHandlerPtr m_ExternalToolsEventHandler;
	
	
};


QuiverImpl::QuiverImpl (Quiver *parent) :
          m_BrowserPtr(new Browser()),
          m_ViewerPtr(new Viewer()),
          m_StatusbarPtr(new Statusbar()),
          m_ImageListPtr(new ImageList(true)),
		  m_BrowserEventHandler(new BrowserEventHandler(this)),
		  m_ViewerEventHandler(new ViewerEventHandler(this)),
		  m_PreferencesEventHandler( new PreferencesEventHandler(this) ),
		  m_ImageListEventHandler ( new ImageListEventHandler(this) ),
		  m_BookmarksEventHandler ( new BookmarksEventHandler(this) ),
		  m_ExternalToolsEventHandler ( new ExternalToolsEventHandler(this) )
{
	m_pQuiver = parent;
	
	m_BookmarksPtr = Bookmarks::GetInstance();
	m_BookmarksPtr->AddEventHandler(m_BookmarksEventHandler);

	m_ExternalToolsPtr = ExternalTools::GetInstance();
	m_ExternalToolsPtr->AddEventHandler(m_ExternalToolsEventHandler);

	m_ImageListPtr->AddEventHandler(m_ImageListEventHandler);	
	m_iMergedExternalToolsUI = 0;
	m_iMergedBookmarksUI = 0;
}

QuiverImpl::~QuiverImpl()
{
	m_BookmarksPtr->RemoveEventHandler(m_BookmarksEventHandler);
	m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandler);	

	m_ImageListPtr.reset();
	m_BrowserPtr.reset();
	m_ViewerPtr.reset();
	m_StatusbarPtr.reset();

	if (NULL != m_pUIManager)
		g_object_unref(m_pUIManager);

	gtk_widget_destroy(m_pQuiverWindow);
}

void QuiverImpl::LoadBookmarks()
{
	if (m_pUIManager)
	{
		vector<Bookmark> bookmarks = m_BookmarksPtr->GetBookmarks();

		if (0 != m_iMergedBookmarksUI)
		{
			gtk_ui_manager_remove_ui(m_pUIManager,m_iMergedBookmarksUI);
			gtk_ui_manager_ensure_update(m_pUIManager);
		}
		
		if (0 < bookmarks.size())
		{
			GtkActionGroup* actions2 = gtk_action_group_new ("BookmarkActions");		

			list<string>::iterator itr;
			list<string> ui_commands;
			
			for (unsigned int i = 0; i < bookmarks.size(); ++i)
			{
				GtkActionEntry action_entry = {0};
				action_entry.stock_id = bookmarks[i].GetIcon().c_str();
				// this should be unique, something like Bookmark_category_name
				stringstream ss;
				ss << "Bookmark_" << bookmarks[i].GetID();
				string name = ss.str();
				action_entry.name = name.c_str();
				action_entry.label = bookmarks[i].GetName().c_str();
				action_entry.accelerator = "";
				action_entry.tooltip = bookmarks[i].GetDescription().c_str();
				action_entry.callback = G_CALLBACK(quiver_action_handler_cb);
							

				gtk_action_group_add_actions    (actions2, &action_entry, 1, this);
				// this should be unique, something like Bookmark_category_name
				ui_commands.push_back(name.c_str());
			}
	
			gtk_ui_manager_insert_action_group (m_pUIManager,actions2,0);

			g_object_unref(actions2);
			
			string str_ui =
				"<ui>"
#ifdef QUIVER_MAEMO
				"	<popup name='MenubarMain'>"
#else
				"	<menubar name='MenubarMain'>"
#endif
				"		<menu action='MenuBookmarks'>"
				"			<placeholder name='BookmarkItems'>";
			for (itr = ui_commands.begin(); ui_commands.end() != itr; ++itr)
			{
				str_ui += "<menuitem action='" + (*itr) + "'/>";

			}
			
			str_ui +=
				"			</placeholder>"
				"		</menu>"
#ifdef QUIVER_MAEMO
				"	</popup>"
#else
				"	</menubar>"
#endif
				"</ui>";


			GError *error = NULL;
			m_iMergedBookmarksUI = gtk_ui_manager_add_ui_from_string
                (m_pUIManager,
                 str_ui.c_str(),
                 str_ui.length(),
                 &error);

			gtk_ui_manager_ensure_update(m_pUIManager);

			if (NULL != error)
			{
				printf("error: %s\n",error->message);
				g_error_free(error);
			}
		}
	}
}

void QuiverImpl::LoadExternalTools()
{
	if (m_pUIManager)
	{
		vector<ExternalTool> externaltools = m_ExternalToolsPtr->GetExternalTools();

		if (0 != m_iMergedExternalToolsUI)
		{
			gtk_ui_manager_remove_ui(m_pUIManager,m_iMergedExternalToolsUI);
			gtk_ui_manager_ensure_update(m_pUIManager);
		}
		
		if (0 < externaltools.size())
		{
			GtkActionGroup* actions2 = gtk_action_group_new ("ExternalToolActions");		

			list<string>::iterator itr;
			list<string> ui_commands;
			
			for (unsigned int i = 0; i < externaltools.size(); ++i)
			{
				GtkActionEntry action_entry = {0};
				action_entry.stock_id = externaltools[i].GetIcon().c_str();
				// this should be unique, something like Bookmark_category_name
				stringstream ss;
				ss << "ExternalTool_" << externaltools[i].GetID();
				string name = ss.str();
				action_entry.name = name.c_str();
				action_entry.label = externaltools[i].GetName().c_str();
				action_entry.accelerator = "";
				action_entry.tooltip = externaltools[i].GetTooltip().c_str();
				action_entry.callback = G_CALLBACK(quiver_action_handler_cb);

				gtk_action_group_add_actions    (actions2, &action_entry, 1, this);
				// this should be unique, something like Bookmark_category_name
				ui_commands.push_back(name.c_str());
			}
	
			gtk_ui_manager_insert_action_group (m_pUIManager,actions2,0);

			g_object_unref(actions2);
			
			string str_ui =
				"<ui>"
#ifdef QUIVER_MAEMO
				"	<popup name='MenubarMain'>"
#else
				"	<menubar name='MenubarMain'>"
#endif
				"		<menu action='MenuTools'>"
				"			<placeholder name='ToolsExternal'>";
			for (itr = ui_commands.begin(); ui_commands.end() != itr; ++itr)
			{
				str_ui += "<menuitem action='" + (*itr) + "'/>";

			}
			
			str_ui +=
				"			</placeholder>"
				"		</menu>"
#ifdef QUIVER_MAEMO
				"	</popup>"
#else
				"	</menubar>"
#endif
				"</ui>";

			if (0 != m_iMergedExternalToolsUI)
			{
				gtk_ui_manager_remove_ui(m_pUIManager,m_iMergedExternalToolsUI);
				gtk_ui_manager_ensure_update(m_pUIManager);
			}

			GError *error = NULL;
			m_iMergedExternalToolsUI = gtk_ui_manager_add_ui_from_string
                (m_pUIManager,
                 str_ui.c_str(),
                 str_ui.length(),
                 &error);

			gtk_ui_manager_ensure_update(m_pUIManager);

			if (NULL != error)
			{
				printf("error: %s\n",error->message);
				g_error_free(error);
			}
		}
	}
}

void QuiverImpl::Save()
{
	if (m_CurrentQuiverFile.Modified() && m_CurrentQuiverFile.IsWriteable())
	{
		SaveImageTaskPtr saveImageTaskPtr(new SaveImageTask(m_CurrentQuiverFile));
		TaskManager::GetInstance()->AddTask(saveImageTaskPtr);
	}
}

void QuiverImpl::SaveAs()
{
	cout << "Supported write file types: " << endl;
	GSList *formats = gdk_pixbuf_get_formats ();
	//GSList *writable_formats = NULL;
	GdkPixbufFormat * fmt;
	while (NULL != formats)
	{
		fmt = (GdkPixbufFormat*)formats->data;
		
		if (gdk_pixbuf_format_is_writable(fmt))
		{
			//cout << gdk_pixbuf_format_get_name(fmt) <<": " << endl;
			//cout << gdk_pixbuf_format_get_description(fmt) << endl;
			gchar ** ext_ptr_head = gdk_pixbuf_format_get_extensions(fmt);
			gchar ** ext_ptr = ext_ptr_head;
			while (NULL != *ext_ptr)
			{
				cout << *ext_ptr << "," ;
				ext_ptr++;
			}
			g_strfreev(ext_ptr_head);
			//cout << endl;
			ext_ptr_head = gdk_pixbuf_format_get_mime_types(fmt);
			ext_ptr = ext_ptr_head;
			while (NULL != *ext_ptr)
			{
				//c_setSupportedMimeTypes.insert(*ext_ptr);
				cout << *ext_ptr << "," ;
				ext_ptr++;
			}
			g_strfreev(ext_ptr_head);
			cout << endl;
		}
		
		formats = g_slist_next(formats);
		//g_slist_foreach (formats, add_if_writable, &writable_formats);
	}
	g_slist_free (formats);
}

bool QuiverImpl::CanClose()
{
#ifndef QUIVER_MAEMO
	gtk_window_get_position(GTK_WINDOW(m_pQuiverWindow),&m_iAppX,&m_iAppY);
	gtk_window_get_size(GTK_WINDOW(m_pQuiverWindow),&m_iAppWidth,&m_iAppHeight);
#endif
	return true;
}

#define ACTION_QUIVER_OPEN                                   "FileOpen"
#define ACTION_QUIVER_OPEN_FOLDER                            "FileOpenFolder"
#define ACTION_QUIVER_SAVE                                   "Save"
#define ACTION_QUIVER_SAVE_AS                                "SaveAs"
#define ACTION_QUIVER_CLOSE                                  "Close"
#define ACTION_QUIVER_PREFERENCES                            "Preferences"
#define ACTION_QUIVER_VIEW_MENUBAR                           "ViewMenubar"
#define ACTION_QUIVER_VIEW_TOOLBAR_MAIN                      "ViewToolbarMain"
#define ACTION_QUIVER_VIEW_PROPERTIES                        "ViewProperties"
#define ACTION_QUIVER_VIEW_STATUSBAR                         "ViewStatusbar"
#define ACTION_QUIVER_SORT_BY_NAME                           "SortByName"
#define ACTION_QUIVER_SORT_BY_NAME_NATURAL                   "SortByNameNatural"
#define ACTION_QUIVER_SORT_BY_DATE                           "SortByDate"
#define ACTION_QUIVER_SORT_BY_DATE_MODIFIED                  "SortByDateModified"
#define ACTION_QUIVER_SORT_BY_RANDOM                         "SortByRandom"
#define ACTION_QUIVER_SORT_DESCENDING                        "SortDescending"
#define ACTION_QUIVER_FULLSCREEN                             "FullScreen"
#define ACTION_QUIVER_SLIDESHOW                              "SlideShow"
#define ACTION_QUIVER_BOOKMARKS_ADD                          "BookmarksAdd"
#define ACTION_QUIVER_BOOKMARKS_EDIT                         "BookmarksEdit"
#define ACTION_QUIVER_EXTERNAL_TOOLS                         "ExternalTools"
#define ACTION_QUIVER_ADJUST_DATE                            "AdjustDate"
#define ACTION_QUIVER_ORGANIZE                               "Organize"
#define ACTION_QUIVER_RENAME                                 "Rename"
#define ACTION_QUIVER_DONATE                                 "Donate"
#define ACTION_QUIVER_ABOUT                                  "About"
#define ACTION_QUIVER_UI_MODE_BROWSER                        "UIModeBrowser"
#define ACTION_QUIVER_UI_MODE_VIEWER                         "UIModeViewer"
#define ACTION_QUIVER_ESCAPE                                 "QuiverEscape"
#define ACTION_QUIVER_CLOSE_2                                ACTION_QUIVER_CLOSE"_2"
#define ACTION_QUIVER_CLOSE_3                                ACTION_QUIVER_CLOSE"_3"
#define ACTION_QUIVER_CLOSE_4                                ACTION_QUIVER_CLOSE"_4"

#ifdef QUIVER_MAEMO
#define ACTION_QUIVER_UI_MODE_SWITCH_MAEMO                   "UIModeSwitch_MAEMO"
#define ACTION_QUIVER_UI_MODE_VIEWER_MAEMO                   ACTION_QUIVER_UI_MODE_VIEWER"_MAEMO"
#define ACTION_QUIVER_FULLSCREEN_MAEMO                       ACTION_QUIVER_FULLSCREEN"_MAEMO"
#endif

static const char * quiver_ui_main =
"<ui>"
#ifdef QUIVER_MAEMO
"	<popup name='MenubarMain'>"
#else
"	<menubar name='MenubarMain'>"
#endif
"		<menu action='MenuFile'>"
#ifdef FIXME_DISABLED
"			<menuitem action='"ACTION_QUIVER_OPEN"'/>"
"			<menuitem action='"ACTION_QUIVER_OPEN_FOLDER"'/>"
#endif
"			<placeholder action='FileOpenItems' />"
"			<separator/>"
#ifndef QUIVER_MAEMO // FIXME - fix saving for maemo
"			<menuitem action='"ACTION_QUIVER_SAVE"'/>"
#endif
#ifdef FIXME_DISABLED
"			<menuitem action='"ACTION_QUIVER_SAVE_AS"'/>"
#endif
"			<separator/>"
"			<placeholder action='FileMenuAdditions' />"
"			<separator/>"
#ifndef QUIVER_MAEMO
"			<menuitem action='"ACTION_QUIVER_CLOSE"'/>"
#endif
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
"			<menuitem action='"ACTION_QUIVER_PREFERENCES"'/>"
"		</menu>"
"		<menu action='MenuView'>"
"			<placeholder name='UIModeItems'/>"
#ifndef QUIVER_MAEMO
"			<menuitem action='"ACTION_QUIVER_VIEW_MENUBAR"'/>"
#endif
"			<menuitem action='"ACTION_QUIVER_VIEW_TOOLBAR_MAIN"'/>"
"			<menuitem action='"ACTION_QUIVER_VIEW_PROPERTIES"'/>"
"		 	<menuitem action='"ACTION_QUIVER_VIEW_STATUSBAR"'/>"
"			<placeholder name='UIItems'/>"
"			<separator/>"
"			<menu action='MenuSort'>"
"				<menuitem action='"ACTION_QUIVER_SORT_BY_NAME"'/>"
"				<menuitem action='"ACTION_QUIVER_SORT_BY_NAME_NATURAL"'/>"
"				<menuitem action='"ACTION_QUIVER_SORT_BY_DATE"'/>"
"				<menuitem action='"ACTION_QUIVER_SORT_BY_DATE_MODIFIED"'/>"
"				<menuitem action='"ACTION_QUIVER_SORT_BY_RANDOM"'/>"
"				<separator/>"
"				<menuitem action='"ACTION_QUIVER_SORT_DESCENDING"'/>"
"			</menu>"
"			<separator/>"
"			<menuitem action='"ACTION_QUIVER_FULLSCREEN"'/>"
"			<menuitem action='"ACTION_QUIVER_SLIDESHOW"'/>"
"			<separator/>"
"			<placeholder name='Zoom'/>"
"			<separator/>"
"		</menu>"
"		<placeholder name='MenuImage'/>"
"		<menu action='MenuGo'>"
"			<placeholder name='ImageNavigation'/>"
"			<separator/>"
"			<placeholder name='FolderNavigation'/>"
"			<separator/>"
"		</menu>"
"		<menu action='MenuBookmarks'>"
"			<menuitem action='"ACTION_QUIVER_BOOKMARKS_ADD"'/>"
"			<menuitem action='"ACTION_QUIVER_BOOKMARKS_EDIT"'/>"
"			<separator/>"
"			<placeholder name='BookmarkItems'/>"
"		</menu>"
"		<menu action='MenuTools'>"
"			<menuitem action='"ACTION_QUIVER_ADJUST_DATE"'/>"
"			<menuitem action='"ACTION_QUIVER_RENAME"'/>"
"			<menuitem action='"ACTION_QUIVER_ORGANIZE"'/>"
"			<menuitem action='"ACTION_QUIVER_EXTERNAL_TOOLS"'/>"
"			<separator/>"
"			<placeholder name='ToolsExternal'/>"
"		</menu>"
"		<menu action='MenuWindow'>"
"		</menu>"
"		<menu action='MenuHelp'>"
"			<menuitem action='"ACTION_QUIVER_DONATE"'/>"
"			<separator/>"
"			<menuitem action='"ACTION_QUIVER_ABOUT"'/>"
"		</menu>"
#ifdef QUIVER_MAEMO
"		<menuitem action='"ACTION_QUIVER_CLOSE"'/>"
"	</popup>"
#else
"	</menubar>"
#endif
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='UIModeItems'/>"
"		<separator/>"
"		<placeholder name='NavToolItems'/>"
"		<separator/>"
"		<placeholder name='UIItems'/>"
"		<separator/>"
"		<toolitem action='"ACTION_QUIVER_FULLSCREEN"'/>"
"		<separator/>"
"		<toolitem action='"ACTION_QUIVER_SLIDESHOW"'/>"
"		<separator/>"
"		<placeholder name='ZoomToolItems'/>"
"		<separator/>"
"		<placeholder name='TransformToolItems'/>"
"		<separator/>"
"		<placeholder name='Trash'/>"
"		<separator/>"
"	</toolbar>"
"	<popup name='ContextMenuMain'>"
"	</popup>"
"	<accelerator action='"ACTION_QUIVER_CLOSE_2"'/>"
"	<accelerator action='"ACTION_QUIVER_CLOSE_3"'/>"
"	<accelerator action='"ACTION_QUIVER_CLOSE_4"'/>"
"	<accelerator action='"ACTION_QUIVER_ESCAPE"'/>"
#ifdef QUIVER_MAEMO
"	<accelerator action='"ACTION_QUIVER_FULLSCREEN_MAEMO"'/>"
"	<accelerator action='"ACTION_QUIVER_UI_MODE_SWITCH_MAEMO"'/>"
#endif
"</ui>";


static const char *quiver_ui_browser =
"<ui>"
#ifdef QUIVER_MAEMO
"	<popup name='MenubarMain'>"
#else
"	<menubar name='MenubarMain'>"
#endif
"		<menu action='MenuView'>"
"			<placeholder name='UIModeItems'>"
"				<separator/>"
"				<menuitem action='"ACTION_QUIVER_UI_MODE_VIEWER"'/>"
"				<separator/>"
"			</placeholder>"
"		</menu>"
#ifdef QUIVER_MAEMO
"	</popup>"
#else
"	</menubar>"
#endif
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='UIModeItems'>"
"			<separator/>"
"			<toolitem action='"ACTION_QUIVER_UI_MODE_VIEWER"'/>"
"			<separator/>"
"		</placeholder>"
"		<placeholder name='NavToolItems'>"
"			<separator/>"
"		</placeholder>"
"	</toolbar>"
"	<popup name='ContextMenuMain'>"
"	</popup>"
"</ui>";


static const char *quiver_ui_viewer =
"<ui>"
#ifdef QUIVER_MAEMO
"	<popup name='MenubarMain'>"
#else
"	<menubar name='MenubarMain'>"
#endif
"		<menu action='MenuView'>"
"			<placeholder name='UIModeItems'>"
"				<separator/>"
"				<menuitem action='"ACTION_QUIVER_UI_MODE_BROWSER"'/>"
"				<separator/>"
"			</placeholder>"
"		</menu>"
#ifdef QUIVER_MAEMO
"	</popup>"
#else
"	</menubar>"
#endif
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='UIModeItems'>"
"			<separator/>"
"			<toolitem action='"ACTION_QUIVER_UI_MODE_BROWSER"'/>"
"			<separator/>"
"		</placeholder>"
"	</toolbar>"
"	<popup name='ContextMenuMain'>"
"	</popup>"
"</ui>";

GtkToggleActionEntry QuiverImpl::action_entries_toggle[] = {
	{ ACTION_QUIVER_FULLSCREEN, QUIVER_STOCK_FULLSCREEN , N_("_Full Screen"), "f", N_("Toggle Full Screen Mode"), G_CALLBACK(quiver_action_handler_cb),FALSE},

#ifdef QUIVER_MAEMO
	{ ACTION_QUIVER_FULLSCREEN_MAEMO, "", N_("_Full Screen"), "F6", N_("Toggle Full Screen Mode"), G_CALLBACK(quiver_action_handler_cb),FALSE},
#endif
	{ ACTION_QUIVER_SLIDESHOW,QUIVER_STOCK_SLIDESHOW, N_("_Slide Show"), "s", N_("Toggle Slide Show"), G_CALLBACK(quiver_action_handler_cb),FALSE},	
	{ ACTION_QUIVER_VIEW_MENUBAR, QUIVER_STOCK_ZOOM_IN,"Menubar", "<Control><Shift>M", "Show/Hide the Menubar", G_CALLBACK(quiver_action_handler_cb),TRUE},
	{ ACTION_QUIVER_VIEW_TOOLBAR_MAIN, QUIVER_STOCK_ZOOM_IN,"Toolbar", "<Control><Shift>T", "Show/Hide the Toolbar", G_CALLBACK(quiver_action_handler_cb),TRUE},
	{ ACTION_QUIVER_VIEW_STATUSBAR, QUIVER_STOCK_ZOOM_IN,"Statusbar", "<Control><Shift>S", "Show/Hide the Statusbar", G_CALLBACK(quiver_action_handler_cb),TRUE},
	{ ACTION_QUIVER_VIEW_PROPERTIES, QUIVER_STOCK_PROPERTIES,"Properties", "<Alt>Return", "Show/Hide Image Properties", G_CALLBACK(quiver_action_handler_cb),FALSE},
	{ ACTION_QUIVER_SORT_DESCENDING, QUIVER_STOCK_SORT_DESCENDING, "In Descending Order", "", "Arrange the items in descending order", G_CALLBACK(quiver_action_handler_cb),FALSE},
};

GtkRadioActionEntry QuiverImpl::sort_radio_action_entries[] = {
	{ ACTION_QUIVER_SORT_BY_NAME, "","By Name", "", "Sort by file name", ImageList::SORT_BY_FILENAME},
	{ ACTION_QUIVER_SORT_BY_NAME_NATURAL, "","By Name (natural order)", "", "Sort by file name (natural order)", ImageList::SORT_BY_FILENAME_NATURAL},
	{ ACTION_QUIVER_SORT_BY_DATE, "","By Date", "", "Sort by date", ImageList::SORT_BY_DATE},
	{ ACTION_QUIVER_SORT_BY_DATE_MODIFIED, "","By Date Modified", "", "Sort by date modified", ImageList::SORT_BY_DATE_MODIFIED},
	{ ACTION_QUIVER_SORT_BY_RANDOM, "","Randomize", "", "Randomize", ImageList::SORT_BY_RANDOM},
};

GtkActionEntry QuiverImpl::action_entries[] = {
	
	{ "MenuFile", NULL, N_("_File") },
	{ "MenuZoom", NULL, N_("Zoom")},
	{ "MenuTransform", NULL, N_("Transform")},
	{ "MenuEdit", NULL, N_("_Edit") },
	{ "MenuView", NULL, N_("_View") },
	{ "MenuSort", NULL, N_("_Arrange Items") },
	{ "MenuImage", NULL, N_("_Image") },
	{ "MenuGo", NULL, N_("_Go") },
	{ "MenuTools", NULL, N_("_Tools") },
	{ "MenuWindow", NULL, N_("_Window") },
	{ "MenuHelp", NULL, N_("_Help") },

	{ ACTION_QUIVER_UI_MODE_BROWSER,QUIVER_STOCK_BROWSER , "_Browser", "", "Browse Images", G_CALLBACK(quiver_action_handler_cb)},
	{ ACTION_QUIVER_UI_MODE_VIEWER, QUIVER_STOCK_APP, "_Viewer", "<Control>b", "View Image", G_CALLBACK(quiver_action_handler_cb)},
#ifdef QUIVER_MAEMO
	{ ACTION_QUIVER_UI_MODE_SWITCH_MAEMO, "" , NULL, "Return", NULL, G_CALLBACK(quiver_action_handler_cb)},
#endif

	{ ACTION_QUIVER_OPEN, QUIVER_STOCK_OPEN, "_Open", "<Control>o", "Open an image", G_CALLBACK(quiver_action_handler_cb)},
	{ ACTION_QUIVER_OPEN_FOLDER, QUIVER_STOCK_OPEN, "Open _Folder", "<Control>f", "Open a Folder", G_CALLBACK( quiver_action_handler_cb )},
	{ ACTION_QUIVER_SAVE, QUIVER_STOCK_SAVE, "_Save", "<Control>s", "Save the Image", G_CALLBACK(quiver_action_handler_cb)},
	{ ACTION_QUIVER_SAVE_AS, QUIVER_STOCK_SAVE, "Save _As", "", "Save the Image As", G_CALLBACK(quiver_action_handler_cb)},
	
	{ ACTION_QUIVER_CLOSE, QUIVER_STOCK_QUIT, "_Close", "<Alt>F4", "Close quiver", G_CALLBACK( quiver_action_handler_cb )},
	{ ACTION_QUIVER_CLOSE_2, QUIVER_STOCK_QUIT, "_Close", "q", "Close quiver", G_CALLBACK( quiver_action_handler_cb )},
	{ ACTION_QUIVER_CLOSE_3, QUIVER_STOCK_QUIT, "_Close", "<Control>q", "Close quiver", G_CALLBACK( quiver_action_handler_cb )},	
	{ ACTION_QUIVER_CLOSE_4, QUIVER_STOCK_QUIT, "_Close", "<Control>w", "Close quiver", G_CALLBACK( quiver_action_handler_cb )},	
	{ ACTION_QUIVER_ESCAPE, "", NULL, "Escape", NULL, G_CALLBACK( quiver_action_handler_cb )},	

	{ ACTION_QUIVER_PREFERENCES, QUIVER_STOCK_PREFERENCES, "_Preferences", "<Control>p", "Edit quiver preferences", G_CALLBACK(quiver_action_handler_cb)},



	{ "MenuBookmarks", NULL, "_Bookmarks" },
	{ ACTION_QUIVER_BOOKMARKS_ADD, QUIVER_STOCK_ADD, "_Add Bookmark", "<Control>d", "Add a bookmark", G_CALLBACK(quiver_action_handler_cb)},
	{ ACTION_QUIVER_BOOKMARKS_EDIT, QUIVER_STOCK_EDIT, "_Edit Bookmarks...", "<Control>b", "Edit the bookmarks", G_CALLBACK(quiver_action_handler_cb)},

	{ ACTION_QUIVER_RENAME, QUIVER_STOCK_EDIT, "Rename...", "", "Rename image(s)", G_CALLBACK(quiver_action_handler_cb)},
	{ ACTION_QUIVER_ORGANIZE, QUIVER_STOCK_EDIT, "Organize...", "", "Organize photos", G_CALLBACK(quiver_action_handler_cb)},
	{ ACTION_QUIVER_ADJUST_DATE, QUIVER_STOCK_EDIT, "Adjust Date...", "", "Adjust Exif Dates", G_CALLBACK(quiver_action_handler_cb)},
	{ ACTION_QUIVER_EXTERNAL_TOOLS, QUIVER_STOCK_EDIT, "External Tools...", "", "Add / edit external tools", G_CALLBACK(quiver_action_handler_cb)},

	{ ACTION_QUIVER_DONATE, "", "_Donate...", "", "Help support quiver by donating...", G_CALLBACK( quiver_action_handler_cb )},
	{ ACTION_QUIVER_ABOUT, QUIVER_STOCK_ABOUT, "_About", "", "About quiver", G_CALLBACK( quiver_action_handler_cb )},

};


GtkTargetEntry QuiverImpl::quiver_drag_target_table[] = {
		{ "STRING",     0, QUIVER_TARGET_STRING }, // STRING is used for legacy motif apps
		{ "text/plain", 0, QUIVER_TARGET_STRING },  // the real mime types to support
		 { "text/uri-list", 0, QUIVER_TARGET_URI },
	};

/*
void Quiver::SignalDragDataReceived (GtkWidget *widget,GdkDragContext *drag_context, gint x,gint y,
                                            GtkSelectionData *data, guint info, guint time,gpointer user_data)
{
	gboolean retval = FALSE;
  GDK_ACTION_DEFAULT = 1 << 0,
  GDK_ACTION_COPY    = 1 << 1,
  GDK_ACTION_MOVE    = 1 << 2,
  GDK_ACTION_LINK    = 1 << 3,
  GDK_ACTION_PRIVATE = 1 << 4,
  GDK_ACTION_ASK     = 1 << 5
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
				m_QuiverImplPtr->m_ImageListPtr->Add(&file_list);
			}
			else
			{
				//create a new list
				//cout << "suggested action : GDK_ACTION_COPY! " << endl;
				m_QuiverImplPtr->m_ImageListPtr->SetImageList(&file_list);
			}
		}
	}
	
	gtk_drag_finish (drag_context, retval, FALSE, time);
	
}
*/
/*
void Quiver::signal_drag_begin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{
	((Quiver*)user_data)->SignalDragBegin(widget,drag_context,user_data);
}
void  Quiver::signal_drag_end(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{
	((Quiver*)user_data)->SignalDragEnd(widget,drag_context,user_data);
}
*/
/*
void  Quiver::SignalDragEnd(GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{
	//re-enable drop
	gtk_drag_dest_set(m_QuiverImplPtr->m_pQuiverWindow,GTK_DEST_DEFAULT_ALL,
		quiver_drag_target_table, 3, (GdkDragAction)(GDK_ACTION_COPY|GDK_ACTION_MOVE));
}
*/
/*
void  Quiver::SignalDragBegin (GtkWidget *widget,GdkDragContext *drag_context,gpointer user_data)
{
	
	// disable drop 
	gtk_drag_dest_unset(m_QuiverImplPtr->m_pQuiverWindow);
	
	// TODO
	// set icon
	GdkPixbuf *thumb = m_QuiverImplPtr->m_ImageListPtr->GetCurrent().GetThumbnail();

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
		if (m_QuiverImplPtr->m_ImageListPtr->GetSize())
		{
    		gtk_selection_data_set (selection_data,
			    selection_data->target,
			    8, (const guchar*)m_QuiverImplPtr->m_ImageListPtr->GetCurrent().GetURI(),strlen(m_QuiverImplPtr->m_ImageListPtr->GetCurrent().GetURI()));
		}
	}
	else if (info == QUIVER_TARGET_URI)
	{
		g_print ("drop the image uri as text/uri\n");
		if (m_QuiverImplPtr->m_ImageListPtr->GetSize())
		{
			//selection data set
			//context->suggested_action = GDK_ACTION_LINK;
    		gtk_selection_data_set (selection_data,
			    selection_data->target,
			    8, (const guchar*)m_QuiverImplPtr->m_ImageListPtr->GetCurrent().GetURI(),strlen(m_QuiverImplPtr->m_ImageListPtr->GetCurrent().GetURI()));
		}
	}
  	else
	{
		gtk_selection_data_set (selection_data,
				selection_data->target,
				8, (const guchar*)"I'm Data!", 9);
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
*/

void Quiver::SetWindowTitle(string s)
{
	string title = "quiver - " + s;
#ifndef QUIVER_MAEMO
	gtk_window_set_title (GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow), title.c_str());
#endif	
}

void Quiver::ImageChanged()
{
	if ( m_QuiverImplPtr->m_ImageListPtr->GetSize() )
	{
		QuiverFile f = m_QuiverImplPtr->m_ImageListPtr->GetCurrent();
		
		m_QuiverImplPtr->Save();
		
		m_QuiverImplPtr->m_CurrentQuiverFile = f;
		
		SetWindowTitle( f.GetFilePath() );
		
		m_QuiverImplPtr->m_StatusbarPtr->SetPosition(m_QuiverImplPtr->m_ImageListPtr->GetCurrentIndex()+1,m_QuiverImplPtr->m_ImageListPtr->GetSize());
		m_QuiverImplPtr->m_ExifView.SetQuiverFile(f);
		m_QuiverImplPtr->m_StatusbarPtr->SetQuiverFile(f);
		
	}
	else
	{
		m_QuiverImplPtr->m_StatusbarPtr->SetPosition(0,0);
		QuiverFile f;
		m_QuiverImplPtr->m_StatusbarPtr->SetQuiverFile(f);
	}
}

#ifdef QUIVER_MAEMO
static void notify_gtk_enable_accels_changed (GObject *gobject, GParamSpec *arg1, gpointer user_data)
{
	gboolean value = FALSE;
	g_object_get(gobject, arg1->name, &value,NULL);
	if (FALSE == value)
	{
		g_object_set(gobject, arg1->name,TRUE,NULL);
	}
}
#endif

static gboolean event_window_state( GtkWidget *widget, GdkEventWindowState *event, gpointer data )
{
	QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
	PreferencesPtr prefsPtr = Preferences::GetInstance();

	gboolean bFullscreen = FALSE;
	pQuiverImpl->m_WindowState = event->new_window_state;
	//cout << event->new_window_state << " FS: " << GDK_WINDOW_STATE_FULLSCREEN <<endl;
	//
	pQuiverImpl->UpdateUI();

	if (GDK_WINDOW_STATE_FULLSCREEN & event->new_window_state)
	{
		prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN, true);

		pQuiverImpl->m_bTimeoutEventMotionNotifyRunning = true;
		g_timeout_add(1500, timeout_event_motion_notify,pQuiverImpl);
		
		bFullscreen = TRUE;
	}
	else
	{
		prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN, false);
		pQuiverImpl->m_bSlideShowRestoreFromFS = false;
#ifdef QUIVER_MAEMO
		gdk_window_set_cursor (pQuiverImpl->m_pQuiverWindow->window, NULL);
#endif
	}
	
	GtkAction * action = QuiverUtils::GetAction(pQuiverImpl->m_pUIManager,ACTION_QUIVER_FULLSCREEN);
	if (NULL != action)
	{
		g_signal_handlers_block_matched (action,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,pQuiverImpl);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),bFullscreen);
		g_signal_handlers_unblock_matched (action,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,pQuiverImpl);

	}

	return FALSE;
}

gboolean Quiver::event_delete( GtkWidget *widget,GdkEvent  *event, gpointer   data )
{
	return ((Quiver*)data)->EventDelete(widget,event,data);
}


void Quiver::Close()
{
	SaveSettings();
	// force reference count to 0 for the quiverimplptr
	m_QuiverImplPtr->m_BrowserPtr->RemoveEventHandler(m_QuiverImplPtr->m_BrowserEventHandler);
	m_QuiverImplPtr->m_ViewerPtr->RemoveEventHandler(m_QuiverImplPtr->m_ViewerEventHandler);
	
	PreferencesPtr prefs = Preferences::GetInstance();
	prefs->RemoveEventHandler(m_QuiverImplPtr->m_PreferencesEventHandler);
	prefs.reset();
	
	m_QuiverImplPtr.reset();

	// reset global smart pointers
	Bookmarks::Reset();
	ExternalTools::Reset();
	ImageSaveManager::Reset();
	TaskManagerDlg::Reset();
	TaskManager::Reset();
	Preferences::Reset();
	QuiverFile::ClearThumbnailCache();
	
	gtk_main_quit ();	
}

gboolean Quiver::EventDelete( GtkWidget *widget,GdkEvent  *event, gpointer   data )
{
	if (m_QuiverImplPtr->CanClose())
	{
		Close();
	}
	
	// don't send destroy signal
    return TRUE;
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
Quiver::Quiver(std::list<std::string> &images, bool bRecursive/* = false*/)
	: 	m_QuiverImplPtr(new QuiverImpl(this) )
{
	m_QuiverImplPtr->m_bListImagesRecursive = bRecursive;
	m_QuiverImplPtr->m_listImages = images;
	Init();

#ifdef QUIVER_MAEMO
	/* Initialize maemo application */
	osso_context = osso_initialize("org.yi.mike.quiver", PACKAGE_VERSION, TRUE, NULL);
    
	/* Check that initialization was ok */
	if (osso_context == NULL)
	{
		//return OSSO_ERROR;
	}
	else
	{
		osso_mime_set_cb (osso_context, mime_open_handler, m_QuiverImplPtr.get());
	}
#endif

}

void Quiver::Init()
{
	m_QuiverImplPtr->m_pUIManager = NULL;

	m_QuiverImplPtr->m_iMergedViewerUI = 0;
	m_QuiverImplPtr->m_iMergedBrowserUI = 0;	

	m_QuiverImplPtr->m_bSlideShowRestoreFromFS = false;
	
	m_QuiverImplPtr->m_bInitialized = false;
	m_QuiverImplPtr->m_bTimeoutEventMotionNotifyRunning = false;
	m_QuiverImplPtr->m_bTimeoutEventMotionNotifyMouseMoved = false;
	
	m_QuiverImplPtr->m_iTimeoutMouseMotionNotify = 0;
	m_QuiverImplPtr->m_iTimeoutKeepScreenOn = 0;

	m_QuiverImplPtr->m_WindowState = GDK_WINDOW_STATE_WITHDRAWN;

	//initialize
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->AddEventHandler(m_QuiverImplPtr->m_PreferencesEventHandler);
	
	m_QuiverImplPtr->m_BrowserPtr->AddEventHandler(m_QuiverImplPtr->m_BrowserEventHandler);
	m_QuiverImplPtr->m_ViewerPtr->AddEventHandler(m_QuiverImplPtr->m_ViewerEventHandler);

	/* Create the main window */
#ifdef QUIVER_MAEMO
	HildonProgram* program;
	program = HILDON_PROGRAM(hildon_program_get_instance());

	g_set_application_name("quiver");

	m_QuiverImplPtr->m_pQuiverWindow = hildon_window_new();
	hildon_program_add_window(program, HILDON_WINDOW(m_QuiverImplPtr->m_pQuiverWindow));
	
#ifdef HAVE_HILDON_1
	// add a callback here
	// the following is to work around a bug in maemo gtk 
	// which causes accelerators not to work
	// https://bugs.maemo.org/show_bug.cgi?id=2278
	// (only in OS2008)

	GtkSettings* settings = gtk_settings_get_default();
	g_object_set(settings, "gtk-enable-accels",TRUE,NULL);
	g_signal_connect (G_OBJECT(settings), "notify::gtk-enable-accels",
		G_CALLBACK (notify_gtk_enable_accels_changed), NULL);
#endif // HAVE_HILDON_1
#else
	m_QuiverImplPtr->m_pQuiverWindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_name(m_QuiverImplPtr->m_pQuiverWindow,"Quiver Window");
#endif


	if (LoadSettings())
	{	
		//set the size and position of the window
		//gtk_widget_set_uposition(quiver_window,m_iAppX,m_iAppY);
#ifndef QUIVER_MAEMO
		gtk_window_move(GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow),m_QuiverImplPtr->m_iAppX,m_QuiverImplPtr->m_iAppY);
		gtk_window_set_default_size (GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow),m_QuiverImplPtr->m_iAppWidth,m_QuiverImplPtr->m_iAppHeight);
#endif

	}
	
	gtk_window_set_default_icon_name (QUIVER_STOCK_APP);	

	/* Set up GUI elements */

	m_QuiverImplPtr->m_pUIManager = gtk_ui_manager_new();
	gtk_ui_manager_set_add_tearoffs (m_QuiverImplPtr->m_pUIManager,TRUE);

	gtk_ui_manager_add_ui_from_string(m_QuiverImplPtr->m_pUIManager,
			quiver_ui_main,
			strlen(quiver_ui_main),
			NULL);

	guint n_entries = G_N_ELEMENTS (m_QuiverImplPtr->action_entries);

	
	GtkActionGroup* actions = gtk_action_group_new ("GlobalActions");
	
	gtk_action_group_add_actions(actions, m_QuiverImplPtr->action_entries, n_entries, m_QuiverImplPtr.get());
                                 
	gtk_action_group_add_toggle_actions(actions,
										m_QuiverImplPtr->action_entries_toggle, 
										G_N_ELEMENTS (m_QuiverImplPtr->action_entries_toggle),
										m_QuiverImplPtr.get());

	gtk_action_group_add_radio_actions(actions,
										m_QuiverImplPtr->sort_radio_action_entries, 
										G_N_ELEMENTS (m_QuiverImplPtr->sort_radio_action_entries),
										ImageList::SORT_BY_FILENAME_NATURAL,
										G_CALLBACK(quiver_radio_action_handler_cb),
										m_QuiverImplPtr.get());										

	gtk_ui_manager_insert_action_group (m_QuiverImplPtr->m_pUIManager,actions,0);

	g_object_unref(actions);

	GtkWidget* toolbar = gtk_ui_manager_get_widget(m_QuiverImplPtr->m_pUIManager,"/ui/ToolbarMain/");
	if (NULL != toolbar)
	{
		gtk_toolbar_set_style(GTK_TOOLBAR(toolbar),GTK_TOOLBAR_ICONS);
#ifdef QUIVER_MAEMO
		gtk_toolbar_set_tooltips(GTK_TOOLBAR(toolbar),FALSE);
#endif
	}
                                             
	g_signal_connect (m_QuiverImplPtr->m_pUIManager, "connect_proxy",
		G_CALLBACK (signal_connect_proxy), m_QuiverImplPtr.get());
	g_signal_connect (m_QuiverImplPtr->m_pUIManager, "disconnect_proxy",
		G_CALLBACK (signal_disconnect_proxy), m_QuiverImplPtr.get());

                                             
	gtk_window_add_accel_group (GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow),
								gtk_ui_manager_get_accel_group(m_QuiverImplPtr->m_pUIManager));
	

	m_QuiverImplPtr->m_BrowserPtr->SetUIManager(m_QuiverImplPtr->m_pUIManager);
	m_QuiverImplPtr->m_ViewerPtr->SetUIManager(m_QuiverImplPtr->m_pUIManager);
	
	m_QuiverImplPtr->m_BrowserPtr->SetStatusbar(m_QuiverImplPtr->m_StatusbarPtr);
	m_QuiverImplPtr->m_ViewerPtr->SetStatusbar(m_QuiverImplPtr->m_StatusbarPtr);
	
	m_QuiverImplPtr->m_ExifView.SetUIManager(m_QuiverImplPtr->m_pUIManager);
	//GTK_WIDGET_UNSET_FLAGS(toolbar,GTK_CAN_FOCUS);

 	//gtk_widget_add_events (m_pQuiverWindow, GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);
	
    g_signal_connect (G_OBJECT (m_QuiverImplPtr->m_pQuiverWindow), "window_state_event",
    			G_CALLBACK (event_window_state), m_QuiverImplPtr.get());

    g_signal_connect (G_OBJECT (m_QuiverImplPtr->m_pQuiverWindow), "delete_event",
    			G_CALLBACK (Quiver::event_delete), this);
    	/*
    g_signal_connect (G_OBJECT (m_pQuiverWindow), "scroll_event",
    			G_CALLBACK (Quiver::event_scroll), this);
    	*/
	g_signal_connect (G_OBJECT (m_QuiverImplPtr->m_pQuiverWindow), "button_press_event",
				G_CALLBACK (quiver_window_button_press), m_QuiverImplPtr.get());

	g_signal_connect (G_OBJECT (m_QuiverImplPtr->m_pQuiverWindow), "motion_notify_event",
				G_CALLBACK (event_motion_notify), m_QuiverImplPtr.get());
				
			


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

	GtkWidget* hbox_browser_viewer_container;
	
	vbox = gtk_vbox_new(FALSE,0);
	m_QuiverImplPtr->m_pHPanedMainArea = gtk_hpaned_new();
	gtk_widget_set_name(m_QuiverImplPtr->m_pHPanedMainArea,"Quiver hpaned");
	
	hbox_browser_viewer_container = gtk_hbox_new(FALSE,0);
	gtk_widget_set_name(hbox_browser_viewer_container,"Quiver hbox 1");
	m_QuiverImplPtr->m_pNBProperties = gtk_notebook_new();
	gtk_widget_set_name(m_QuiverImplPtr->m_pNBProperties ,"Quiver notebook 1");
	
	gtk_widget_set_no_show_all(m_QuiverImplPtr->m_pNBProperties,TRUE);
	
	bool prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PROPS_SHOW);

	GtkAction *action = QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_VIEW_PROPERTIES);
	if (prefs_show)
	{
		gtk_widget_show(m_QuiverImplPtr->m_pNBProperties);
	}
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), prefs_show ? TRUE : FALSE);

	
	action = QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_SORT_BY_NAME_NATURAL);
	if (action)
	{
		int sortby = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_SORT_BY,ImageList::SORT_BY_FILENAME_NATURAL);
		gtk_radio_action_set_current_value (GTK_RADIO_ACTION(action), sortby);
	}

	action = QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_SORT_DESCENDING);
	if (action)
	{
		bool bDec = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_SORT_REVERSED,false);
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION(action), bDec ? TRUE : FALSE );
	}

	//FIXME: temp notebook stuff
	//gtk_notebook_append_page(GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties),gtk_drawing_area_new(),gtk_label_new("File"));
	//gtk_notebook_append_page(GTK_NOTEBOOK(m_pNBProperties),gtk_drawing_area_new(),gtk_label_new("Exif"));
	gtk_notebook_append_page(GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties),m_QuiverImplPtr->m_ExifView.GetWidget(),gtk_label_new("Exif"));
	//gtk_notebook_append_page(GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties),gtk_drawing_area_new(),gtk_label_new("IPTC"));
	//gtk_notebook_append_page(GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties),gtk_drawing_area_new(),gtk_label_new("Database"));
	gtk_notebook_popup_enable(GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties));
	gtk_notebook_set_scrollable (GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties),TRUE);
	
	// statusbar
	statusbar =  m_QuiverImplPtr->m_StatusbarPtr->GetWidget();
	gtk_widget_show_all(statusbar);
	gtk_widget_hide(statusbar);
	gtk_widget_set_no_show_all(statusbar,TRUE);
	

#ifdef QUIVER_MAEMO 
	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW, false);
#else
	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW, true);
#endif
	if (prefs_show)
	{
		gtk_widget_show(statusbar);
	}
	action = QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_VIEW_STATUSBAR);
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), prefs_show ? TRUE : FALSE);

	// menubar
	m_QuiverImplPtr->m_pMenubar = gtk_ui_manager_get_widget (m_QuiverImplPtr->m_pUIManager,"/ui/MenubarMain");
	gtk_widget_set_no_show_all(m_QuiverImplPtr->m_pMenubar,TRUE);

	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW, true);
	if (prefs_show)
	{
		gtk_widget_show(m_QuiverImplPtr->m_pMenubar);
	}
	action = QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_VIEW_MENUBAR);
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), prefs_show ? TRUE : FALSE);
	
	// toolbar
	m_QuiverImplPtr->m_pToolbar = gtk_ui_manager_get_widget (m_QuiverImplPtr->m_pUIManager,"/ui/ToolbarMain");
	gtk_widget_set_no_show_all(m_QuiverImplPtr->m_pToolbar,TRUE);

	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW, true);
	if (prefs_show)
	{
		gtk_widget_show(m_QuiverImplPtr->m_pToolbar);
	}
	action = QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_VIEW_TOOLBAR_MAIN);
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), prefs_show ? TRUE : FALSE);

	// FIXME: context menu
	// GtkWidget *context_menu = gtk_ui_manager_get_widget (m_QuiverImplPtr->m_pUIManager,"/ui/ContextMenu");
	
	gtk_widget_set_name(m_QuiverImplPtr->m_pToolbar  ,"Quiver m_QuiverImplPtr->m_pToolbar");
	

	// pack the browser and viewer area
	gtk_box_pack_start (GTK_BOX (hbox_browser_viewer_container), m_QuiverImplPtr->m_BrowserPtr->GetWidget(), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox_browser_viewer_container), m_QuiverImplPtr->m_ViewerPtr->GetWidget(), TRUE, TRUE, 0);

	// pack the hpaned (main gui area)
	gtk_paned_pack1(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea),hbox_browser_viewer_container,TRUE,TRUE);
	gtk_paned_pack2(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea),m_QuiverImplPtr->m_pNBProperties,FALSE,FALSE);

	int hpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_HPANE_POS, m_QuiverImplPtr->m_iAppWidth/2);
	gtk_paned_set_position(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea),hpaned_pos);

	// pack the main gui ara with the rest of the gui compoents
	//gtk_container_add (GTK_CONTAINER (vbox),menubar);
#ifdef QUIVER_MAEMO
	hildon_program_set_common_menu (program, GTK_MENU(m_QuiverImplPtr->m_pMenubar));
	hildon_program_set_common_toolbar (program, GTK_TOOLBAR(m_QuiverImplPtr->m_pToolbar));

	//gtk_widget_tap_and_hold_setup(m_QuiverImplPtr->m_pQuiverWindow, context_menu, NULL, GTK_TAP_AND_HOLD_NONE  ); 
#else
	gtk_box_pack_start (GTK_BOX (vbox), m_QuiverImplPtr->m_pMenubar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), m_QuiverImplPtr->m_pToolbar, FALSE, FALSE, 0);
#endif

	gtk_box_pack_start (GTK_BOX (vbox), m_QuiverImplPtr->m_pHPanedMainArea, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox),statusbar , FALSE, FALSE, 0);

	// add the gui elements to the main window
	gtk_container_add (GTK_CONTAINER (m_QuiverImplPtr->m_pQuiverWindow),vbox);


	/* Show the application window */
	
	gtk_container_set_border_width (GTK_CONTAINER(m_QuiverImplPtr->m_pQuiverWindow),0);
	
	/*
	 * FIXME
	gtk_drag_dest_set(m_pQuiverWindow,GTK_DEST_DEFAULT_ALL,
					quiver_drag_target_table, 3, (GdkDragAction)(GDK_ACTION_COPY|GDK_ACTION_MOVE));
	*/
/*
	g_signal_connect (G_OBJECT (m_pQuiverWindow), "drag_data_received",
				GTK_SIGNAL_FUNC (Quiver::signal_drag_data_received), this);


	gtk_drag_source_set (m_ViewerPtr->GetWidget(), (GdkModifierType)(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK),
			   quiver_drag_target_table, 3, (GdkDragAction)(
	//GDK_ACTION_COPY | GDK_ACTION_MOVE | 	GDK_ACTION_LINK));
*/

	/*
	gtk_drag_source_set_icon (m_Viewer, 
				gtk_widget_get_colormap (window),
				drag_icon, drag_mask);
	*/		
/*
	gtk_signal_connect (GTK_OBJECT (m_ViewerPtr->GetWidget()), "drag_data_get",
		      GTK_SIGNAL_FUNC (Quiver::signal_drag_data_get), this);
	gtk_signal_connect (GTK_OBJECT (m_ViewerPtr->GetWidget()), "drag_data_delete",
		      GTK_SIGNAL_FUNC (Quiver::signal_drag_data_delete), this);

	gtk_signal_connect (GTK_OBJECT (m_ViewerPtr->GetWidget()), "drag_begin",
	      GTK_SIGNAL_FUNC (Quiver::signal_drag_begin), this);
	
	gtk_signal_connect (GTK_OBJECT (m_ViewerPtr->GetWidget()), "drag_end",
	      GTK_SIGNAL_FUNC (Quiver::signal_drag_end), this);		  
*/
/*	
	g_signal_connect (G_OBJECT (m_pQuiverWindow), "drag_drop",
				G_CALLBACK (signal_drag_drop), this);
			

	g_signal_connect (G_OBJECT (m_pQuiverWindow), "drag_motion",
				G_CALLBACK (signal_drag_motion), this);
*/
					
	g_idle_add(idle_quiver_init,this);
	
	gtk_widget_show_all (GTK_WIDGET(m_QuiverImplPtr->m_pQuiverWindow));

	//test adding a custom item to the menu
	m_QuiverImplPtr->LoadExternalTools();

	m_QuiverImplPtr->LoadBookmarks();

	bool bStartFS =
	   prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_START_FULLSCREEN, false);

	if (bStartFS)
	{
		OnFullScreen();
	}


}


Quiver::~Quiver()
{
	//destructor
#ifdef QUIVER_MAEMO
	if (NULL != osso_context)
	{
		osso_deinitialize(osso_context);
	}
#endif
}

bool Quiver::LoadSettings()
{
	string gtk_rc = g_szConfigDir + string("/gtkrc");
	
	gtk_rc_parse (gtk_rc.c_str());

	string strAccelMap = g_szConfigDir + string("/quiver_keys.map");	
	
	gtk_accel_map_load(strAccelMap.c_str());

	PreferencesPtr prefsPtr = Preferences::GetInstance();

	m_QuiverImplPtr->m_iAppX      = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_LEFT, gdk_screen_width()/4);
	m_QuiverImplPtr->m_iAppY      = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOP, gdk_screen_height()/4);
	m_QuiverImplPtr->m_iAppWidth  = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WIDTH, gdk_screen_width()/2);
	m_QuiverImplPtr->m_iAppHeight = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_HEIGHT, gdk_screen_height()/2);
	
	return (m_QuiverImplPtr->m_iAppWidth && m_QuiverImplPtr->m_iAppHeight);
}

void Quiver::SaveSettings()
{
	//Timer t("Quiver::SaveSettings()");
	
	string directory = g_szConfigDir;
	
	string strAccelMap = directory + string("/quiver_keys.map");
	gtk_accel_map_save(strAccelMap.c_str());


	if (GDK_WINDOW_STATE_FULLSCREEN == m_QuiverImplPtr->m_WindowState)
	{
		return;
	}
	
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_LEFT,m_QuiverImplPtr->m_iAppX);
	prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOP,m_QuiverImplPtr->m_iAppY);
	prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WIDTH,m_QuiverImplPtr->m_iAppWidth);
	prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_HEIGHT,m_QuiverImplPtr->m_iAppHeight);
	prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_HPANE_POS,gtk_paned_get_position(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea)));
}


void Quiver::SetImageList(list<string> &files, bool bRecursive /* = false */)
{
	bool bShowViewer = false;
	if (1 == files.size())
	{
		bShowViewer = true;
		struct stat stat_struct = {0};
		if (0 == g_stat(files.front().c_str(),&stat_struct))
		{
			if (stat_struct.st_mode & S_IFDIR)
			{
				//browser
				bShowViewer = false;
			}
		}
		else
		{
			bShowViewer = false;
		}
	}
	
	if (bShowViewer)
	{
		ShowViewer();
	}
	else
	{
		// must do the following to hide the widget on initial
		// display of app
		ShowBrowser();

		m_QuiverImplPtr->m_BrowserPtr->GrabFocus();
	}

	m_QuiverImplPtr->m_ImageListPtr->SetImageList(&files, bRecursive);
}


static gboolean DestroyQuiver (gpointer data)
{
	Quiver *pQuiver = (Quiver*)data;
	delete pQuiver;
	return TRUE;
}

#ifdef QUIVER_MAEMO

static void
mime_open_handler (gpointer raw_data, int argc, char **argv)
{
	QuiverImpl* pQuiverImpl = static_cast<QuiverImpl*>(raw_data);
	gdk_threads_enter();
	if (argc > 0)
	{
		list<string> files;
		for (int i = 0;i<argc;i++)
		{	
			gchar* filename = g_filename_from_uri(argv[i],NULL,NULL);
			if (NULL != filename)
			{
				files.push_back(filename);
				g_free(filename);
			}
			else
			{
				files.push_back(argv[i]);
			}
		}

		if (pQuiverImpl->m_bInitialized)
		{
			// just set the image list
			pQuiverImpl->m_pQuiver->SetImageList(files);

			// and present the window (bring it to the front)
			gtk_window_present(GTK_WINDOW(pQuiverImpl->m_pQuiverWindow));
		}
		else
		{
			// not initialized yet, set the file list
			pQuiverImpl->m_listImages = files;
		}
	}
	gdk_threads_leave();
}


#endif
typedef struct _CreateQuiverData
{
	list<string> *pFiles;
	bool bRecursive;
} CreateQuiverData;

static gboolean CreateQuiver (gpointer data)
{
	CreateQuiverData* cqd = (CreateQuiverData*)data;
	QuiverStockIcons::Load();
	
	Quiver *pQuiver = new Quiver(*(cqd->pFiles), cqd->bRecursive);
	gtk_quit_add (0,DestroyQuiver, pQuiver);
	return TRUE;
}

int main (int argc, char **argv)
{
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

 	/* init threads */
	g_thread_init (NULL);
	gdk_threads_init ();
	gdk_threads_enter ();

	
	/* Initialize the widget set */
	gtk_init (&argc, &argv);

	gst_init(&argc, &argv);

	ThreadUtil::Init();


	// set up global variables
	// config directory
	gchar* szConfDir = g_build_filename(g_get_user_config_dir(),g_get_prgname(),NULL);
	strncpy(g_szConfigDir,szConfDir,255);
	g_free(szConfDir);

	// config file path
	gchar *szConfigFileName = g_strconcat(g_get_prgname(), ".ini", NULL);
	gchar *szConfigFilePath = g_build_filename(g_szConfigDir,szConfigFileName,NULL);
	strncpy(g_szConfigFilePath,szConfigFilePath,255);
	g_free(szConfigFileName);
	g_free(szConfigFilePath);
	
	// create config directory
	g_mkdir_with_parents(g_szConfigDir,S_IRUSR|S_IWUSR|S_IXUSR);

	list<string> files;
	for (int i =1;i<argc;i++)
	{	
		gchar* filename = g_filename_from_uri(argv[i],NULL,NULL);
		if (NULL != filename)
		{
			files.push_back(filename);
			g_free(filename);
		}
		else
		{
			files.push_back(argv[i]);
		}
	}

	CreateQuiverData cqd = {0};
	cqd.bRecursive = false;
	
	if (argc == 1)
	{	
		const gchar* dir;
		// default to a directory
		// specified in preferences
		PreferencesPtr prefsPtr = Preferences::GetInstance();
#ifdef QUIVER_MAEMO
		dir = "~/MyDocs/.images";
#else
		dir = g_get_home_dir();
#endif
		if (prefsPtr->HasKey(QUIVER_PREFS_APP, QUIVER_PREFS_APP_PHOTO_LIBRARY))
		{	
			string photo_library = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PHOTO_LIBRARY,dir);
			files.push_back(photo_library);
			GFile* file = g_file_new_for_path("/");
			GFile* photoLib = g_file_new_for_path(photo_library.c_str());

			if (!g_file_equal(file, photoLib))
			{
				// just in case the users sets the root
				// as the photo library
				cqd.bRecursive = true;
			}
			g_object_unref(photoLib);
			g_object_unref(file);
		}
		else
		{
			files.push_back(dir);
		}

	}
	//pthread_setconcurrency(4);

	cqd.pFiles = &files;
	gtk_init_add (CreateQuiver,&cqd);
	
	// FIX FOR BUG: http://bugzilla.gnome.org/show_bug.cgi?id=65041
	// race condition when registering types
	// we have many threads that create pixbuf loaders
	// so must do the following:
	
	//GdkPixbufNonAnim
	GdkPixbufAnimation* anim;
	anim = gdk_pixbuf_non_anim_new (NULL);
	g_object_unref(anim);
	

	GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
	gdk_pixbuf_loader_close(loader, NULL);
	g_object_unref(loader);
	
	// END BUG FIX items
                                             
	gtk_main ();

	gst_deinit();
	

	gdk_threads_leave();
	
	return 0;
}

gboolean Quiver::idle_quiver_init (gpointer data)
{
	return ((Quiver*)data)->IdleQuiverInit(data);
}
gboolean Quiver::IdleQuiverInit(gpointer data)
{
	gdk_threads_enter();
	// put process intenstive startup code in here 
	// (loading image list, setting first image)

	// set up the stock icons

	SetImageList(m_QuiverImplPtr->m_listImages, m_QuiverImplPtr->m_bListImagesRecursive);

	m_QuiverImplPtr->m_BrowserPtr->SetImageList(m_QuiverImplPtr->m_ImageListPtr);
	m_QuiverImplPtr->m_ViewerPtr->SetImageList(m_QuiverImplPtr->m_ImageListPtr);

	// call this a second time to make sure the list is updated
	if (0 == m_QuiverImplPtr->m_iMergedBrowserUI)
	{
		ShowViewer();
	}
	else
	{
		ShowBrowser();
	}

	TaskManagerDlg::Create(GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow));

	m_QuiverImplPtr->m_bInitialized = true;

	gdk_threads_leave();

	return FALSE; // return false so it is never called again
}


static gboolean timeout_event_motion_notify (gpointer data)
{
	QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
	if (GDK_WINDOW_STATE_FULLSCREEN & pQuiverImpl->m_WindowState)
	{
		gdk_threads_enter();
		
		GdkCursor *empty_cursor;
		GdkBitmap * empty_bitmap;
		char zero[] = { 0x0 };	
		GdkColor blank = { 0, 0, 0, 0 };	
	
		empty_bitmap = gdk_bitmap_create_from_data (NULL,zero,1,1);
		empty_cursor = gdk_cursor_new_from_pixmap (empty_bitmap,empty_bitmap,&blank,&blank,0,0);

#ifndef QUIVER_MAEMO
		gdk_window_set_cursor (pQuiverImpl->m_pQuiverWindow->window, empty_cursor);
#endif
		
		g_object_unref(empty_bitmap);
		gdk_cursor_unref (empty_cursor);
		
		//remove the mouse cursor		
		gdk_threads_leave();
	}
	pQuiverImpl->m_iTimeoutMouseMotionNotify = 0;
	return FALSE;
}


static gboolean event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data )
{
	QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
	if (0 != pQuiverImpl->m_iTimeoutMouseMotionNotify)
	{
		g_source_remove(pQuiverImpl->m_iTimeoutMouseMotionNotify);
		pQuiverImpl->m_iTimeoutMouseMotionNotify = 0;
	}

#ifndef QUIVER_MAEMO	
	gdk_window_set_cursor (pQuiverImpl->m_pQuiverWindow->window, NULL);
#endif

	pQuiverImpl->m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,pQuiverImpl);

	return FALSE;
}



//==============================================================================
//== gtk_ui_manager signals ====================================================
//==============================================================================

	// gtk_ui_manager signals
static void signal_connect_proxy (GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data)
{
	if (GTK_IS_ITEM(proxy))
	{
		g_signal_connect (proxy, "select",G_CALLBACK (signal_item_select), data);
		g_signal_connect (proxy, "deselect",G_CALLBACK (signal_item_deselect), data);
	}
}
static void signal_disconnect_proxy (GtkUIManager *manager,GtkAction *action,GtkWidget*proxy, gpointer data)
{
	/*
	g_signal_handlers_disconnect_by_func (proxy, G_CALLBACK (signal_item_select), data);
	g_signal_handlers_disconnect_by_func (proxy, G_CALLBACK (signal_item_deselect), data);
	*/
}

static void signal_item_select (GtkItem *proxy,gpointer data)
{
	QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
	GtkAction* action;
	char*      message;
	
	action = (GtkAction*)g_object_get_data (G_OBJECT (proxy),  "gtk-action");
	g_return_if_fail (action != NULL);
	
	g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
	if (message)
	{
		pQuiverImpl->m_StatusbarPtr->PushText(message);
		g_free (message);
	}

}

static void signal_item_deselect (GtkItem *proxy,gpointer data)
{
	QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
	GtkAction* action;
	char*      message;
	
	action = (GtkAction*)g_object_get_data (G_OBJECT (proxy),  "gtk-action");
	g_return_if_fail (action != NULL);
	
	g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
	if (message)
	{
		pQuiverImpl->m_StatusbarPtr->PopText();
		g_free (message);
	}

}


void Quiver::ShowViewer()
{
	m_QuiverImplPtr->m_BrowserPtr->Hide();

	if (0 != m_QuiverImplPtr->m_iMergedBrowserUI)
	{
		gtk_ui_manager_remove_ui(m_QuiverImplPtr->m_pUIManager,m_QuiverImplPtr->m_iMergedBrowserUI);
		m_QuiverImplPtr->m_iMergedBrowserUI = 0;
		gtk_ui_manager_ensure_update(m_QuiverImplPtr->m_pUIManager);
	}

	if (0 == m_QuiverImplPtr->m_iMergedViewerUI)
	{
		m_QuiverImplPtr->m_iMergedViewerUI = gtk_ui_manager_add_ui_from_string(m_QuiverImplPtr->m_pUIManager,
			quiver_ui_viewer,
			strlen(quiver_ui_viewer),
			NULL);
		gtk_ui_manager_ensure_update(m_QuiverImplPtr->m_pUIManager);
	}

	m_QuiverImplPtr->m_ViewerPtr->Show();
	m_QuiverImplPtr->m_ViewerPtr->GrabFocus();
}

void Quiver::ShowBrowser()
{
	m_QuiverImplPtr->m_ViewerPtr->Hide();
	gtk_ui_manager_ensure_update(m_QuiverImplPtr->m_pUIManager);

	if (0 != m_QuiverImplPtr->m_iMergedViewerUI)
	{
		gtk_ui_manager_remove_ui(m_QuiverImplPtr->m_pUIManager,m_QuiverImplPtr->m_iMergedViewerUI);
		m_QuiverImplPtr->m_iMergedViewerUI = 0;
		gtk_ui_manager_ensure_update(m_QuiverImplPtr->m_pUIManager);
	}

	if (0 == m_QuiverImplPtr->m_iMergedBrowserUI)
	{
		m_QuiverImplPtr->m_iMergedBrowserUI = gtk_ui_manager_add_ui_from_string(m_QuiverImplPtr->m_pUIManager,
			quiver_ui_browser,
			strlen(quiver_ui_browser),
			NULL);
		gtk_ui_manager_ensure_update(m_QuiverImplPtr->m_pUIManager);
	}
	m_QuiverImplPtr->m_BrowserPtr->Show();
	m_QuiverImplPtr->m_BrowserPtr->GrabFocus();
}


void Quiver::OnAbout()
{
	const char * authors[] = {"mike morrison <mike_morrison@alumni.uvic.ca>",NULL};
	const char * artists[] = {"mike morrison <mike_morrison@alumni.uvic.ca>",NULL};
	const char * documenters[] = {"mike morrison <mike_morrison@alumni.uvic.ca>",NULL};
	gtk_show_about_dialog(GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow),
		"name",GETTEXT_PACKAGE,
		"version",PACKAGE_VERSION,
		"copyright","copyright (c) 2008\nmike morrison",
		"comments","a gtk image viewer",
		"authors",authors,
		"artists",artists,
		"documenters",documenters,
		"website",PACKAGE_BUGREPORT,
		"website-label","quiver website",
		NULL);
}


void Quiver::OnFullScreen()
{
	if (GDK_WINDOW_STATE_FULLSCREEN & m_QuiverImplPtr->m_WindowState)
	{
		gtk_window_unfullscreen(GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow));
	}
	else
	{
		// timeout to hide mouse cursor
		gtk_window_fullscreen(GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow));
	}
}

void QuiverImpl::UpdateUI()
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	GtkAction * action = QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_SLIDESHOW);
	bool bInSlideShow = false;

	if (NULL != action)
	{
		bInSlideShow =
			(TRUE == gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
	}

	if (bInSlideShow)
	{
		action 
			= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_MENUBAR);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), FALSE);

		action 
			= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_TOOLBAR_MAIN);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), FALSE);

		action 
			= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_STATUSBAR);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), FALSE);

		action 
			= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_PROPERTIES);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), FALSE);

		/*
		gtk_widget_hide(m_pToolbar);
		gtk_widget_hide(m_pMenubar);
		gtk_widget_hide(m_StatusbarPtr->GetWidget());
		gtk_widget_hide(m_pNBProperties);
		*/
	}
	else
	{
		bool bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PROPS_SHOW);
		if (bShow)
		{
			action 
				= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_PROPERTIES);
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), TRUE);
		}

		if (GDK_WINDOW_STATE_FULLSCREEN & m_WindowState)
		{
			bool bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW_FS, false);
			action 
				= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_TOOLBAR_MAIN);
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShow ? TRUE : FALSE);

			bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW_FS, false);
			action 
				= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_MENUBAR);
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShow ? TRUE : FALSE);
			
			bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW_FS, false);
			action 
				= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_STATUSBAR);
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShow ? TRUE : FALSE);
		}
		else
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN, false);
			m_bSlideShowRestoreFromFS = false;
			// show widgets
			bool bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW);
			action 
				= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_TOOLBAR_MAIN);
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShow ? TRUE : FALSE);
			
			bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW);
			action 
				= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_MENUBAR);
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShow ? TRUE : FALSE);
			
			bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW);
			action 
				= QuiverUtils::GetAction(m_pUIManager,ACTION_QUIVER_VIEW_STATUSBAR);
			gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), bShow ? TRUE : FALSE);
		}
	}
}


void QuiverImpl::BrowserEventHandler::HandleSelectionChanged(BrowserEventPtr event_ptr)
{
	list<unsigned int> selection = parent->m_BrowserPtr->GetSelection();
	list<unsigned int>::iterator itr;
	
	unsigned long long total_size = 0;
	
	// n items selected (xx kb)
	for (itr = selection.begin(); selection.end() != itr; ++itr)
	{
		if (*itr < parent->m_ImageListPtr->GetSize())
		{
			QuiverFile f = (*parent->m_ImageListPtr)[*itr];
			total_size += f.GetFileSize();
		}
	}
	
	char status_text[256];
	g_snprintf(status_text, 256, "%d items selected (%qd bytes)",selection.size(), total_size);

	//parent->m_StatusbarPtr->SetText(status_text);
}

void QuiverImpl::BrowserEventHandler::HandleItemActivated(BrowserEventPtr event_ptr)
{
	parent->m_pQuiver->ShowViewer();
}

void QuiverImpl::BrowserEventHandler::HandleCursorChanged(BrowserEventPtr event_ptr)
{
	parent->m_pQuiver->ImageChanged();
}

void QuiverImpl::ViewerEventHandler::HandleItemActivated(ViewerEventPtr event_ptr)
{
	parent->m_pQuiver->ShowBrowser();
}

void QuiverImpl::ViewerEventHandler::HandleItemClicked(ViewerEventPtr event_ptr)
{
	parent->m_pQuiver->OnFullScreen();
}

void QuiverImpl::ViewerEventHandler::HandleCursorChanged(ViewerEventPtr event_ptr)
{
	parent->m_pQuiver->ImageChanged();
}

static gboolean timeout_keep_screen_on (gpointer data)
{
#ifdef QUIVER_MAEMO 
	if (NULL != osso_context)
	{
		osso_display_blanking_pause(osso_context);
	}
#endif
	return TRUE;
}

void QuiverImpl::ViewerEventHandler::HandleSlideShowStarted(ViewerEventPtr event_ptr)
{
	PreferencesPtr prefs = Preferences::GetInstance();
	
	bool bFS = (gboolean)prefs->GetBoolean(QUIVER_PREFS_SLIDESHOW, QUIVER_PREFS_SLIDESHOW_FULLSCREEN, TRUE);
	if (bFS)
	{
		if ( !(GDK_WINDOW_STATE_FULLSCREEN & parent->m_WindowState) )
		{
			parent->m_bSlideShowRestoreFromFS = true;
			parent->m_pQuiver->OnFullScreen();
		}
	}

	parent->UpdateUI();
	
	// start a timer to keep the display on
	if (0 == parent->m_iTimeoutKeepScreenOn)
	{
#ifdef QUIVER_MAEMO 
		parent->m_iTimeoutKeepScreenOn = g_timeout_add(5000, timeout_keep_screen_on, parent);
#endif
	}
}

void QuiverImpl::ViewerEventHandler::HandleSlideShowStopped(ViewerEventPtr event_ptr)
{
	// return from FS if necessary
	if (parent->m_bSlideShowRestoreFromFS)
	{
		parent->m_pQuiver->OnFullScreen();
	}

	GtkAction * action = QuiverUtils::GetAction(parent->m_pUIManager,ACTION_QUIVER_SLIDESHOW);
	if (NULL != action)
	{
		g_signal_handlers_block_matched (action,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,this);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),FALSE);
		g_signal_handlers_unblock_matched (action,G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,this);
	}

	parent->UpdateUI();

	// stop the timer that keeps the display on
	if (0 != parent->m_iTimeoutKeepScreenOn)
	{
		g_source_remove(parent->m_iTimeoutKeepScreenOn);
		parent->m_iTimeoutKeepScreenOn = 0;
	}
}


void QuiverImpl::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event)
{

}

void QuiverImpl::BookmarksEventHandler::HandleBookmarkChanged(BookmarksEventPtr event)
{
	parent->LoadBookmarks();
}

void QuiverImpl::ExternalToolsEventHandler::HandleExternalToolChanged(ExternalToolsEventPtr event)
{
	parent->LoadExternalTools();
}

void Quiver::OnShowProperties(bool bShow /* = true */)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PROPS_SHOW, bShow);
	
	if (bShow)
	{
		gtk_widget_show(m_QuiverImplPtr->m_pNBProperties);
	}
	else
	{
		gtk_widget_hide(m_QuiverImplPtr->m_pNBProperties);
	}
}

void Quiver::OnQuit()
{
	if (m_QuiverImplPtr->CanClose())
	{
		Close();
	}
}

void Quiver::OnOpenFile()
{
#ifndef QUIVER_MAEMO
	GtkWidget *dialog;
	
	dialog = gtk_file_chooser_dialog_new ("Open File",
					      GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow),
					      GTK_FILE_CHOOSER_ACTION_OPEN,
					      QUIVER_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					      QUIVER_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					      NULL);
	
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	  {
	    char *filename;
	
	    filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
	    list<string> file_list;
	    file_list.push_back(filename);
	    m_QuiverImplPtr->m_ImageListPtr->SetImageList(&file_list);
	    //open_file (filename);
	    g_free (filename);
	  }
	
	gtk_widget_destroy (dialog);
#endif
}

void Quiver::OnOpenFolder()
{
	
#ifndef QUIVER_MAEMO
	GtkWidget *dialog;
	dialog = gtk_file_chooser_dialog_new ("Open Folder",
					      GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow),
					      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
					      QUIVER_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					      QUIVER_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					      NULL);
	
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	  {
	    char *filename;
	
	    filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
	    list<string> file_list;
	    file_list.push_back(filename);
	    m_QuiverImplPtr->m_ImageListPtr->SetImageList(&file_list);
	    g_free (filename);
	  }
	
	gtk_widget_destroy (dialog);
#endif
}

void Quiver::OnSlideShow(bool bStart)
{
	if( bStart )
	{
		PreferencesPtr prefsPtr = Preferences::GetInstance();
		bool bRandomOrder = 
			prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_RANDOM_ORDER,false);

		if (bRandomOrder)
		{
			GtkAction* action = 
				QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_SORT_BY_NAME_NATURAL);
			if (action)
			{
				gtk_radio_action_set_current_value (GTK_RADIO_ACTION(action), ImageList::SORT_BY_RANDOM);
				m_QuiverImplPtr->m_ImageListPtr->SetCurrentIndex(0);
			}
		}

		ShowViewer();
		m_QuiverImplPtr->m_ViewerPtr->SlideShowStart();
	}
	else
	{
		PreferencesPtr prefsPtr = Preferences::GetInstance();
		GtkAction* action = QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_SORT_BY_NAME_NATURAL);
		if (action)
		{
			int sortby = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_SORT_BY,ImageList::SORT_BY_FILENAME_NATURAL);
			gtk_radio_action_set_current_value (GTK_RADIO_ACTION(action), sortby);
		}

		m_QuiverImplPtr->m_ViewerPtr->SlideShowStop();
	}
}

void Quiver::OnShowToolbar(bool bShow)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();

	GtkAction * action = QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_SLIDESHOW);
	bool bInSlideShow = false;
	if (NULL != action)
	{
		bInSlideShow =
			(TRUE == gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
	}

	if (!bInSlideShow)
	{
		if (GDK_WINDOW_STATE_FULLSCREEN & m_QuiverImplPtr->m_WindowState)
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW_FS, bShow);
		}
		else
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW, bShow);
		}
	}

	if (bShow)
	{
		gtk_widget_show(m_QuiverImplPtr->m_pToolbar);
	}
	else
	{
		gtk_widget_hide(m_QuiverImplPtr->m_pToolbar);
	}
}

void Quiver::OnShowStatusbar(bool bShow)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	GtkAction * action = QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_SLIDESHOW);
	bool bInSlideShow = false;
	if (NULL != action)
	{
		bInSlideShow =
			(TRUE == gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
	}

	if (!bInSlideShow)
	{
		if (GDK_WINDOW_STATE_FULLSCREEN & m_QuiverImplPtr->m_WindowState)
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW_FS, bShow);
		}
		else
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW, bShow);
		}
	}

	if (bShow)
	{
		gtk_widget_show(m_QuiverImplPtr->m_StatusbarPtr->GetWidget());
	}
	else
	{
		gtk_widget_hide(m_QuiverImplPtr->m_StatusbarPtr->GetWidget());
	}
}

void Quiver::OnShowMenubar(bool bShow)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();

	GtkAction * action = QuiverUtils::GetAction(m_QuiverImplPtr->m_pUIManager,ACTION_QUIVER_SLIDESHOW);
	bool bInSlideShow = false;
	if (NULL != action)
	{
		bInSlideShow =
			(TRUE == gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
	}

	if (!bInSlideShow)
	{
		if (GDK_WINDOW_STATE_FULLSCREEN & m_QuiverImplPtr->m_WindowState)
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW_FS, bShow);
		}
		else
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW, bShow);
		}
	}

	if (bShow)
	{
		gtk_widget_show(m_QuiverImplPtr->m_pMenubar);
	}
	else
	{
		gtk_widget_hide(m_QuiverImplPtr->m_pMenubar);
	}
}

static void
quiver_radio_action_handler_cb(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data)
{
//	printf("%s: %s\n",gtk_action_get_name(GTK_ACTION(action)),gtk_action_get_name(GTK_ACTION(current)));
	quiver_action_handler_cb(GTK_ACTION(action), user_data);
}

static void quiver_action_handler_cb(GtkAction *action, gpointer data)
{
	QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
	Quiver *pQuiver;
	pQuiver = pQuiverImpl->m_pQuiver;
	
	const gchar * szAction = gtk_action_get_name(action);

	//printf("quiver_action_handler_cb: %s\n",szAction);

	if (0 == strcmp(szAction,ACTION_QUIVER_CLOSE) 
	    || 0 == strcmp(szAction,ACTION_QUIVER_CLOSE_2) 
	    || 0 == strcmp(szAction,ACTION_QUIVER_CLOSE_3) 
	    || 0 == strcmp(szAction,ACTION_QUIVER_CLOSE_4))
	{
		pQuiver->OnQuit();
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_ESCAPE) )
	{
		// this action will do one of the following

		bool bDoneSomething = false;

		if (GDK_WINDOW_STATE_FULLSCREEN & pQuiverImpl->m_WindowState)
		{
			// 3. if in browser and fullscreen, return to unfullscreen mode
			pQuiver->OnFullScreen();
			bDoneSomething = true;
		}

		if (!bDoneSomething && 0 != pQuiverImpl->m_iMergedViewerUI)
		{
			// 1. if in viewer and zoomed return to zoom fit
			bDoneSomething = pQuiverImpl->m_ViewerPtr->ResetViewMode();

			if (!bDoneSomething)
			{
				// 2. if in viewer and zoom fit return to browser
				pQuiver->ShowBrowser();
				bDoneSomething = true;
			}
		}


	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_OPEN))
	{
		pQuiver->OnOpenFile();
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_OPEN_FOLDER))
	{
		pQuiver->OnOpenFolder();
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_VIEW_PROPERTIES))
	{
		if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
		{
			pQuiver->OnShowProperties(true);	
		}
		else
		{
			pQuiver->OnShowProperties(false);
		}
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_VIEW_TOOLBAR_MAIN))
	{
		if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
		{
			pQuiver->OnShowToolbar(true);	
		}
		else
		{
			pQuiver->OnShowToolbar(false);
		}
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_VIEW_MENUBAR))
	{
		if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
		{
			pQuiver->OnShowMenubar(true);	
		}
		else
		{
			pQuiver->OnShowMenubar(false);
		}
	}
	else if (0 == strcmp(szAction, ACTION_QUIVER_VIEW_STATUSBAR))
	{
		if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
		{
			pQuiver->OnShowStatusbar(true);	
		}
		else
		{
			pQuiver->OnShowStatusbar(false);
		}
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_FULLSCREEN)
#ifdef QUIVER_MAEMO
	 || 0 == strcmp(szAction,ACTION_QUIVER_FULLSCREEN_MAEMO)
#endif
	)
	{
		pQuiver->OnFullScreen();
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_UI_MODE_BROWSER))
	{
		pQuiver->ShowBrowser();
	}		
	else if (0 == strcmp(szAction,ACTION_QUIVER_UI_MODE_VIEWER))
	{
		pQuiver->ShowViewer();
	}
#ifdef QUIVER_MAEMO
	else if (0 == strcmp(szAction,ACTION_QUIVER_UI_MODE_SWITCH_MAEMO))
	{
		if (0 == pQuiverImpl->m_iMergedViewerUI)
		{
			pQuiver->ShowViewer();
		}
		else
		{
			if (! pQuiverImpl->m_ViewerPtr->ResetViewMode() )
			{
				pQuiver->ShowBrowser();
			}
		}
	}
#endif
	else if (0 == strcmp(szAction, ACTION_QUIVER_DONATE))
	{
		DonateDlg dlg;
		dlg.Run();
	}
	else if (0 == strcmp(szAction, ACTION_QUIVER_ABOUT))
	{
		pQuiver->OnAbout();
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_SLIDESHOW))
	{
		pQuiver->OnSlideShow(TRUE == gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_PREFERENCES))
	{
		//
		PreferencesDlg prefDlg;
		prefDlg.Run();
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_SAVE))
	{
		pQuiverImpl->Save();
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_SAVE_AS))
	{
		pQuiverImpl->SaveAs();	
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_SORT_BY_NAME))
	{
		GtkAction *sort_desc_action = QuiverUtils::GetAction(pQuiverImpl->m_pUIManager,ACTION_QUIVER_SORT_DESCENDING);
		bool bAsc = ( FALSE == gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(sort_desc_action)) );
		
		gint sortby = gtk_radio_action_get_current_value(GTK_RADIO_ACTION(action));
		pQuiverImpl->m_ImageListPtr->Sort((ImageList::SortBy)sortby,bAsc);

		GtkAction * slideshow_action = QuiverUtils::GetAction(pQuiverImpl->m_pUIManager,ACTION_QUIVER_SLIDESHOW);
		bool bInSlideShow = false;
		if (NULL != slideshow_action)
		{
			bInSlideShow =
				(TRUE == gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(slideshow_action)));
		}

		if (!bInSlideShow)
		{
			PreferencesPtr prefsPtr = Preferences::GetInstance();
			prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_SORT_BY,sortby);
		}

	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_SORT_DESCENDING))
	{
		// should just reverse the list
		pQuiverImpl->m_ImageListPtr->Reverse();
		PreferencesPtr prefsPtr = Preferences::GetInstance();

		GtkAction *sort_desc_action = QuiverUtils::GetAction(pQuiverImpl->m_pUIManager,ACTION_QUIVER_SORT_DESCENDING);

		bool bDec = ( TRUE == gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(sort_desc_action)) );
		prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_SORT_REVERSED,bDec);
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_RENAME))
	{
		RenameDlg dlg;
		list<string> folders;
		folders = pQuiverImpl->m_ImageListPtr->GetFolderList();

		if (!folders.empty())
		{
			dlg.SetInputFolder(folders.front());
		}

		if (dlg.Run())
		{
			// organize pictures dialog
			RenameTaskPtr renameTaskPtr(new RenameTask());
			renameTaskPtr->SetInputFolder( dlg.GetInputFolder() );
			renameTaskPtr->SetOutputFolder( dlg.GetOutputFolder() );
			renameTaskPtr->SetTemplate( dlg.GetTemplate() );
			renameTaskPtr->SetIncludeSubfolders( dlg.GetIncludeSubfolders() );

			/*
			std::list<unsigned int> items = pQuiverImpl->m_BrowserPtr->GetSelection();
			std::list<unsigned int>::iterator itr;
			for (itr = items.begin(); items.end() != itr; ++itr)
			{
				QuiverFile f = (*pQuiverImpl->m_ImageListPtr)[*itr];
				renameTaskPtr->AddFile(f);	
			}				
			*/

			TaskManager::GetInstance()->AddTask(renameTaskPtr);
		}

	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_ORGANIZE))
	{
		OrganizeDlg dlg;

		list<string> folders;
		folders = pQuiverImpl->m_ImageListPtr->GetFolderList();

		if (!folders.empty())
		{
			dlg.SetInputFolder(folders.front());
		}

		if (dlg.Run())
		{
			// organize pictures dialog
			OrganizeTaskPtr organizeTaskPtr(new OrganizeTask());
			organizeTaskPtr->SetInputFolder( dlg.GetInputFolder() );
			organizeTaskPtr->SetOutputFolder( dlg.GetOutputFolder() );
			organizeTaskPtr->SetDateTemplate( dlg.GetDateTemplate() );
			organizeTaskPtr->SetAppendedText( dlg.GetAppendedText() );
			organizeTaskPtr->SetDayExtension( dlg.GetDayExtention() );
			organizeTaskPtr->SetIncludeSubfolders( dlg.GetIncludeSubfolders() );

			/*
			std::list<unsigned int> items = pQuiverImpl->m_BrowserPtr->GetSelection();
			std::list<unsigned int>::iterator itr;
			for (itr = items.begin(); items.end() != itr; ++itr)
			{
				QuiverFile f = (*pQuiverImpl->m_ImageListPtr)[*itr];
				organizeTaskPtr->AddFile(f);	
			}				
			*/

			TaskManager::GetInstance()->AddTask(organizeTaskPtr);
		}
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_ADJUST_DATE))
	{
		AdjustDateDlg dlg;
		if (dlg.Run())
		{
			if (dlg.IsAdjustDate())
			{
				AdjustDateTaskPtr adjustDateTaskPtr(new AdjustDateTask(
								dlg.GetAdjustmentYears(),
								dlg.GetAdjustmentDays(),
								dlg.GetAdjustmentHours(),
								dlg.GetAdjustmentMinutes(),
								dlg.GetAdjustmentSeconds()));

				if (dlg.ModifyExifDate())
				{
					adjustDateTaskPtr->AddAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME);
				}

				if (dlg.ModifyExifDateOrig())
				{
					adjustDateTaskPtr->AddAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME_ORIG);
				}
				if (dlg.ModifyExifDateDig())
				{
					adjustDateTaskPtr->AddAdjustDateFields(AdjustDateTask::DATE_FIELD_EXIF_DATE_TIME_DIGITIZED);
				}

				// adjust exif date
				std::list<unsigned int> items = pQuiverImpl->m_BrowserPtr->GetSelection();
				std::list<unsigned int>::iterator itr;
				for (itr = items.begin(); items.end() != itr; ++itr)
				{
					QuiverFile f = (*pQuiverImpl->m_ImageListPtr)[*itr];
					adjustDateTaskPtr->AddFile(f);	
				}				

				TaskManager::GetInstance()->AddTask(adjustDateTaskPtr);
			}
			else if (dlg.IsSetDate())
			{
			}
		}
	}
	else if (g_str_has_prefix(szAction,"Bookmark_"))
	{
		const gchar* strid = szAction + strlen("Bookmark_");
		int id;

		stringstream ss;
		ss << strid;
		ss >> id;
		BookmarksPtr bookmarksPtr = pQuiverImpl->m_BookmarksPtr;
		const Bookmark* b = bookmarksPtr->GetBookmark(id);
		if (NULL != b)
		{
			list<string> uris = b->GetURIs();
			pQuiverImpl->m_ImageListPtr->SetImageList(&uris, b->GetRecursive());
		}
	}
	else if (g_str_has_prefix(szAction,"ExternalTool_"))
	{
		const gchar* strid = szAction + strlen("ExternalTool_");
		int id;

		stringstream ss;
		ss << strid;
		ss >> id;

		// run external tool
		const ExternalTool* extTool = pQuiverImpl->m_ExternalToolsPtr->GetExternalTool( id );

		if (NULL != extTool)
		{

			list<unsigned int> selection = pQuiverImpl->m_BrowserPtr->GetSelection();
			list<string> files;

			bool bInViewer = (0 != pQuiverImpl->m_iMergedViewerUI);
			if (bInViewer || 1 == selection.size())
			{
				QuiverFile f;
				if (bInViewer)
					f = pQuiverImpl->m_ImageListPtr->GetCurrent();
				else
					f = (*pQuiverImpl->m_ImageListPtr)[selection.front()];
				
				string file, directory;

				file = f.GetFilePath();
				files.push_back(file);
			}
			else if (1 < selection.size())
			{
				list<unsigned int>::iterator itr;
				for (itr = selection.begin(); selection.end() != itr; ++itr)
				{
					files.push_back((*pQuiverImpl->m_ImageListPtr)[*itr].GetFilePath());
				}
			}
			
			list<string> commands;

			if (extTool->GetSupportsMultiple())
			{
				string str_files;
				string str_dirs;

				list<string>::iterator itr;
				for (itr = files.begin(); files.end() != itr; ++itr)
				{
					str_files += "\"" + *itr + "\" "; 
				}

				for (itr = files.begin(); files.end() != itr; ++itr)
				{
					gchar *szDir = g_path_get_dirname ((*itr).c_str());
					str_dirs += "\"" + string(szDir) + "\" "; 
					g_free(szDir);
				}
				string cmd = extTool->GetCmd();
				boost::replace_all(cmd,"%f", str_files);
				boost::replace_all(cmd,"%d", str_dirs);

				commands.push_back(cmd);
			}
			else
			{
				list<string>::iterator itr;
				for (itr = files.begin(); files.end() != itr; ++itr)
				{
					string file, directory, cmd;
					file = *itr;
					cmd = extTool->GetCmd();
					
					gchar *szDir = g_path_get_dirname (file.c_str());
					directory = szDir;
					g_free(szDir);
					
					boost::replace_all(cmd,"%f", "\"" + file + "\"");
					boost::replace_all(cmd,"%d", "\"" + directory + "\"");
					
					commands.push_back(cmd);			
				}
			}

			list<string>::iterator itr;
			for (itr = commands.begin(); commands.end() != itr; ++itr)
			{
				string cmd = *itr; 
				printf("Running external command: %s\n", cmd.c_str());
				GError *error = NULL;
				g_spawn_command_line_async (cmd.c_str(), &error);
				if (NULL != error)
				{
					g_warning("%s\n", error->message);
					g_error_free(error);
				}
			}
		}
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_EXTERNAL_TOOLS))
	{
		ExternalToolsDlg externalToolsDlg;
		externalToolsDlg.Run();
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_BOOKMARKS_ADD))
	{
		list<string> folders, files;
		folders = pQuiverImpl->m_ImageListPtr->GetFolderList();
		files = pQuiverImpl->m_ImageListPtr->GetFileList();
		folders.insert(folders.end(), files.begin(), files.end());

		string strBaseName;
		gchar * bookmark_name = NULL;

		if (0 == folders.size())
		{
			// no bookmark to add
		}	
		else
		{
			gchar* filename = g_filename_from_uri(folders.front().c_str(),NULL,NULL);

			if (NULL != filename)
			{
				gchar* basename = g_path_get_basename(filename);
				if (NULL != basename)
				{
					strBaseName = basename;
					g_free(basename);
				}
				g_free(filename);
			}

			if ( 1 == folders.size() )
			{
				bookmark_name = g_strdup(strBaseName.c_str());
			}
			else
			{
				if ( 2 < folders.size() )
				{
					bookmark_name = g_strdup_printf("%s (and %d other folders)", strBaseName.c_str(), folders.size()-1);
				}
				else
				{
					bookmark_name = g_strdup_printf("%s (and 1 other folder)", strBaseName.c_str());
				}
			}
		}

		if (NULL != bookmark_name)
		{
			Bookmark b(bookmark_name, "","",folders,false);
			BookmarkAddEditDlg dlg(b);

			dlg.Run();
			if (!dlg.Cancelled())
			{
				Bookmark newbm = dlg.GetBookmark();
				if (!newbm.GetName().empty())
				{
					pQuiverImpl->m_BookmarksPtr->AddBookmark(newbm);
				}
			}
			g_free(bookmark_name);
		}

	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_BOOKMARKS_EDIT))
	{
		BookmarksDlg bookmarkDlg;
		bookmarkDlg.Run();
	}
}


static gboolean quiver_window_button_press ( GtkWidget *widget, GdkEventButton *event, gpointer data )
{
	// don't do anything for MAEMO because of weirdness in HildonControlbar
#ifndef QUIVER_MAEMO
	if (2 == event->button)
	{
		QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
		pQuiverImpl->m_pQuiver->OnFullScreen();
		return TRUE;
	}
#endif

	return FALSE;
}




void QuiverImpl::ImageListEventHandler::HandleContentsChanged(ImageListEventPtr event)
{
	parent->m_pQuiver->ImageChanged();
}
void QuiverImpl::ImageListEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event) 
{
	parent->m_pQuiver->ImageChanged();
}
void QuiverImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr event)
{
	parent->m_pQuiver->ImageChanged();
}
void QuiverImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr event)
{
	parent->m_pQuiver->ImageChanged();
}
void QuiverImpl::ImageListEventHandler::HandleItemChanged(ImageListEventPtr event)
{
	parent->m_pQuiver->ImageChanged();
}

