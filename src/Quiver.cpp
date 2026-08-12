#include <config.h>

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

#include <boost/algorithm/string.hpp>
#include "quiver-i18n.h"


// globals needed for preferences

gchar g_szConfigDir[256]      = "";
gchar g_szConfigFilePath[256] = "";

#ifdef QUIVER_MAEMO
osso_context_t* osso_context  = NULL;
#endif

using namespace std;

// helper functions


static void quiver_new_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer data);
static void quiver_escape_action(QuiverImpl *pQuiverImpl);
static gboolean quiver_window_button_press ( GtkWidget *widget, GdkEventButton *event, gpointer data );

#ifdef QUIVER_MAEMO
static void notify_gtk_enable_accels_changed (GObject *gobject, GParamSpec *arg1, gpointer user_data);
#endif
static gboolean event_window_state( GtkWidget *widget, GdkEventWindowState *event, gpointer data );
static gboolean timeout_event_motion_notify (gpointer data);
static gboolean event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data );


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
	
	static void ShowViewerUIItems(QuiverImpl *pQuiverImpl, bool bShow);
	static void ShowBrowserUIItems(QuiverImpl *pQuiverImpl, bool bShow);

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

	static const GtkTargetEntry quiver_drag_target_table[];
	// drag/drop targets
	enum {
    	QUIVER_TARGET_STRING,
		QUIVER_TARGET_URI
  	};

	bool m_bListImagesRecursive;
	std::list<std::string> m_listImages;

	GtkBuilder *m_pBuilder;
	bool m_bViewerMode;

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
	
	GtkWidget *m_pMenuBookmarkItems;
	GtkWidget *m_pMenuToolsExternal;
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
	m_pBuilder = NULL;
	m_bViewerMode = false;
	
	m_BookmarksPtr = Bookmarks::GetInstance();
	m_BookmarksPtr->AddEventHandler(m_BookmarksEventHandler);

	m_ExternalToolsPtr = ExternalTools::GetInstance();
	m_ExternalToolsPtr->AddEventHandler(m_ExternalToolsEventHandler);

	m_ImageListPtr->AddEventHandler(m_ImageListEventHandler);	

	m_pMenuBookmarkItems = NULL;
	m_pMenuToolsExternal = NULL;

	// add ignored extensions
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	std::list<std::string> exts = prefsPtr->GetStringList(QUIVER_PREFS_APP, QUIVER_PREFS_APP_IGNORED_EXTENSIONS);
	for (std::list<std::string>::iterator itr = exts.begin();
			exts.end() != itr; ++itr)
	{
		printf("ignoring extension : %s\n", itr->c_str());
		m_ImageListPtr->AddIgnoredExtension(*itr);
	}
}
QuiverImpl::~QuiverImpl()
{
	m_BookmarksPtr->RemoveEventHandler(m_BookmarksEventHandler);
	m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandler);	

	m_ImageListPtr.reset();
	m_BrowserPtr.reset();
	m_ViewerPtr.reset();
	m_StatusbarPtr.reset();

	gtk_widget_destroy(m_pQuiverWindow);
}

void QuiverImpl::LoadBookmarks()
{
	if (NULL == m_pMenuBookmarkItems)
	{
		return;
	}

	// remove previously added bookmark menu items (anything after the separator)
	GList *children = gtk_container_get_children(GTK_CONTAINER(m_pMenuBookmarkItems));
	gboolean bPastSeparator = FALSE;
	for (GList *itr = children; NULL != itr; itr = g_list_next(itr))
	{
		GtkWidget *child = GTK_WIDGET(itr->data);
		if (bPastSeparator)
		{
			gtk_widget_destroy(child);
		}
		else if (GTK_IS_SEPARATOR_MENU_ITEM(child))
		{
			bPastSeparator = TRUE;
		}
	}
	g_list_free(children);

	vector<Bookmark> bookmarks = m_BookmarksPtr->GetBookmarks();
	for (unsigned int i = 0; i < bookmarks.size(); ++i)
	{
		// this should be unique, something like Bookmark_category_name
		stringstream ss;
		ss << "Bookmark_" << bookmarks[i].GetID();
		string name = ss.str();

		QuiverUtils::RemoveAction(name.c_str());
		QuiverUtils::AddSimpleAction(name.c_str(), "", quiver_new_action_handler_cb, this);

		string full_name = "quiver." + name;
		GtkWidget *item = gtk_menu_item_new_with_label(bookmarks[i].GetName().c_str());
		gtk_actionable_set_action_name(GTK_ACTIONABLE(item), full_name.c_str());
		gtk_widget_show(item);
		gtk_menu_shell_append(GTK_MENU_SHELL(m_pMenuBookmarkItems), item);
	}
}

void QuiverImpl::LoadExternalTools()
{
	if (NULL == m_pMenuToolsExternal)
	{
		return;
	}

	// remove previously added external tool menu items (anything after the separator)
	GList *children = gtk_container_get_children(GTK_CONTAINER(m_pMenuToolsExternal));
	gboolean bPastSeparator = FALSE;
	for (GList *itr = children; NULL != itr; itr = g_list_next(itr))
	{
		GtkWidget *child = GTK_WIDGET(itr->data);
		if (bPastSeparator)
		{
			gtk_widget_destroy(child);
		}
		else if (GTK_IS_SEPARATOR_MENU_ITEM(child))
		{
			bPastSeparator = TRUE;
		}
	}
	g_list_free(children);

	vector<ExternalTool> externaltools = m_ExternalToolsPtr->GetExternalTools();
	for (unsigned int i = 0; i < externaltools.size(); ++i)
	{
		// this should be unique, something like ExternalTool_category_name
		stringstream ss;
		ss << "ExternalTool_" << externaltools[i].GetID();
		string name = ss.str();

		QuiverUtils::RemoveAction(name.c_str());
		QuiverUtils::AddSimpleAction(name.c_str(), "", quiver_new_action_handler_cb, this);

		string full_name = "quiver." + name;
		GtkWidget *item = gtk_menu_item_new_with_label(externaltools[i].GetName().c_str());
		gtk_actionable_set_action_name(GTK_ACTIONABLE(item), full_name.c_str());
		gtk_widget_show(item);
		gtk_menu_shell_append(GTK_MENU_SHELL(m_pMenuToolsExternal), item);
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
#define ACTION_QUIVER_GO_FOLDER_PARENT                       "GoFolderParent"
#define ACTION_QUIVER_GO_FOLDER_NEXT                         "GoFolderNext"
#define ACTION_QUIVER_GO_FOLDER_PREV                         "GoFolderPrev"
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
"<interface>"
"	<object class='GtkMenuBar' id='MenubarMain'>"
"		<child>"
"			<object class='GtkMenuItem' id='MenuFile'>"
"				<property name='label' translatable='yes'>_File</property>"
"				<property name='use_underline'>True</property>"
"				<child type='submenu'>"
"					<object class='GtkMenu'>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemOpen'>"
"								<property name='label' translatable='yes'>_Open</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.FileOpen</property>"
"								<property name='tooltip-text' translatable='yes'>Open an image</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemOpenFolder'>"
"								<property name='label' translatable='yes'>Open _Folder</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.FileOpenFolder</property>"
"								<property name='tooltip-text' translatable='yes'>Open a Folder</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemBrowserOpenLocation'>"
"								<property name='label' translatable='yes'>Open _Location</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.BrowserOpenLocation</property>"
"								<property name='tooltip-text' translatable='yes'>Open a Location</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemSave'>"
"								<property name='label' translatable='yes'>_Save</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.Save</property>"
"								<property name='tooltip-text' translatable='yes'>Save the Image</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemSaveAs'>"
"								<property name='label' translatable='yes'>Save _As</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.SaveAs</property>"
"								<property name='tooltip-text' translatable='yes'>Save the Image As</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemClose'>"
"								<property name='label' translatable='yes'>_Close</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.Close</property>"
"								<property name='tooltip-text' translatable='yes'>Close quiver</property>"
"							</object>"
"						</child>"
"					</object>"
"				</child>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkMenuItem' id='MenuEdit'>"
"				<property name='label' translatable='yes'>_Edit</property>"
"				<property name='use_underline'>True</property>"
"				<child type='submenu'>"
"					<object class='GtkMenu'>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemBrowserCopy'>"
"								<property name='label' translatable='yes'>_Copy</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.BrowserCopy</property>"
"								<property name='tooltip-text' translatable='yes'>Copy image</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemViewerCopy'>"
"								<property name='label' translatable='yes'>_Copy</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.ViewerCopy</property>"
"								<property name='tooltip-text' translatable='yes'>Copy image</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemBrowserTrash'>"
"								<property name='label' translatable='yes'>_Move To Trash</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.BrowserTrash</property>"
"								<property name='tooltip-text' translatable='yes'>Move selected image(s) to the Trash</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemViewerTrash'>"
"								<property name='label' translatable='yes'>_Move To Trash</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.ViewerTrash</property>"
"								<property name='tooltip-text' translatable='yes'>Move image to the Trash</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemPreferences'>"
"								<property name='label' translatable='yes'>_Preferences</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.Preferences</property>"
"								<property name='tooltip-text' translatable='yes'>Edit quiver preferences</property>"
"							</object>"
"						</child>"
"					</object>"
"				</child>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkMenuItem' id='MenuView'>"
"				<property name='label' translatable='yes'>_View</property>"
"				<property name='use_underline'>True</property>"
"				<child type='submenu'>"
"					<object class='GtkMenu'>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemUIModeViewer'>"
"								<property name='label' translatable='yes'>_Viewer</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.UIModeViewer</property>"
"								<property name='tooltip-text' translatable='yes'>View Image</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemUIModeBrowser'>"
"								<property name='label' translatable='yes'>_Browser</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.UIModeBrowser</property>"
"								<property name='tooltip-text' translatable='yes'>Browse Images</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkCheckMenuItem' id='CheckMenuItemMenubar'>"
"								<property name='label' translatable='yes'>Menubar</property>"
"								<property name='tooltip-text' translatable='yes'>Show/Hide the Menubar</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkCheckMenuItem' id='CheckMenuItemToolbar'>"
"								<property name='label' translatable='yes'>Toolbar</property>"
"								<property name='tooltip-text' translatable='yes'>Show/Hide the Toolbar</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkCheckMenuItem' id='CheckMenuItemProperties'>"
"								<property name='label' translatable='yes'>Properties</property>"
"								<property name='tooltip-text' translatable='yes'>Show/Hide Image Properties</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkCheckMenuItem' id='CheckMenuItemStatusbar'>"
"								<property name='label' translatable='yes'>Statusbar</property>"
"								<property name='tooltip-text' translatable='yes'>Show/Hide the Statusbar</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkCheckMenuItem' id='CheckMenuItemBrowserSidebar'>"
"								<property name='label' translatable='yes'>Sidebar</property>"
"								<property name='tooltip-text' translatable='yes'>Show/Hide Sidebar (Folder Tree, Preview Window, etc)</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkCheckMenuItem' id='CheckMenuItemBrowserPreview'>"
"								<property name='label' translatable='yes'>Preview</property>"
"								<property name='tooltip-text' translatable='yes'>Show/Hide Image Preview</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkCheckMenuItem' id='CheckMenuItemViewFilmStrip'>"
"								<property name='label' translatable='yes'>Film Strip</property>"
"								<property name='tooltip-text' translatable='yes'>Show/Hide Film Strip</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuSort'>"
"								<property name='label' translatable='yes'>_Arrange Items</property>"
"								<property name='use_underline'>True</property>"
"								<child type='submenu'>"
"									<object class='GtkMenu'>"
"										<child>"
"											<object class='GtkRadioMenuItem' id='RadioMenuItemSortByName'>"
"												<property name='label' translatable='yes'>By _Name</property>"
"												<property name='use_underline'>True</property>"
"												<property name='tooltip-text' translatable='yes'>Sort by file name</property>"
"											</object>"
"										</child>"
"										<child>"
"											<object class='GtkRadioMenuItem' id='RadioMenuItemSortByNameNatural'>"
"												<property name='label' translatable='yes'>By _Name (natural order)</property>"
"												<property name='use_underline'>True</property>"
"												<property name='group'>RadioMenuItemSortByName</property>"
"												<property name='tooltip-text' translatable='yes'>Sort by file name (natural order)</property>"
"											</object>"
"										</child>"
"										<child>"
"											<object class='GtkRadioMenuItem' id='RadioMenuItemSortByDate'>"
"												<property name='label' translatable='yes'>By _Date</property>"
"												<property name='use_underline'>True</property>"
"												<property name='group'>RadioMenuItemSortByName</property>"
"												<property name='tooltip-text' translatable='yes'>Sort by date</property>"
"											</object>"
"										</child>"
"										<child>"
"											<object class='GtkRadioMenuItem' id='RadioMenuItemSortByDateModified'>"
"												<property name='label' translatable='yes'>By Date _Modified</property>"
"												<property name='use_underline'>True</property>"
"												<property name='group'>RadioMenuItemSortByName</property>"
"												<property name='tooltip-text' translatable='yes'>Sort by date modified</property>"
"											</object>"
"										</child>"
"										<child>"
"											<object class='GtkRadioMenuItem' id='RadioMenuItemSortByRandom'>"
"												<property name='label' translatable='yes'>_Randomize</property>"
"												<property name='use_underline'>True</property>"
"												<property name='group'>RadioMenuItemSortByName</property>"
"												<property name='tooltip-text' translatable='yes'>Randomize</property>"
"											</object>"
"										</child>"
"										<child><object class='GtkSeparatorMenuItem'/></child>"
"										<child>"
"											<object class='GtkCheckMenuItem' id='CheckMenuItemSortDescending'>"
"												<property name='label' translatable='yes'>In _Descending Order</property>"
"												<property name='use_underline'>True</property>"
"												<property name='tooltip-text' translatable='yes'>Arrange the items in descending order</property>"
"											</object>"
"										</child>"
"									</object>"
"								</child>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkCheckMenuItem' id='CheckMenuItemFullScreen'>"
"								<property name='label' translatable='yes'>_Full Screen</property>"
"								<property name='use_underline'>True</property>"
"								<property name='tooltip-text' translatable='yes'>Toggle Full Screen Mode</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkCheckMenuItem' id='CheckMenuItemSlideShow'>"
"								<property name='label' translatable='yes'>_Slide Show</property>"
"								<property name='use_underline'>True</property>"
"								<property name='tooltip-text' translatable='yes'>Toggle Slide Show</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuZoom'>"
"								<property name='label' translatable='yes'>Zoom</property>"
"								<child type='submenu'>"
"									<object class='GtkMenu'>"
"										<child>"
"											<object class='GtkRadioMenuItem' id='RadioMenuItemZoomFit'>"
"												<property name='label' translatable='yes'>Zoom _Fit</property>"
"												<property name='use_underline'>True</property>"
"												<property name='tooltip-text' translatable='yes'>Fit to Window</property>"
"											</object>"
"										</child>"
"										<child>"
"											<object class='GtkRadioMenuItem' id='RadioMenuItemZoomFitStretch'>"
"												<property name='label' translatable='yes'>Zoom Fit _Stretch</property>"
"												<property name='use_underline'>True</property>"
"												<property name='group'>RadioMenuItemZoomFit</property>"
"												<property name='tooltip-text' translatable='yes'>Fit to Window Stretch</property>"
"											</object>"
"										</child>"
"										<child>"
"											<object class='GtkRadioMenuItem' id='RadioMenuItemZoom100'>"
"												<property name='label' translatable='yes'>_Actual Size</property>"
"												<property name='use_underline'>True</property>"
"												<property name='group'>RadioMenuItemZoomFit</property>"
"												<property name='tooltip-text' translatable='yes'>Actual Size</property>"
"											</object>"
"										</child>"
"										<child>"
"											<object class='GtkRadioMenuItem' id='RadioMenuItemZoomFillScreen'>"
"												<property name='label' translatable='yes'>_Fill Screen</property>"
"												<property name='use_underline'>True</property>"
"												<property name='group'>RadioMenuItemZoomFit</property>"
"												<property name='tooltip-text' translatable='yes'>Fill the screen with the image</property>"
"											</object>"
"										</child>"
"										<child><object class='GtkSeparatorMenuItem'/></child>"
"										<child>"
"											<object class='GtkCheckMenuItem' id='CheckMenuItemMaximizeForDisplay'>"
"												<property name='label' translatable='yes'>Rotate to _Maximize View</property>"
"												<property name='use_underline'>True</property>"
"												<property name='tooltip-text' translatable='yes'>Rotate Images to Maximize Display Area</property>"
"											</object>"
"										</child>"
"									</object>"
"								</child>"
"							</object>"
"						</child>"
"					</object>"
"				</child>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkMenuItem' id='MenuImage'>"
"				<property name='label' translatable='yes'>_Image</property>"
"				<property name='use_underline'>True</property>"
"				<child type='submenu'>"
"					<object class='GtkMenu'>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemRotateCW'>"
"								<property name='label' translatable='yes'>_Rotate Clockwise</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.RotateCW</property>"
"								<property name='tooltip-text' translatable='yes'>Rotate Clockwise</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemRotateCCW'>"
"								<property name='label' translatable='yes'>Rotate _Counterclockwise</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.RotateCCW</property>"
"								<property name='tooltip-text' translatable='yes'>Rotate Counterclockwise</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemFlipH'>"
"								<property name='label' translatable='yes'>Flip _Horizontally</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.FlipH</property>"
"								<property name='tooltip-text' translatable='yes'>Flip Horizontally</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemFlipV'>"
"								<property name='label' translatable='yes'>Flip _Vertically</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.FlipV</property>"
"								<property name='tooltip-text' translatable='yes'>Flip Vertically</property>"
"							</object>"
"						</child>"
"					</object>"
"				</child>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkMenuItem' id='MenuGo'>"
"				<property name='label' translatable='yes'>_Go</property>"
"				<property name='use_underline'>True</property>"
"				<child type='submenu'>"
"					<object class='GtkMenu'>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemImageFirst'>"
"								<property name='label' translatable='yes'>_First Image</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.ImageFirst</property>"
"								<property name='tooltip-text' translatable='yes'>Go to first image</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemImagePrevious'>"
"								<property name='label' translatable='yes'>_Previous Image</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.ImagePrevious</property>"
"								<property name='tooltip-text' translatable='yes'>Go to previous image</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemImageNext'>"
"								<property name='label' translatable='yes'>_Next Image</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.ImageNext</property>"
"								<property name='tooltip-text' translatable='yes'>Go to next image</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemImageLast'>"
"								<property name='label' translatable='yes'>_Last Image</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.ImageLast</property>"
"								<property name='tooltip-text' translatable='yes'>Go to last image</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemBrowserHistoryBack'>"
"								<property name='label' translatable='yes'>Go _Back</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.BrowserHistoryBack</property>"
"								<property name='tooltip-text' translatable='yes'>Go Back</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemBrowserHistoryForward'>"
"								<property name='label' translatable='yes'>Go _Forward</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.BrowserHistoryForward</property>"
"								<property name='tooltip-text' translatable='yes'>Go Forward</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemGoFolderParent'>"
"								<property name='label' translatable='yes'>Open _Parent</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.GoFolderParent</property>"
"								<property name='tooltip-text' translatable='yes'>Open parent folder</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemGoFolderNext'>"
"								<property name='label' translatable='yes'>Open _Next Folder</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.GoFolderNext</property>"
"								<property name='tooltip-text' translatable='yes'>Open next folder</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemGoFolderPrev'>"
"								<property name='label' translatable='yes'>Open _Previous Folder</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.GoFolderPrev</property>"
"								<property name='tooltip-text' translatable='yes'>Open previous folder</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"					</object>"
"				</child>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkMenuItem' id='MenuBookmarks'>"
"				<property name='label' translatable='yes'>_Bookmarks</property>"
"				<property name='use_underline'>True</property>"
"				<child type='submenu'>"
"					<object class='GtkMenu' id='MenuBookmarkItems'>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemBookmarksAdd'>"
"								<property name='label' translatable='yes'>_Add Bookmark</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.BookmarksAdd</property>"
"								<property name='tooltip-text' translatable='yes'>Add a bookmark</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemBookmarksEdit'>"
"								<property name='label' translatable='yes'>_Edit Bookmarks...</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.BookmarksEdit</property>"
"								<property name='tooltip-text' translatable='yes'>Edit the bookmarks</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem' id='BookmarkItemsSeparator'/></child>"
"					</object>"
"				</child>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkMenuItem' id='MenuTools'>"
"				<property name='label' translatable='yes'>_Tools</property>"
"				<property name='use_underline'>True</property>"
"				<child type='submenu'>"
"					<object class='GtkMenu' id='MenuToolsExternal'>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemAdjustDate'>"
"								<property name='label' translatable='yes'>Adjust Date...</property>"
"								<property name='action_name'>quiver.AdjustDate</property>"
"								<property name='tooltip-text' translatable='yes'>Adjust Exif Dates</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemRename'>"
"								<property name='label' translatable='yes'>Rename...</property>"
"								<property name='action_name'>quiver.Rename</property>"
"								<property name='tooltip-text' translatable='yes'>Rename image(s)</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemOrganize'>"
"								<property name='label' translatable='yes'>Organize...</property>"
"								<property name='action_name'>quiver.Organize</property>"
"								<property name='tooltip-text' translatable='yes'>Organize photos</property>"
"							</object>"
"						</child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemExternalTools'>"
"								<property name='label' translatable='yes'>External Tools...</property>"
"								<property name='action_name'>quiver.ExternalTools</property>"
"								<property name='tooltip-text' translatable='yes'>Add / edit external tools</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem' id='ExternalToolsSeparator'/></child>"
"					</object>"
"				</child>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkMenuItem' id='MenuHelp'>"
"				<property name='label' translatable='yes'>_Help</property>"
"				<property name='use_underline'>True</property>"
"				<child type='submenu'>"
"					<object class='GtkMenu'>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemDonate'>"
"								<property name='label' translatable='yes'>_Donate...</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.Donate</property>"
"								<property name='tooltip-text' translatable='yes'>Help support quiver by donating...</property>"
"							</object>"
"						</child>"
"						<child><object class='GtkSeparatorMenuItem'/></child>"
"						<child>"
"							<object class='GtkMenuItem' id='MenuItemAbout'>"
"								<property name='label' translatable='yes'>_About</property>"
"								<property name='use_underline'>True</property>"
"								<property name='action_name'>quiver.About</property>"
"								<property name='tooltip-text' translatable='yes'>About quiver</property>"
"							</object>"
"						</child>"
"					</object>"
"				</child>"
"			</object>"
"		</child>"
"	</object>"
"	<object class='GtkToolbar' id='ToolbarMain'>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonUIModeViewer'>"
"				<property name='icon_name'>gtk-file</property>"
"				<property name='action_name'>quiver.UIModeViewer</property>"
"				<property name='tooltip-text' translatable='yes'>View Image</property>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonUIModeBrowser'>"
"				<property name='icon_name'>gtk-directory</property>"
"				<property name='action_name'>quiver.UIModeBrowser</property>"
"				<property name='tooltip-text' translatable='yes'>Browse Images</property>"
"			</object>"
"		</child>"
"		<child><object class='GtkSeparatorToolItem'/></child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonBrowserHistoryBack'>"
"				<property name='icon_name'>gtk-go-back</property>"
"				<property name='action_name'>quiver.BrowserHistoryBack</property>"
"				<property name='tooltip-text' translatable='yes'>Go Back</property>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonBrowserHistoryForward'>"
"				<property name='icon_name'>gtk-go-forward</property>"
"				<property name='action_name'>quiver.BrowserHistoryForward</property>"
"				<property name='tooltip-text' translatable='yes'>Go Forward</property>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonImagePrevious'>"
"				<property name='icon_name'>gtk-go-back</property>"
"				<property name='action_name'>quiver.ImagePrevious</property>"
"				<property name='tooltip-text' translatable='yes'>Go to previous image</property>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonImageNext'>"
"				<property name='icon_name'>gtk-go-forward</property>"
"				<property name='action_name'>quiver.ImageNext</property>"
"				<property name='tooltip-text' translatable='yes'>Go to next image</property>"
"			</object>"
"		</child>"
"		<child><object class='GtkSeparatorToolItem'/></child>"
"		<child>"
"			<object class='GtkToggleToolButton' id='ToolButtonBrowserSidebar'>"
"				<property name='icon_name'>gtk-directory</property>"
"				<property name='tooltip-text' translatable='yes'>Show/Hide Sidebar (Folder Tree, Preview Window, etc)</property>"
"			</object>"
"		</child>"
"		<child><object class='GtkSeparatorToolItem'/></child>"
"		<child>"
"			<object class='GtkToggleToolButton' id='ToolButtonFullScreen'>"
"				<property name='icon_name'>gtk-fullscreen</property>"
"				<property name='tooltip-text' translatable='yes'>Toggle Full Screen Mode</property>"
"			</object>"
"		</child>"
"		<child><object class='GtkSeparatorToolItem'/></child>"
"		<child>"
"			<object class='GtkToggleToolButton' id='ToolButtonSlideShow'>"
"				<property name='icon_name'>gtk-index</property>"
"				<property name='tooltip-text' translatable='yes'>Toggle Slide Show</property>"
"			</object>"
"		</child>"
"		<child><object class='GtkSeparatorToolItem'/></child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonZoomIn'>"
"				<property name='icon_name'>gtk-zoom-in</property>"
"				<property name='action_name'>quiver.ZoomIn</property>"
"				<property name='tooltip-text' translatable='yes'>Zoom In</property>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonZoomOut'>"
"				<property name='icon_name'>gtk-zoom-out</property>"
"				<property name='action_name'>quiver.ZoomOut</property>"
"				<property name='tooltip-text' translatable='yes'>Zoom Out</property>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonZoom100'>"
"				<property name='icon_name'>gtk-zoom-100</property>"
"				<property name='action_name'>quiver.Zoom100</property>"
"				<property name='tooltip-text' translatable='yes'>Actual Size</property>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonZoomFit'>"
"				<property name='icon_name'>gtk-zoom-fit</property>"
"				<property name='action_name'>quiver.ZoomFit</property>"
"				<property name='tooltip-text' translatable='yes'>Fit to Window</property>"
"			</object>"
"		</child>"
"		<child><object class='GtkSeparatorToolItem'/></child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonRotateCCW'>"
"				<property name='icon_name'>edit-undo</property>"
"				<property name='action_name'>quiver.RotateCCW</property>"
"				<property name='tooltip-text' translatable='yes'>Rotate Counterclockwise</property>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonRotateCW'>"
"				<property name='icon_name'>edit-redo</property>"
"				<property name='action_name'>quiver.RotateCW</property>"
"				<property name='tooltip-text' translatable='yes'>Rotate Clockwise</property>"
"			</object>"
"		</child>"
"		<child><object class='GtkSeparatorToolItem'/></child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonBrowserTrash'>"
"				<property name='icon_name'>gtk-delete</property>"
"				<property name='action_name'>quiver.BrowserTrash</property>"
"				<property name='tooltip-text' translatable='yes'>Move selected image(s) to the Trash</property>"
"			</object>"
"		</child>"
"		<child>"
"			<object class='GtkToolButton' id='ToolButtonViewerTrash'>"
"				<property name='icon_name'>gtk-delete</property>"
"				<property name='action_name'>quiver.ViewerTrash</property>"
"				<property name='tooltip-text' translatable='yes'>Move image to the Trash</property>"
"			</object>"
"		</child>"
"		<child><object class='GtkSeparatorToolItem'/></child>"
"	</object>"
"</interface>";

const GtkTargetEntry QuiverImpl::quiver_drag_target_table[] = {
		{ (gchar*)"STRING",     0, QUIVER_TARGET_STRING }, // STRING is used for legacy motif apps
		{ (gchar*)"text/plain", 0, QUIVER_TARGET_STRING },  // the real mime types to support
		 { (gchar*)"text/uri-list", 0, QUIVER_TARGET_URI },
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
{ (void)widget; 
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
	
	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_FULLSCREEN, bFullscreen);

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
	delete this;	
}

gboolean Quiver::EventDelete( GtkWidget *widget,GdkEvent  *event, gpointer   data )
{ (void)data;  (void)event;  (void)widget; 
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
	m_QuiverImplPtr->m_pMenuBookmarkItems = NULL;
	m_QuiverImplPtr->m_pMenuToolsExternal = NULL;

	m_QuiverImplPtr->m_bViewerMode = false;

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

	GtkBuilder *builder = gtk_builder_new();
	GError *error = NULL;
	gtk_builder_add_from_string(builder, quiver_ui_main, -1, &error);
	if (NULL != error)
	{
		g_critical("%s", error->message);
		g_error_free(error);
	}

	m_QuiverImplPtr->m_pMenubar = GTK_WIDGET(gtk_builder_get_object(builder, "MenubarMain"));
	m_QuiverImplPtr->m_pToolbar = GTK_WIDGET(gtk_builder_get_object(builder, "ToolbarMain"));
	m_QuiverImplPtr->m_pMenuBookmarkItems = GTK_WIDGET(gtk_builder_get_object(builder, "MenuBookmarkItems"));
	m_QuiverImplPtr->m_pMenuToolsExternal = GTK_WIDGET(gtk_builder_get_object(builder, "MenuToolsExternal"));

	gtk_toolbar_set_style(GTK_TOOLBAR(m_QuiverImplPtr->m_pToolbar), GTK_TOOLBAR_ICONS);
#ifdef QUIVER_MAEMO
	gtk_toolbar_set_tooltips(GTK_TOOLBAR(m_QuiverImplPtr->m_pToolbar), FALSE);
#endif

	/* GSimpleAction based action system (replaces GtkUIManager/GtkAction).
	 * Actions are registered here and their widgets bound with
	 * QuiverUtils::BindWidget() / BindToggleWidget() / BindRadioWidget(). */
	QuiverUtils::InitActions();
	gtk_widget_insert_action_group(m_QuiverImplPtr->m_pQuiverWindow, "quiver",
		G_ACTION_GROUP(QuiverUtils::GetActionGroup()));
	QuiverUtils::AddAccelGroup(GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow));

	/* Global simple actions */
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_OPEN, "<Control>o", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_OPEN_FOLDER, "<Control>f", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_SAVE, "<Control>s", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_SAVE_AS, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_CLOSE, "<Alt>F4", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_CLOSE_2, "q", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_CLOSE_3, "<Control>q", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_CLOSE_4, "<Control>w", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_ESCAPE, "Escape", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_PREFERENCES, "<Control>p", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_UI_MODE_BROWSER, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_UI_MODE_VIEWER, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_GO_FOLDER_PARENT, "<Alt>Up", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_GO_FOLDER_NEXT, "<Shift><Alt>Right", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_GO_FOLDER_PREV, "<Shift><Alt>Left", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_BOOKMARKS_ADD, "<Control>d", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_BOOKMARKS_EDIT, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_ADJUST_DATE, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_ORGANIZE, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_RENAME, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_EXTERNAL_TOOLS, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_DONATE, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_ABOUT, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
#ifdef QUIVER_MAEMO
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_UI_MODE_SWITCH_MAEMO, "Return", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
#endif

	/* Global toggle actions */
	QuiverUtils::AddToggleAction(ACTION_QUIVER_FULLSCREEN, "f", FALSE, quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddToggleAction(ACTION_QUIVER_SLIDESHOW, "s", FALSE, quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddToggleAction(ACTION_QUIVER_VIEW_MENUBAR, "<Control><Shift>M",
		prefsPtr->GetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_MENUBAR_SHOW, true), quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddToggleAction(ACTION_QUIVER_VIEW_TOOLBAR_MAIN, "<Control><Shift>T",
		prefsPtr->GetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_TOOLBAR_SHOW, true), quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddToggleAction(ACTION_QUIVER_VIEW_STATUSBAR, "<Control><Shift>S",
		prefsPtr->GetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_STATUSBAR_SHOW, true), quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddToggleAction(ACTION_QUIVER_VIEW_PROPERTIES, "<Alt>Return", FALSE, quiver_new_action_handler_cb, m_QuiverImplPtr.get());
	QuiverUtils::AddToggleAction(ACTION_QUIVER_SORT_DESCENDING, "",
		prefsPtr->GetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_SORT_REVERSED, false), quiver_new_action_handler_cb, m_QuiverImplPtr.get());

	/* Sort order radio actions */
	const gchar *sort_names[] = {
		ACTION_QUIVER_SORT_BY_NAME,
		ACTION_QUIVER_SORT_BY_NAME_NATURAL,
		ACTION_QUIVER_SORT_BY_DATE,
		ACTION_QUIVER_SORT_BY_DATE_MODIFIED,
		ACTION_QUIVER_SORT_BY_RANDOM };
	gint sort_values[] = {
		ImageList::SORT_BY_FILENAME,
		ImageList::SORT_BY_FILENAME_NATURAL,
		ImageList::SORT_BY_DATE,
		ImageList::SORT_BY_DATE_MODIFIED,
		ImageList::SORT_BY_RANDOM };
	QuiverUtils::AddRadioActions(sort_names, sort_values, G_N_ELEMENTS(sort_names),
		prefsPtr->GetInteger(QUIVER_PREFS_APP, QUIVER_PREFS_APP_SORT_BY, ImageList::SORT_BY_FILENAME_NATURAL),
		quiver_new_action_handler_cb, m_QuiverImplPtr.get());

	/* Register the browser and viewer actions */
	m_QuiverImplPtr->m_BrowserPtr->RegisterActions();
	m_QuiverImplPtr->m_ViewerPtr->RegisterActions();

	/* Give the browser the shared toolbar so it can insert its thumb-size widget */
	m_QuiverImplPtr->m_BrowserPtr->SetToolbar(GTK_TOOLBAR(m_QuiverImplPtr->m_pToolbar));

	/* Bind global toggle widgets */
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemFullScreen")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_FULLSCREEN);
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemSlideShow")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_SLIDESHOW);
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemMenubar")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_VIEW_MENUBAR);
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemToolbar")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_VIEW_TOOLBAR_MAIN);
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemStatusbar")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_VIEW_STATUSBAR);
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemProperties")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_VIEW_PROPERTIES);
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemSortDescending")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_SORT_DESCENDING);
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonFullScreen")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_FULLSCREEN);
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonSlideShow")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_SLIDESHOW);

	/* Bind sort radio widgets */
	QuiverUtils::BindRadioWidget(GTK_WIDGET(gtk_builder_get_object(builder, "RadioMenuItemSortByName")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_SORT_BY_NAME);
	QuiverUtils::BindRadioWidget(GTK_WIDGET(gtk_builder_get_object(builder, "RadioMenuItemSortByNameNatural")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_SORT_BY_NAME_NATURAL);
	QuiverUtils::BindRadioWidget(GTK_WIDGET(gtk_builder_get_object(builder, "RadioMenuItemSortByDate")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_SORT_BY_DATE);
	QuiverUtils::BindRadioWidget(GTK_WIDGET(gtk_builder_get_object(builder, "RadioMenuItemSortByDateModified")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_SORT_BY_DATE_MODIFIED);
	QuiverUtils::BindRadioWidget(GTK_WIDGET(gtk_builder_get_object(builder, "RadioMenuItemSortByRandom")), m_QuiverImplPtr->m_pQuiverWindow, ACTION_QUIVER_SORT_BY_RANDOM);

	/* Bind browser toggle widgets */
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemBrowserSidebar")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserViewSidebar");
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemBrowserPreview")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserViewPreview");
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonBrowserSidebar")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserViewSidebar");

	/* Bind viewer toggle widgets */
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemViewFilmStrip")), m_QuiverImplPtr->m_pQuiverWindow, "ViewFilmStrip");
	QuiverUtils::BindToggleWidget(GTK_WIDGET(gtk_builder_get_object(builder, "CheckMenuItemMaximizeForDisplay")), m_QuiverImplPtr->m_pQuiverWindow, "MaximizeForDisplay");

	/* Bind viewer zoom radio widgets */
	QuiverUtils::BindRadioWidget(GTK_WIDGET(gtk_builder_get_object(builder, "RadioMenuItemZoomFit")), m_QuiverImplPtr->m_pQuiverWindow, "ZoomFit");
	QuiverUtils::BindRadioWidget(GTK_WIDGET(gtk_builder_get_object(builder, "RadioMenuItemZoomFitStretch")), m_QuiverImplPtr->m_pQuiverWindow, "ZoomFitStretch");
	QuiverUtils::BindRadioWidget(GTK_WIDGET(gtk_builder_get_object(builder, "RadioMenuItemZoom100")), m_QuiverImplPtr->m_pQuiverWindow, "Zoom100");
	QuiverUtils::BindRadioWidget(GTK_WIDGET(gtk_builder_get_object(builder, "RadioMenuItemZoomFillScreen")), m_QuiverImplPtr->m_pQuiverWindow, "ZoomFillScreen");

	/* Bind the browser simple-action widgets */
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemBrowserOpenLocation")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserOpenLocation");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemBrowserCopy")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserCopy");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemBrowserTrash")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserTrash");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemBrowserHistoryBack")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserHistoryBack");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemBrowserHistoryForward")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserHistoryForward");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonBrowserHistoryBack")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserHistoryBack");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonBrowserHistoryForward")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserHistoryForward");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonBrowserTrash")), m_QuiverImplPtr->m_pQuiverWindow, "BrowserTrash");

	/* Bind the viewer simple-action widgets */
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemViewerCopy")), m_QuiverImplPtr->m_pQuiverWindow, "ViewerCopy");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemViewerTrash")), m_QuiverImplPtr->m_pQuiverWindow, "ViewerTrash");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonViewerTrash")), m_QuiverImplPtr->m_pQuiverWindow, "ViewerTrash");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemImageFirst")), m_QuiverImplPtr->m_pQuiverWindow, "ImageFirst");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemImagePrevious")), m_QuiverImplPtr->m_pQuiverWindow, "ImagePrevious");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemImageNext")), m_QuiverImplPtr->m_pQuiverWindow, "ImageNext");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemImageLast")), m_QuiverImplPtr->m_pQuiverWindow, "ImageLast");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonImagePrevious")), m_QuiverImplPtr->m_pQuiverWindow, "ImagePrevious");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonImageNext")), m_QuiverImplPtr->m_pQuiverWindow, "ImageNext");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonZoomIn")), m_QuiverImplPtr->m_pQuiverWindow, "ZoomIn");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonZoomOut")), m_QuiverImplPtr->m_pQuiverWindow, "ZoomOut");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonZoom100")), m_QuiverImplPtr->m_pQuiverWindow, "Zoom100");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonZoomFit")), m_QuiverImplPtr->m_pQuiverWindow, "ZoomFit");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemRotateCW")), m_QuiverImplPtr->m_pQuiverWindow, "RotateCW");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemRotateCCW")), m_QuiverImplPtr->m_pQuiverWindow, "RotateCCW");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemFlipH")), m_QuiverImplPtr->m_pQuiverWindow, "FlipH");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "MenuItemFlipV")), m_QuiverImplPtr->m_pQuiverWindow, "FlipV");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonRotateCCW")), m_QuiverImplPtr->m_pQuiverWindow, "RotateCCW");
	QuiverUtils::BindWidget(GTK_WIDGET(gtk_builder_get_object(builder, "ToolButtonRotateCW")), m_QuiverImplPtr->m_pQuiverWindow, "RotateCW");

	m_QuiverImplPtr->m_BrowserPtr->SetStatusbar(m_QuiverImplPtr->m_StatusbarPtr);
	m_QuiverImplPtr->m_ViewerPtr->SetStatusbar(m_QuiverImplPtr->m_StatusbarPtr);

	/* keep the builder alive so the mode-dependent widgets can be looked up */
	m_QuiverImplPtr->m_pBuilder = builder;
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
	
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
	m_QuiverImplPtr->m_pHPanedMainArea = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_name(m_QuiverImplPtr->m_pHPanedMainArea,"Quiver hpaned");
	
	hbox_browser_viewer_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	gtk_widget_set_name(hbox_browser_viewer_container,"Quiver hbox 1");
	m_QuiverImplPtr->m_pNBProperties = gtk_notebook_new();
	gtk_widget_set_name(m_QuiverImplPtr->m_pNBProperties ,"Quiver notebook 1");
	
	gtk_widget_set_no_show_all(m_QuiverImplPtr->m_pNBProperties,TRUE);
	
	bool prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PROPS_SHOW);

	if (prefs_show)
	{
		gtk_widget_show(m_QuiverImplPtr->m_pNBProperties);
	}
	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_PROPERTIES, prefs_show);

	{
		int sortby = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_SORT_BY,ImageList::SORT_BY_FILENAME_NATURAL);
		QuiverUtils::SetRadioActionCurrent(ACTION_QUIVER_SORT_BY_NAME_NATURAL, sortby);
	}

	{
		bool bDec = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_SORT_REVERSED,false);
		QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_SORT_DESCENDING, bDec);
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
	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_STATUSBAR, prefs_show);

	// menubar
	gtk_widget_set_no_show_all(m_QuiverImplPtr->m_pMenubar,TRUE);

	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW, true);
	if (prefs_show)
	{
		gtk_widget_show(m_QuiverImplPtr->m_pMenubar);

		// GtkBuilder does not show menu items the way GtkUIManager used to,
		// so explicitly show the top-level items and their submenu contents.
		GList *items = gtk_container_get_children(GTK_CONTAINER(m_QuiverImplPtr->m_pMenubar));
		for (GList *itr = items; NULL != itr; itr = g_list_next(itr))
		{
			GtkWidget *item = GTK_WIDGET(itr->data);
			gtk_widget_show(item);
			GtkWidget *submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
			if (NULL != submenu)
			{
				gtk_widget_show_all(submenu);
			}
		}
		g_list_free(items);
	}
	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_MENUBAR, prefs_show);
	
	// toolbar
	gtk_widget_set_no_show_all(m_QuiverImplPtr->m_pToolbar,TRUE);

	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW, true);
	if (prefs_show)
	{
		gtk_widget_show(m_QuiverImplPtr->m_pToolbar);
	}
	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_TOOLBAR_MAIN, prefs_show);

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
	
	// load the user's theme overrides (gtk_rc_parse is deprecated, so use a CSS provider)
	if (g_file_test (gtk_rc.c_str(), G_FILE_TEST_EXISTS))
	{
		GtkCssProvider* cssProvider = gtk_css_provider_new();
		GError* cssError = NULL;
		if (gtk_css_provider_load_from_path (cssProvider, gtk_rc.c_str(), &cssError))
		{
			gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
				GTK_STYLE_PROVIDER (cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
		}
		else if (NULL != cssError)
		{
			g_error_free (cssError);
		}
		g_object_unref (cssProvider);
	}

	string strAccelMap = g_szConfigDir + string("/quiver_keys.map");	
	
	gtk_accel_map_load(strAccelMap.c_str());

	PreferencesPtr prefsPtr = Preferences::GetInstance();

	GdkMonitor* monitor = gdk_display_get_primary_monitor(gdk_display_get_default());
	GdkRectangle screen_geom;
	gdk_monitor_get_geometry(monitor, &screen_geom);

	m_QuiverImplPtr->m_iAppX      = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_LEFT, screen_geom.width/4);
	m_QuiverImplPtr->m_iAppY      = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOP, screen_geom.height/4);
	m_QuiverImplPtr->m_iAppWidth  = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WIDTH, screen_geom.width/2);
	m_QuiverImplPtr->m_iAppHeight = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_HEIGHT, screen_geom.height/2);
	
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
		struct stat stat_struct = {};
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



#ifdef QUIVER_MAEMO

static void
mime_open_handler (gpointer raw_data, int argc, char **argv)
{
	QuiverImpl* pQuiverImpl = static_cast<QuiverImpl*>(raw_data);
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
 (void)pQuiver;
	return FALSE; // run once
}

int main (int argc, char **argv)
{
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

 	/* init threads */
	//g_type_init ();

	
	/* Initialize the widget set */
	gtk_init (&argc, &argv);

	// set dark theme
	g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);

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

	CreateQuiverData cqd = {};
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
			if (!photo_library.empty())
				files.push_back(photo_library);
			GFile* file = g_file_new_for_path("/");
			GFile* homedir = g_file_new_for_path(dir);
			GFile* photoLib = g_file_new_for_uri(photo_library.c_str());

			if (!g_file_equal(file, photoLib) && !g_file_equal(photoLib, homedir))
			{
				// just in case the users sets the root
				// as the photo library
				cqd.bRecursive = true;
			}
			g_object_unref(photoLib);
			g_object_unref(homedir);
			g_object_unref(file);
		}
		else
		{
			files.push_back(dir);
		}

	}
	//pthread_setconcurrency(4);

	cqd.pFiles = &files;
	// FIXME: will not create a window
	//gtk_init_add (CreateQuiver,&cqd);
	g_idle_add(CreateQuiver, &cqd);
	
	// FIX FOR BUG: http://bugzilla.gnome.org/show_bug.cgi?id=65041
	// race condition when registering types
	// we have many threads that create pixbuf loaders
	// so must do the following:
	
	//GdkPixbufNonAnim
	// ensure the GdkPixbufAnimation type is registered to avoid races with pixbuf loaders
	g_type_ensure (gdk_pixbuf_animation_get_type ());
	

	GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
	gdk_pixbuf_loader_close(loader, NULL);
	g_object_unref(loader);
	
	// END BUG FIX items
                                             
	gtk_main ();

	gst_deinit();
	

	return 0;
}

gboolean Quiver::idle_quiver_init (gpointer data)
{
	return ((Quiver*)data)->IdleQuiverInit(data);
}
gboolean Quiver::IdleQuiverInit(gpointer data)
{ (void)data; 
	// put process intenstive startup code in here 
	// (loading image list, setting first image)

	// set up the stock icons

	SetImageList(m_QuiverImplPtr->m_listImages, m_QuiverImplPtr->m_bListImagesRecursive);

	m_QuiverImplPtr->m_BrowserPtr->SetImageList(m_QuiverImplPtr->m_ImageListPtr);
	m_QuiverImplPtr->m_ViewerPtr->SetImageList(m_QuiverImplPtr->m_ImageListPtr);

	// call this a second time to make sure the list is updated
	if (m_QuiverImplPtr->m_bViewerMode)
	{
		ShowViewer();
	}
	else
	{
		ShowBrowser();
	}

	TaskManagerDlg::Create(GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow));

	m_QuiverImplPtr->m_bInitialized = true;

	return FALSE; // return false so it is never called again
}


static gboolean timeout_event_motion_notify (gpointer data)
{
	QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
	if (GDK_WINDOW_STATE_FULLSCREEN & pQuiverImpl->m_WindowState)
	{
		// FIXME:
		/*
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
		*/
	}
	pQuiverImpl->m_iTimeoutMouseMotionNotify = 0;
	return FALSE;
}


static gboolean event_motion_notify( GtkWidget *widget, GdkEventMotion *event, gpointer data )
{ (void)event;  (void)widget; 
	QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
	if (0 != pQuiverImpl->m_iTimeoutMouseMotionNotify)
	{
		g_source_remove(pQuiverImpl->m_iTimeoutMouseMotionNotify);
		pQuiverImpl->m_iTimeoutMouseMotionNotify = 0;
	}

#ifndef QUIVER_MAEMO	
	gdk_window_set_cursor (gtk_widget_get_window(pQuiverImpl->m_pQuiverWindow), NULL);
#endif

	pQuiverImpl->m_iTimeoutMouseMotionNotify = g_timeout_add(1500,timeout_event_motion_notify,pQuiverImpl);

	return FALSE;
}



//==============================================================================
//== ShowViewer / ShowBrowser ===================================================
//==============================================================================

static void show_ui_items(GtkBuilder *builder, const char **ids, guint n, bool bShow)
{
	for (guint i = 0; i < n; i++)
	{
		GtkWidget *widget = GTK_WIDGET(gtk_builder_get_object(builder, ids[i]));
		if (NULL != widget)
		{
			if (bShow)
			{
				gtk_widget_show(widget);
			}
			else
			{
				gtk_widget_hide(widget);
			}
		}
	}
}

void QuiverImpl::ShowViewerUIItems(QuiverImpl *pQuiverImpl, bool bShow)
{
	static const char *viewer_items[] = {
		"MenuItemSave", "MenuItemSaveAs", "MenuItemClose",
		"MenuItemViewerCopy", "MenuItemViewerTrash",
		"MenuItemImageFirst", "MenuItemImagePrevious", "MenuItemImageNext", "MenuItemImageLast",
		"MenuItemRotateCW", "MenuItemRotateCCW", "MenuItemFlipH", "MenuItemFlipV",
		"CheckMenuItemViewFilmStrip",
		"ToolButtonImagePrevious", "ToolButtonImageNext",
		"ToolButtonZoomIn", "ToolButtonZoomOut", "ToolButtonZoom100", "ToolButtonZoomFit",
		"ToolButtonRotateCCW", "ToolButtonRotateCW", "ToolButtonViewerTrash",
		"ToolButtonUIModeBrowser" };
	if (NULL != pQuiverImpl && NULL != pQuiverImpl->m_pBuilder)
	{
		show_ui_items(pQuiverImpl->m_pBuilder, viewer_items, G_N_ELEMENTS(viewer_items), bShow);
	}
}

void QuiverImpl::ShowBrowserUIItems(QuiverImpl *pQuiverImpl, bool bShow)
{
	static const char *browser_items[] = {
		"MenuItemBrowserOpenLocation", "MenuItemBrowserCopy", "MenuItemBrowserTrash",
		"MenuItemBrowserHistoryBack", "MenuItemBrowserHistoryForward",
		"MenuItemGoFolderParent", "MenuItemGoFolderNext", "MenuItemGoFolderPrev",
		"CheckMenuItemBrowserSidebar", "CheckMenuItemBrowserPreview",
		"CheckMenuItemSortDescending",
		"ToolButtonBrowserHistoryBack", "ToolButtonBrowserHistoryForward",
		"ToolButtonBrowserSidebar", "ToolButtonBrowserTrash",
		"ToolButtonUIModeViewer" };
	if (NULL != pQuiverImpl && NULL != pQuiverImpl->m_pBuilder)
	{
		show_ui_items(pQuiverImpl->m_pBuilder, browser_items, G_N_ELEMENTS(browser_items), bShow);
	}
}

void Quiver::ShowViewer()
{
	m_QuiverImplPtr->m_BrowserPtr->Hide();
	m_QuiverImplPtr->m_ViewerPtr->Show();

	m_QuiverImplPtr->m_bViewerMode = true;
	QuiverImpl::ShowViewerUIItems(m_QuiverImplPtr.get(), true);
	QuiverImpl::ShowBrowserUIItems(m_QuiverImplPtr.get(), false);

	m_QuiverImplPtr->m_ViewerPtr->GrabFocus();
}

void Quiver::ShowBrowser()
{
	m_QuiverImplPtr->m_ViewerPtr->Hide();
	m_QuiverImplPtr->m_BrowserPtr->Show();

	m_QuiverImplPtr->m_bViewerMode = false;
	QuiverImpl::ShowViewerUIItems(m_QuiverImplPtr.get(), false);
	QuiverImpl::ShowBrowserUIItems(m_QuiverImplPtr.get(), true);

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

	bool bInSlideShow = QuiverUtils::ToggleActionGetActive(ACTION_QUIVER_SLIDESHOW);

	if (bInSlideShow)
	{
		QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_MENUBAR, FALSE);
		QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_TOOLBAR_MAIN, FALSE);
		QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_STATUSBAR, FALSE);
		QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_PROPERTIES, FALSE);
	}
	else
	{
		bool bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PROPS_SHOW);
		if (bShow)
		{
			QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_PROPERTIES, TRUE);
		}

		if (GDK_WINDOW_STATE_FULLSCREEN & m_WindowState)
		{
			bool bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW_FS, false);
			QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_TOOLBAR_MAIN, bShow);

			bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW_FS, false);
			QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_MENUBAR, bShow);
			
			bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW_FS, false);
			QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_STATUSBAR, bShow);
		}
		else
		{
			prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN, false);
			m_bSlideShowRestoreFromFS = false;
			// show widgets
			bool bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW);
			QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_TOOLBAR_MAIN, bShow);
			
			bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW);
			QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_MENUBAR, bShow);
			
			bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW);
			QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_STATUSBAR, bShow);
		}
	}
}


void QuiverImpl::BrowserEventHandler::HandleSelectionChanged(BrowserEventPtr event_ptr)
{ (void)event_ptr; 
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
	g_snprintf(status_text, 256, "%lu items selected (%llu bytes)",(unsigned long)selection.size(), (unsigned long long)total_size);

	//parent->m_StatusbarPtr->SetText(status_text);
}

void QuiverImpl::BrowserEventHandler::HandleItemActivated(BrowserEventPtr event_ptr)
{ (void)event_ptr; 
	if (0 != parent->m_ImageListPtr->GetSize() && parent->m_ImageListPtr->GetCurrent().IsFolder())
	{
	    list<string> file_list;
		string currentItem = parent->m_BrowserPtr->GetCurrentFolderChild();
	    file_list.push_back(parent->m_ImageListPtr->GetCurrent().GetURI());
		parent->m_ImageListPtr->SetImageList(&file_list);
		parent->m_ImageListPtr->SetCurrentFile(currentItem);
	}
	else
	{
		parent->m_pQuiver->ShowViewer();
	}
}

void QuiverImpl::BrowserEventHandler::HandleCursorChanged(BrowserEventPtr event_ptr)
{ (void)event_ptr; 
	parent->m_pQuiver->ImageChanged();
}

void QuiverImpl::ViewerEventHandler::HandleItemActivated(ViewerEventPtr event_ptr)
{ (void)event_ptr; 
	parent->m_pQuiver->ShowBrowser();
}

void QuiverImpl::ViewerEventHandler::HandleItemClicked(ViewerEventPtr event_ptr)
{ (void)event_ptr; 
	parent->m_pQuiver->OnFullScreen();
}

void QuiverImpl::ViewerEventHandler::HandleCursorChanged(ViewerEventPtr event_ptr)
{ (void)event_ptr; 
	parent->m_pQuiver->ImageChanged();
}


void QuiverImpl::ViewerEventHandler::HandleSlideShowStarted(ViewerEventPtr event_ptr)
{ (void)event_ptr; 
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
{ (void)event_ptr; 
	// return from FS if necessary
	if (parent->m_bSlideShowRestoreFromFS)
	{
		parent->m_pQuiver->OnFullScreen();
	}

	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_SLIDESHOW, FALSE);

	parent->UpdateUI();

	// stop the timer that keeps the display on
	if (0 != parent->m_iTimeoutKeepScreenOn)
	{
		g_source_remove(parent->m_iTimeoutKeepScreenOn);
		parent->m_iTimeoutKeepScreenOn = 0;
	}
}


void QuiverImpl::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event)
{ (void)event; 

}

void QuiverImpl::BookmarksEventHandler::HandleBookmarkChanged(BookmarksEventPtr event)
{ (void)event; 
	parent->LoadBookmarks();
}

void QuiverImpl::ExternalToolsEventHandler::HandleExternalToolChanged(ExternalToolsEventPtr event)
{ (void)event; 
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
			QuiverUtils::SetRadioActionCurrent(ACTION_QUIVER_SORT_BY_NAME_NATURAL, ImageList::SORT_BY_RANDOM);
			m_QuiverImplPtr->m_ImageListPtr->SetCurrentIndex(0);
		}

		ShowViewer();
		m_QuiverImplPtr->m_ViewerPtr->SlideShowStart();
	}
	else
	{
		PreferencesPtr prefsPtr = Preferences::GetInstance();
		{
			int sortby = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_SORT_BY,ImageList::SORT_BY_FILENAME_NATURAL);
			QuiverUtils::SetRadioActionCurrent(ACTION_QUIVER_SORT_BY_NAME_NATURAL, sortby);
		}

		m_QuiverImplPtr->m_ViewerPtr->SlideShowStop();
	}
}

void Quiver::OnShowToolbar(bool bShow)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();

	bool bInSlideShow = QuiverUtils::ToggleActionGetActive(ACTION_QUIVER_SLIDESHOW);

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
	bool bInSlideShow = QuiverUtils::ToggleActionGetActive(ACTION_QUIVER_SLIDESHOW);

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

	bool bInSlideShow = QuiverUtils::ToggleActionGetActive(ACTION_QUIVER_SLIDESHOW);

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

static void quiver_escape_action(QuiverImpl *pQuiverImpl)
{
	// this action will do one of the following

	Quiver *pQuiver = pQuiverImpl->m_pQuiver;
	bool bDoneSomething = false;

	if (GDK_WINDOW_STATE_FULLSCREEN & pQuiverImpl->m_WindowState)
	{
		// 3. if in browser and fullscreen, return to unfullscreen mode
		pQuiver->OnFullScreen();
		bDoneSomething = true;
	}

	if (!bDoneSomething && pQuiverImpl->m_bViewerMode)
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

static void quiver_new_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer data)
{ (void)parameter; 
	QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
	Quiver *pQuiver;
	pQuiver = pQuiverImpl->m_pQuiver;
	
	const gchar * szAction = g_action_get_name(G_ACTION(action));

	//printf("quiver_new_action_handler_cb: %s\n",szAction);

	if (0 == strcmp(szAction,ACTION_QUIVER_CLOSE) 
	    || 0 == strcmp(szAction,ACTION_QUIVER_CLOSE_2) 
	    || 0 == strcmp(szAction,ACTION_QUIVER_CLOSE_3) 
	    || 0 == strcmp(szAction,ACTION_QUIVER_CLOSE_4))
	{
		pQuiver->OnQuit();
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_ESCAPE) )
	{
		quiver_escape_action(pQuiverImpl);
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
		pQuiver->OnShowProperties(QuiverUtils::ToggleActionGetActive(szAction));
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_VIEW_TOOLBAR_MAIN))
	{
		pQuiver->OnShowToolbar(QuiverUtils::ToggleActionGetActive(szAction));
	}
	else if (0 == strcmp(szAction,ACTION_QUIVER_VIEW_MENUBAR))
	{
		pQuiver->OnShowMenubar(QuiverUtils::ToggleActionGetActive(szAction));
	}
	else if (0 == strcmp(szAction, ACTION_QUIVER_VIEW_STATUSBAR))
	{
		pQuiver->OnShowStatusbar(QuiverUtils::ToggleActionGetActive(szAction));
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
		if (!pQuiverImpl->m_bViewerMode)
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
		pQuiver->OnSlideShow(QuiverUtils::ToggleActionGetActive(szAction));
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
	else if(0 == strcmp(szAction,ACTION_QUIVER_SORT_BY_NAME)
	     || 0 == strcmp(szAction,ACTION_QUIVER_SORT_BY_NAME_NATURAL)
	     || 0 == strcmp(szAction,ACTION_QUIVER_SORT_BY_DATE)
	     || 0 == strcmp(szAction,ACTION_QUIVER_SORT_BY_DATE_MODIFIED)
	     || 0 == strcmp(szAction,ACTION_QUIVER_SORT_BY_RANDOM))
	{
		bool bAsc = ( FALSE == QuiverUtils::ToggleActionGetActive(ACTION_QUIVER_SORT_DESCENDING) );

		gint sortby = QuiverUtils::GetRadioActionCurrent(szAction);
		pQuiverImpl->m_ImageListPtr->Sort((ImageList::SortBy)sortby,bAsc);

		bool bInSlideShow = QuiverUtils::ToggleActionGetActive(ACTION_QUIVER_SLIDESHOW);

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

		bool bDec = QuiverUtils::ToggleActionGetActive(ACTION_QUIVER_SORT_DESCENDING);
		prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_SORT_REVERSED,bDec);
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_GO_FOLDER_PARENT))
	{
		list<string> folders;
		folders = pQuiverImpl->m_ImageListPtr->GetFolderList();
		if (!folders.empty())
		{
			std::string lastFolder = folders.back();

			GFile* dir = g_file_new_for_uri(lastFolder.c_str());
			GFile* parent = g_file_get_parent(dir);
			if (NULL != parent)
			{
				char* uri = g_file_get_uri(parent);
				pQuiverImpl->m_ImageListPtr->SetImageList(uri);
				pQuiverImpl->m_ImageListPtr->SetCurrentFile(lastFolder);
				g_free(uri);
				g_object_unref(parent);
			}
			g_object_unref(dir);
		}
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_GO_FOLDER_NEXT))
	{
		list<string> folders;
		folders = pQuiverImpl->m_ImageListPtr->GetFolderList();
		if (!folders.empty())
		{
			GFile* folder = g_file_new_for_uri(folders.back().c_str());
			GFile* parent = g_file_get_parent(folder);
			std::string lastFolder = folders.back();
			ImageListPtr tmpListPtr(new ImageList());
			char* uri = g_file_get_uri(parent);
			tmpListPtr->SetImageList(uri);
			g_free(uri);
			g_object_unref(parent);
			g_object_unref(folder);

			bool bFoundCurrent = false;
			bool bFoundNext  = false;
			string newFolder;
			for (guint i = 0; i < tmpListPtr->GetSize(); ++i)
			{
				QuiverFile f = (*tmpListPtr)[i];
				if (f.IsFolder())
				{
					newFolder = f.GetURI();
					if (newFolder == lastFolder)
						bFoundCurrent = true;
					else if (bFoundCurrent)
					{
						bFoundNext = true;
						break;
					}

				}
			}

			if (bFoundNext)
				pQuiverImpl->m_ImageListPtr->SetImageList(newFolder);
		}
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_GO_FOLDER_PREV))
	{
		list<string> folders;
		folders = pQuiverImpl->m_ImageListPtr->GetFolderList();
		if (!folders.empty())
		{
			GFile* folder = g_file_new_for_uri(folders.front().c_str());
			GFile* parent = g_file_get_parent(folder);
			std::string lastFolder = folders.back();
			ImageListPtr tmpListPtr(new ImageList());
			char* uri = g_file_get_uri(parent);
			tmpListPtr->SetImageList(uri);
			g_free(uri);
			g_object_unref(parent);
			g_object_unref(folder);

			string newFolder;
			for (guint i = 0; i < tmpListPtr->GetSize(); ++i)
			{
				QuiverFile f = (*tmpListPtr)[i];
				if (f.IsFolder())
				{
					string tmp = f.GetURI();
					if (tmp == lastFolder)
						break;
					newFolder = tmp;
				}
			}

			if (!newFolder.empty())
				pQuiverImpl->m_ImageListPtr->SetImageList(newFolder);
		}
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
			renameTaskPtr->SetTemplate( dlg.GetTemplate() );

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
			organizeTaskPtr->SetFolderTemplate( dlg.GetFolderTemplate() );
			organizeTaskPtr->SetFileTemplate( dlg.GetFileTemplate() );
			organizeTaskPtr->SetRenameFiles( dlg.GetRenameFiles() );
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

				if (dlg.ModifyModificationTime())
				{
					adjustDateTaskPtr->AddAdjustDateFields(AdjustDateTask::DATE_FIELD_MODIFICATION_TIME);
				}

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

			bool bInViewer = pQuiverImpl->m_bViewerMode;
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
					bookmark_name = g_strdup_printf("%s (and %d other folders)", strBaseName.c_str(), (int)(folders.size()-1));
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
{ (void)widget; 
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
{ (void)event; 
	parent->m_pQuiver->ImageChanged();
}
void QuiverImpl::ImageListEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event) 
{ (void)event; 
	parent->m_pQuiver->ImageChanged();
}
void QuiverImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr event)
{ (void)event; 
	parent->m_pQuiver->ImageChanged();
}
void QuiverImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr event)
{ (void)event; 
	parent->m_pQuiver->ImageChanged();
}
void QuiverImpl::ImageListEventHandler::HandleItemChanged(ImageListEventPtr event)
{ (void)event; 
	parent->m_pQuiver->ImageChanged();
}

