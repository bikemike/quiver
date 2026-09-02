#include <config.h>

#include <gst/gst.h>

#include "Quiver.h"

GtkApplication *g_pApp = NULL;

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

#include "ImageListFilter.h"
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

using namespace std;

// GTK4 compat: GdkWindowState removed in GTK4; track fullscreen via a bool.
typedef guint GdkWindowState;
#define GDK_WINDOW_STATE_FULLSCREEN  ((GdkWindowState)1u)
#define GDK_WINDOW_STATE_WITHDRAWN   ((GdkWindowState)2u)

// helper functions


static void quiver_new_action_handler_cb(GSimpleAction *action, GVariant *parameter, gpointer data);
static void quiver_escape_action(QuiverImpl *pQuiverImpl);

static gboolean timeout_event_motion_notify (gpointer data);


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
	PropertyView m_PropertyView;
	
	StatusbarPtr m_StatusbarPtr;

	BookmarksPtr m_BookmarksPtr;
	ExternalToolsPtr m_ExternalToolsPtr;

	GtkWidget *m_pQuiverWindow;

	GtkWidget *m_pMenubar;
	GtkWidget *m_pToolbar;
	GtkWidget *m_pNBProperties;
	GtkWidget* m_pHPanedMainArea;
	
	bool m_bSlideShowRestoreFromFS;
	bool m_bFilmStripVisibleBeforeFS;
			
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

	// drag/drop targets – removed in GTK4; keep enum for reference
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
	
	GMenu *m_pBookmarkMenu;
	GMenu *m_pExternalToolsMenu;
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

	m_pBookmarkMenu = NULL;
	m_pExternalToolsMenu = NULL;

	// add ignored extensions
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	std::list<std::string> exts = prefsPtr->GetStringList(QUIVER_PREFS_APP, QUIVER_PREFS_APP_IGNORED_EXTENSIONS);
	for (std::list<std::string>::iterator itr = exts.begin();
			exts.end() != itr; ++itr)
	{
		m_ImageListPtr->AddIgnoredExtension(*itr);
	}
}
QuiverImpl::~QuiverImpl()
{
	m_BookmarksPtr->RemoveEventHandler(m_BookmarksEventHandler);
	m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandler);	

	/* Sub-object destructors must run while the widget tree is still alive
	 * (some of them, e.g. the browser thumb-sizer, unparent widgets before
	 * releasing their own reference, and the tree would double-free those).
	 * The manual unrefs of tree-owned widgets have been removed from those
	 * destructors, so tearing the sub-objects down first is now safe; the
	 * window destroy at the end frees whatever is still parented. */
	m_ImageListPtr.reset();
	m_BrowserPtr.reset();
	m_ViewerPtr.reset();
	m_StatusbarPtr.reset();

	gtk_window_destroy(GTK_WINDOW(m_pQuiverWindow));
}

void QuiverImpl::LoadBookmarks()
{
	if (NULL == m_pBookmarkMenu)
	{
		return;
	}

	// Remove all dynamic bookmark items (keep first 3: Add, Edit, Separator)
	// GMenu doesn't support removing by position easily; rebuild the whole menu
	{
		gint remaining;
		while ((remaining = g_menu_model_get_n_items(G_MENU_MODEL(m_pBookmarkMenu))) > 3)
		{
			g_menu_remove(m_pBookmarkMenu, 3);
		}
	}

	vector<Bookmark> bookmarks = m_BookmarksPtr->GetBookmarks();
	for (unsigned int i = 0; i < bookmarks.size(); ++i)
	{
		stringstream ss;
		ss << "Bookmark_" << bookmarks[i].GetID();
		string name = ss.str();

		QuiverUtils::RemoveAction(name.c_str());
		QuiverUtils::AddSimpleAction(name.c_str(), "", quiver_new_action_handler_cb, this);

		string full_name = "quiver." + name;
		GMenuItem *item = g_menu_item_new(bookmarks[i].GetName().c_str(), full_name.c_str());
		g_menu_append_item(m_pBookmarkMenu, item);
		g_object_unref(item);
	}
}

void QuiverImpl::LoadExternalTools()
{
	if (NULL == m_pExternalToolsMenu)
	{
		return;
	}

	// Remove all dynamic external tool items (keep first 4: AdjustDate, Rename, Organize, ExternalTools, Separator = 5 items)
	{
		gint remaining;
		while ((remaining = g_menu_model_get_n_items(G_MENU_MODEL(m_pExternalToolsMenu))) > 5)
		{
			g_menu_remove(m_pExternalToolsMenu, 5);
		}
	}

	vector<ExternalTool> externaltools = m_ExternalToolsPtr->GetExternalTools();
	for (unsigned int i = 0; i < externaltools.size(); ++i)
	{
		stringstream ss;
		ss << "ExternalTool_" << externaltools[i].GetID();
		string name = ss.str();

		QuiverUtils::RemoveAction(name.c_str());
		QuiverUtils::AddSimpleAction(name.c_str(), "", quiver_new_action_handler_cb, this);

		string full_name = "quiver." + name;
		GMenuItem *item = g_menu_item_new(externaltools[i].GetName().c_str(), full_name.c_str());
		g_menu_append_item(m_pExternalToolsMenu, item);
		g_object_unref(item);
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
	GtkFileDialog* dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, "Save As");
	gtk_file_dialog_set_initial_name(dialog, m_CurrentQuiverFile.GetFileName().c_str());

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	GFile *result_file = NULL;
	g_object_ref(dialog);
	g_object_set_data(G_OBJECT(dialog), "loop", loop);
	g_object_set_data(G_OBJECT(dialog), "result", &result_file);

	gtk_file_dialog_save(dialog, GTK_WINDOW(m_pQuiverWindow), NULL,
		GAsyncReadyCallback(+[](GObject *source, GAsyncResult *res, gpointer data) {
			GFile **out = (GFile**)data;
			*out = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, NULL);
			GMainLoop *l = (GMainLoop*)g_object_get_data(source, "loop");
			g_main_loop_quit(l);
		}), &result_file);

	g_main_loop_run(loop);
	g_main_loop_unref(loop);

	if (result_file)
	{
		char* filename = g_file_get_path(result_file);
		ImageSaveManager::GetInstance()->SaveImageAs(m_CurrentQuiverFile, filename);
		g_free(filename);
		g_object_unref(result_file);
	}

	g_object_unref(dialog);
}

bool QuiverImpl::CanClose()
{
	if (!gtk_window_is_fullscreen(GTK_WINDOW(m_pQuiverWindow)))
	{
		gtk_window_get_default_size(GTK_WINDOW(m_pQuiverWindow), &m_iAppWidth, &m_iAppHeight);
	}
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
#define ACTION_VIEWER_VIEW_FILM_STRIP                        "ViewFilmStrip"
#define ACTION_QUIVER_CLOSE_2                                ACTION_QUIVER_CLOSE"_2"
#define ACTION_QUIVER_CLOSE_3                                ACTION_QUIVER_CLOSE"_3"
#define ACTION_QUIVER_CLOSE_4                                ACTION_QUIVER_CLOSE"_4"

/* GMenu-based menu definition for GtkPopoverMenuBar (replaces GTK3 GtkMenuBar XML) */
static GMenu* quiver_build_app_menu(GMenu *bookmarkMenu, GMenu *externalToolsMenu)
{
	GMenu *menu = g_menu_new();

	// === File ===
	GMenu *fileMenu = g_menu_new();
	g_menu_append(fileMenu, "_Open", "quiver.FileOpen");
	g_menu_append(fileMenu, "Open _Folder", "quiver.FileOpenFolder");
	g_menu_append(fileMenu, "Open _Location", "quiver.BrowserOpenLocation");
	g_menu_append_section(fileMenu, NULL, NULL);
	g_menu_append(fileMenu, "_Save", "quiver.Save");
	g_menu_append(fileMenu, "Save _As", "quiver.SaveAs");
	g_menu_append_section(fileMenu, NULL, NULL);
	g_menu_append(fileMenu, "_Close", "quiver.Close");
	g_menu_append_section(fileMenu, NULL, NULL);
	g_menu_append(fileMenu, "_Quit", "quiver.Close_3");
	g_menu_append_submenu(menu, "_File", G_MENU_MODEL(fileMenu));
	g_object_unref(fileMenu);

	// === Edit ===
	GMenu *editMenu = g_menu_new();
	g_menu_append(editMenu, "_Copy (Browser)", "quiver.BrowserCopy");
	g_menu_append(editMenu, "_Copy (Viewer)", "quiver.ViewerCopy");
	g_menu_append_section(editMenu, NULL, NULL);
	g_menu_append(editMenu, "_Move To Trash (Browser)", "quiver.BrowserTrash");
	g_menu_append(editMenu, "_Move To Trash (Viewer)", "quiver.ViewerTrash");
	g_menu_append_section(editMenu, NULL, NULL);
	g_menu_append(editMenu, "_Preferences", "quiver.Preferences");
	g_menu_append_submenu(menu, "_Edit", G_MENU_MODEL(editMenu));
	g_object_unref(editMenu);

	// === View ===
	GMenu *viewMenu = g_menu_new();
	g_menu_append(viewMenu, "_Viewer", "quiver.UIModeViewer");
	g_menu_append(viewMenu, "_Browser", "quiver.UIModeBrowser");
	g_menu_append_section(viewMenu, NULL, NULL);
	{
		GMenuItem *item;
		item = g_menu_item_new("Menubar", "quiver.ViewMenubar");
		g_menu_append_item(viewMenu, item);
		g_object_unref(item);
		item = g_menu_item_new("Toolbar", "quiver.ViewToolbarMain");
		g_menu_append_item(viewMenu, item);
		g_object_unref(item);
		item = g_menu_item_new("Properties", "quiver.ViewProperties");
		g_menu_append_item(viewMenu, item);
		g_object_unref(item);
		item = g_menu_item_new("Statusbar", "quiver.ViewStatusbar");
		g_menu_append_item(viewMenu, item);
		g_object_unref(item);
	}
	g_menu_append_section(viewMenu, NULL, NULL);
	g_menu_append(viewMenu, "Sidebar", "quiver.BrowserViewSidebar");
	g_menu_append(viewMenu, "Preview", "quiver.BrowserViewPreview");
	g_menu_append(viewMenu, "Film Strip", "quiver.ViewFilmStrip");
	g_menu_append_section(viewMenu, NULL, NULL);

	// Sort submenu
	GMenu *sortMenu = g_menu_new();
	{
		GMenuItem *item;
		const char *sortNames[] = {"By _Name", "By _Name (natural order)", "By _Date", "By Date _Modified", "_Randomize"};
		const char *sortActions[] = {"quiver.SortByName", "quiver.SortByNameNatural", "quiver.SortByDate", "quiver.SortByDateModified", "quiver.SortByRandom"};
		for (int i = 0; i < 5; i++) {
			item = g_menu_item_new(sortNames[i], sortActions[i]);
			g_menu_append_item(sortMenu, item);
			g_object_unref(item);
		}
		item = g_menu_item_new("In _Descending Order", "quiver.SortDescending");
		g_menu_append_item(sortMenu, item);
		g_object_unref(item);
	}
	g_menu_append_submenu(viewMenu, "_Arrange Items", G_MENU_MODEL(sortMenu));
	g_object_unref(sortMenu);

	g_menu_append_section(viewMenu, NULL, NULL);
	g_menu_append(viewMenu, "_Full Screen", "quiver.FullScreen");
	g_menu_append(viewMenu, "_Slide Show", "quiver.SlideShow");
	g_menu_append_section(viewMenu, NULL, NULL);

	// Zoom submenu
	GMenu *zoomMenu = g_menu_new();
	{
		GMenuItem *item;
		const char *zNames[] = {"Zoom _Fit", "Zoom Fit _Stretch", "_Actual Size", "_Fill Screen"};
		const char *zActions[] = {"quiver.ZoomFit", "quiver.ZoomFitStretch", "quiver.Zoom100", "quiver.ZoomFillScreen"};
		for (int i = 0; i < 4; i++) {
			item = g_menu_item_new(zNames[i], zActions[i]);
			g_menu_append_item(zoomMenu, item);
			g_object_unref(item);
		}
		item = g_menu_item_new("Rotate to _Maximize View", "quiver.MaximizeForDisplay");
		g_menu_append_item(zoomMenu, item);
		g_object_unref(item);
	}
	g_menu_append_submenu(viewMenu, "Zoom", G_MENU_MODEL(zoomMenu));
	g_object_unref(zoomMenu);

	g_menu_append_submenu(menu, "_View", G_MENU_MODEL(viewMenu));
	g_object_unref(viewMenu);

	// === Image ===
	GMenu *imageMenu = g_menu_new();
	g_menu_append(imageMenu, "_Rotate Clockwise", "quiver.RotateCW");
	g_menu_append(imageMenu, "Rotate _Counterclockwise", "quiver.RotateCCW");
	g_menu_append_section(imageMenu, NULL, NULL);
	g_menu_append(imageMenu, "Flip _Horizontally", "quiver.FlipH");
	g_menu_append(imageMenu, "Flip _Vertically", "quiver.FlipV");
	g_menu_append_submenu(menu, "_Image", G_MENU_MODEL(imageMenu));
	g_object_unref(imageMenu);

	// === Video ===
	GMenu *videoMenu = g_menu_new();
	g_menu_append(videoMenu, "_Play / Pause", "quiver.VideoPlay");
	g_menu_append_section(videoMenu, NULL, NULL);
	g_menu_append(videoMenu, "Seek _Back 5s", "quiver.VideoSeekBack5");
	g_menu_append(videoMenu, "Seek _Forward 5s", "quiver.VideoSeekFwd5");
	g_menu_append_section(videoMenu, NULL, NULL);
	g_menu_append(videoMenu, "Skip Back _10s", "quiver.VideoSkipBack");
	g_menu_append(videoMenu, "Skip Forward 1_0s", "quiver.VideoSkipForward");
	g_menu_append_section(videoMenu, NULL, NULL);
	g_menu_append(videoMenu, "Previous Frame", "quiver.VideoFrameBack");
	g_menu_append(videoMenu, "Next Frame", "quiver.VideoFrameFwd");
	g_menu_append_section(videoMenu, NULL, NULL);

	// Speed submenu
	GMenu *speedMenu = g_menu_new();
	{
		const char *speeds[] = {"0.25x", "0.5x", "1.0x (Normal)", "1.5x", "2.0x", "4.0x", "8.0x", "16.0x"};
		const char *speedActions[] = {"quiver.VideoSpeed025", "quiver.VideoSpeed05", "quiver.VideoSpeed10", "quiver.VideoSpeed15", "quiver.VideoSpeed20", "quiver.VideoSpeed40", "quiver.VideoSpeed80", "quiver.VideoSpeed160"};
		for (int i = 0; i < 8; i++) {
			g_menu_append(speedMenu, speeds[i], speedActions[i]);
		}
	}
	g_menu_append_submenu(videoMenu, "Playback _Speed", G_MENU_MODEL(speedMenu));
	g_object_unref(speedMenu);
	g_menu_append_section(videoMenu, NULL, NULL);
	g_menu_append(videoMenu, "_Take Snapshot", "quiver.VideoSnapshot");
	g_menu_append_submenu(menu, "V_ideo", G_MENU_MODEL(videoMenu));
	g_object_unref(videoMenu);

	// === Go ===
	GMenu *goMenu = g_menu_new();
	g_menu_append(goMenu, "_First Image", "quiver.ImageFirst");
	g_menu_append(goMenu, "_Previous Image", "quiver.ImagePrevious");
	g_menu_append(goMenu, "_Next Image", "quiver.ImageNext");
	g_menu_append(goMenu, "_Last Image", "quiver.ImageLast");
	g_menu_append_section(goMenu, NULL, NULL);
	g_menu_append(goMenu, "Go _Back", "quiver.BrowserHistoryBack");
	g_menu_append(goMenu, "Go _Forward", "quiver.BrowserHistoryForward");
	g_menu_append_section(goMenu, NULL, NULL);
	g_menu_append(goMenu, "Open _Parent", "quiver.GoFolderParent");
	g_menu_append(goMenu, "Open _Next Folder", "quiver.GoFolderNext");
	g_menu_append(goMenu, "Open _Previous Folder", "quiver.GoFolderPrev");
	g_menu_append_section(goMenu, NULL, NULL);
	g_menu_append_submenu(menu, "_Go", G_MENU_MODEL(goMenu));
	g_object_unref(goMenu);

	// === Bookmarks ===
	// bookmarkMenu is passed in from QuiverImpl for dynamic bookmark items
	g_menu_append(bookmarkMenu, "_Add Bookmark", "quiver.BookmarksAdd");
	g_menu_append(bookmarkMenu, "_Edit Bookmarks...", "quiver.BookmarksEdit");
	g_menu_append_section(bookmarkMenu, NULL, NULL);
	g_menu_append_submenu(menu, "_Bookmarks", G_MENU_MODEL(bookmarkMenu));

	// === Tools ===
	// externalToolsMenu is passed in from QuiverImpl
	g_menu_append(externalToolsMenu, "Adjust Date...", "quiver.AdjustDate");
	g_menu_append(externalToolsMenu, "Rename...", "quiver.Rename");
	g_menu_append(externalToolsMenu, "Organize...", "quiver.Organize");
	g_menu_append(externalToolsMenu, "External Tools...", "quiver.ExternalTools");
	g_menu_append_section(externalToolsMenu, NULL, NULL);
	g_menu_append_submenu(menu, "_Tools", G_MENU_MODEL(externalToolsMenu));

	// === Help ===
	GMenu *helpMenu = g_menu_new();
	g_menu_append(helpMenu, "_Donate...", "quiver.Donate");
	g_menu_append_section(helpMenu, NULL, NULL);
	g_menu_append(helpMenu, "_About", "quiver.About");
	g_menu_append_submenu(menu, "_Help", G_MENU_MODEL(helpMenu));
	g_object_unref(helpMenu);

	return menu;
}

/* drag-and-drop target table removed – GTK4 uses GtkDropTarget */

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
  (void)widget; (void)context; (void)data;
}
void Quiver::SignalDragDataGet (GtkWidget *widget, GdkDragContext *context, 
	GtkSelectionData *selection_data, guint info, guint time,gpointer data)
{
	if (info == QUIVER_TARGET_STRING)
    {
		if (m_QuiverImplPtr->m_ImageListPtr->GetSize())
		{
    		gtk_selection_data_set (selection_data,
			    selection_data->target,
			    8, (const guchar*)m_QuiverImplPtr->m_ImageListPtr->GetCurrent().GetURI(),strlen(m_QuiverImplPtr->m_ImageListPtr->GetCurrent().GetURI()));
		}
	}
	else if (info == QUIVER_TARGET_URI)
	{
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
	gtk_window_set_title (GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow), title.c_str());
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
		m_QuiverImplPtr->m_PropertyView.SetQuiverFile(f);
		m_QuiverImplPtr->m_StatusbarPtr->SetQuiverFile(f);
		
	}
	else
	{
		m_QuiverImplPtr->m_StatusbarPtr->SetPosition(0,0);
		QuiverFile f;
		m_QuiverImplPtr->m_StatusbarPtr->SetQuiverFile(f);
	}
}

static gboolean event_window_state( GObject *obj, GParamSpec *pspec, gpointer data )
{ (void)obj; (void)pspec;
	QuiverImpl *pQuiverImpl = (QuiverImpl*)data;
	GtkWindow *window = GTK_WINDOW(pQuiverImpl->m_pQuiverWindow);
	PreferencesPtr prefsPtr = Preferences::GetInstance();

	gboolean bFullscreen = FALSE;
	pQuiverImpl->m_WindowState = gtk_window_is_fullscreen(window)
		? GDK_WINDOW_STATE_FULLSCREEN : 0;

	pQuiverImpl->UpdateUI();

	if (GDK_WINDOW_STATE_FULLSCREEN & pQuiverImpl->m_WindowState)
	{
		prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN, true);

		pQuiverImpl->m_bTimeoutEventMotionNotifyRunning = true;
		g_timeout_add(1500, timeout_event_motion_notify,pQuiverImpl);

		/* hide the filmstrip on fullscreen (if preference says so) */
		if (pQuiverImpl->m_bViewerMode)
		{
			bool bHideFS = pQuiverImpl->m_ViewerPtr->IsHideFilmstripFS();
			bool bFilmstripActive =
				QuiverUtils::ToggleActionGetActive(ACTION_VIEWER_VIEW_FILM_STRIP);
			pQuiverImpl->m_bFilmStripVisibleBeforeFS = bFilmstripActive;
			if (bFilmstripActive && bHideFS)
			{
				pQuiverImpl->m_ViewerPtr->SetFilmstripHiddenByFS(true);
				pQuiverImpl->m_ViewerPtr->CancelFilmstripHide();
				gtk_widget_set_visible(pQuiverImpl->m_ViewerPtr->GetFilmstripWidget(), FALSE);
			}
		}
		
		bFullscreen = TRUE;
	}
	else
	{
		prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN, false);
		pQuiverImpl->m_bSlideShowRestoreFromFS = false;

		/* restore the filmstrip if we hid it for fullscreen */
		if (pQuiverImpl->m_bViewerMode && pQuiverImpl->m_bFilmStripVisibleBeforeFS)
		{
			pQuiverImpl->m_ViewerPtr->SetFilmstripHiddenByFS(false);
			if (QuiverUtils::ToggleActionGetActive(ACTION_VIEWER_VIEW_FILM_STRIP))
			{
				if (pQuiverImpl->m_ViewerPtr->IsFilmstripOverlay())
					pQuiverImpl->m_ViewerPtr->ShowFilmstripOverlay();
			}
		}
	}
	
	// update the fullscreen toggle state without running the activate
	// callback (OnFullScreen would re-toggle the window state)
	QuiverUtils::ToggleActionSetState(ACTION_QUIVER_FULLSCREEN, bFullscreen);

	return FALSE;
}

gboolean Quiver::event_close_request( GtkWindow *window, gpointer data )
{
	return ((Quiver*)data)->EventCloseRequest(window, data);
}


void Quiver::Close()
{
	if (m_bClosing)
	{
		return;
	}
	m_bClosing = true;
	// Defer the teardown to an idle callback: Close() is often reached from a
	// keyboard accelerator (e.g. "q" via gtk_accel_groups_activate), and
	// destroying the window inside the accel-group dispatch would leave GTK
	// walking freed accel-group state (SIGSEGV in gtk_accel_groups_activate).
	g_idle_add(close_idle_cb, this);
}

gboolean Quiver::close_idle_cb(gpointer data)
{
	((Quiver*)data)->CloseReal();
	return FALSE;
}

void Quiver::CloseReal()
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
	
	g_application_quit(G_APPLICATION(g_pApp));
	delete this;	
}

gboolean Quiver::EventCloseRequest( GtkWindow *window, gpointer data )
{ (void)data;  (void)window; 
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
	: 	m_QuiverImplPtr(new QuiverImpl(this) ),
		m_bClosing(false)
{
	m_QuiverImplPtr->m_bListImagesRecursive = bRecursive;
	m_QuiverImplPtr->m_listImages = images;
	Init();

}

void Quiver::Init()
{
	m_QuiverImplPtr->m_pBookmarkMenu = NULL;
	m_QuiverImplPtr->m_pExternalToolsMenu = NULL;

	m_QuiverImplPtr->m_bViewerMode = false;

	m_QuiverImplPtr->m_bSlideShowRestoreFromFS = false;
	m_QuiverImplPtr->m_bFilmStripVisibleBeforeFS = false;
	
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
	m_QuiverImplPtr->m_pQuiverWindow = gtk_application_window_new (g_pApp);
	gtk_widget_set_name(m_QuiverImplPtr->m_pQuiverWindow,"Quiver Window");


	if (LoadSettings())
	{	
		//set the size and position of the window
		gtk_window_set_default_size (GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow),m_QuiverImplPtr->m_iAppWidth,m_QuiverImplPtr->m_iAppHeight);

	}
	
	gchar *icon_path = g_build_filename(QUIVER_DATADIR, "icons", "48x48", "quiver-icon-app.png", NULL);
	(void)icon_path;
	gtk_window_set_default_icon_name("quiver-icon-app");
	g_free(icon_path);	

	/* Set up GUI elements */

	/* Build GMenu-based menubar (GTK4 replacement for GtkMenuBar) */
	m_QuiverImplPtr->m_pBookmarkMenu = g_menu_new();
	m_QuiverImplPtr->m_pExternalToolsMenu = g_menu_new();
	GMenu *appMenu = quiver_build_app_menu(m_QuiverImplPtr->m_pBookmarkMenu, m_QuiverImplPtr->m_pExternalToolsMenu);
	m_QuiverImplPtr->m_pMenubar = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(appMenu));
	g_object_unref(appMenu);

	/* Build toolbar as a GtkBox (replaces GtkToolbar) */
	m_QuiverImplPtr->m_pToolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_set_name(m_QuiverImplPtr->m_pToolbar, "Quiver Toolbar");

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
	m_QuiverImplPtr->m_BrowserPtr->SetToolbar(m_QuiverImplPtr->m_pToolbar);

	/* NOTE: BindWidget/BindToggleWidget/BindRadioWidget calls removed.
	 * In GTK4, menu actions are bound via GMenu action names.
	 * Toolbar buttons need to be created programmatically and bound
	 * with gtk_actionable_set_action_name(). TODO: recreate toolbar buttons. */

	m_QuiverImplPtr->m_BrowserPtr->SetStatusbar(m_QuiverImplPtr->m_StatusbarPtr);
	m_QuiverImplPtr->m_ViewerPtr->SetStatusbar(m_QuiverImplPtr->m_StatusbarPtr);

	m_QuiverImplPtr->m_pBuilder = NULL;

    g_signal_connect (G_OBJECT (m_QuiverImplPtr->m_pQuiverWindow), "notify::default-width",
    			G_CALLBACK (event_window_state), m_QuiverImplPtr.get());

    g_signal_connect (G_OBJECT (m_QuiverImplPtr->m_pQuiverWindow), "close-request",
    			G_CALLBACK (Quiver::event_close_request), this);

	/* Track fullscreen state via the notify signal on the 'fullscreened' property */
	g_signal_connect (G_OBJECT (m_QuiverImplPtr->m_pQuiverWindow), "notify::fullscreened",
				G_CALLBACK (event_window_state), m_QuiverImplPtr.get());
				
			


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
	// let the main (browser + viewer) area fill the window vertically instead of
	// leaving empty space that the status bar appears to absorb.
	gtk_widget_set_hexpand(m_QuiverImplPtr->m_pHPanedMainArea, TRUE);
	gtk_widget_set_vexpand(m_QuiverImplPtr->m_pHPanedMainArea, TRUE);
	
	hbox_browser_viewer_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	gtk_widget_set_name(hbox_browser_viewer_container,"Quiver hbox 1");
	// let the browser+viewer container fill the paned's start child area, and
	// pass the extra space through to its (already expanding) children.
	gtk_widget_set_hexpand(hbox_browser_viewer_container, TRUE);
	gtk_widget_set_vexpand(hbox_browser_viewer_container, TRUE);
	m_QuiverImplPtr->m_pNBProperties = gtk_notebook_new();
	gtk_widget_set_name(m_QuiverImplPtr->m_pNBProperties ,"Quiver notebook 1");
	
	bool prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_PROPS_SHOW);

	if (prefs_show)
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pNBProperties, TRUE);
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
	gtk_notebook_append_page(GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties),m_QuiverImplPtr->m_PropertyView.GetWidget(),gtk_label_new("Properties"));
	//gtk_notebook_append_page(GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties),gtk_drawing_area_new(),gtk_label_new("IPTC"));
	//gtk_notebook_append_page(GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties),gtk_drawing_area_new(),gtk_label_new("Database"));
	gtk_notebook_popup_enable(GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties));
	gtk_notebook_set_scrollable (GTK_NOTEBOOK(m_QuiverImplPtr->m_pNBProperties),TRUE);
	
	// statusbar
	statusbar =  m_QuiverImplPtr->m_StatusbarPtr->GetWidget();
	gtk_widget_set_visible(statusbar, FALSE);

	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW, true);
	if (prefs_show)
	{
		gtk_widget_set_visible(statusbar, TRUE);
	}
	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_STATUSBAR, prefs_show);

	// menubar
	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW, true);
	if (prefs_show)
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pMenubar, TRUE);
	}
	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_MENUBAR, prefs_show);
	
	// toolbar
	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW, true);
	if (prefs_show)
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pToolbar, TRUE);
	}
	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_TOOLBAR_MAIN, prefs_show);

	// FIXME: context menu
	// GtkWidget *context_menu = ... 

	// pack the browser and viewer area
	GtkWidget* browser_widget = m_QuiverImplPtr->m_BrowserPtr->GetWidget();
	GtkWidget* viewer_widget = m_QuiverImplPtr->m_ViewerPtr->GetWidget();
	gtk_widget_set_hexpand(browser_widget, TRUE);
	gtk_widget_set_vexpand(browser_widget, TRUE);
	gtk_widget_set_hexpand(viewer_widget, TRUE);
	gtk_widget_set_vexpand(viewer_widget, TRUE);
	gtk_box_append (GTK_BOX (hbox_browser_viewer_container), browser_widget);
	gtk_box_append (GTK_BOX (hbox_browser_viewer_container), viewer_widget);

	// pack the hpaned (main gui area)
	gtk_paned_set_start_child(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea), hbox_browser_viewer_container);
	gtk_paned_set_end_child(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea), m_QuiverImplPtr->m_pNBProperties);
	gtk_paned_set_resize_start_child(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea), TRUE);
	gtk_paned_set_resize_end_child(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea), FALSE);
	gtk_paned_set_shrink_start_child(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea), TRUE);
	gtk_paned_set_shrink_end_child(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea), TRUE);

	int hpaned_pos = prefsPtr->GetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_HPANE_POS, m_QuiverImplPtr->m_iAppWidth/2);
	gtk_paned_set_position(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea),hpaned_pos);

	// pack the main gui area with the rest of the gui components
	gtk_box_append (GTK_BOX (vbox), m_QuiverImplPtr->m_pMenubar);
	gtk_box_append (GTK_BOX (vbox), m_QuiverImplPtr->m_pToolbar);
	gtk_box_append (GTK_BOX (vbox), m_QuiverImplPtr->m_pHPanedMainArea);
	gtk_box_append (GTK_BOX (vbox), statusbar);

	// add the gui elements to the main window
	gtk_window_set_child (GTK_WINDOW (m_QuiverImplPtr->m_pQuiverWindow), vbox);
	
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
	
	gtk_widget_set_visible (GTK_WIDGET(m_QuiverImplPtr->m_pQuiverWindow), TRUE);

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
}

bool Quiver::LoadSettings()
{
	string gtk_rc = g_szConfigDir + string("/gtkrc");
	
	// load the user's theme overrides (gtk_rc_parse is deprecated, so use a CSS provider)
	if (g_file_test (gtk_rc.c_str(), G_FILE_TEST_EXISTS))
	{
		GtkCssProvider* cssProvider = gtk_css_provider_new();
		gtk_css_provider_load_from_path (cssProvider, gtk_rc.c_str());
		gtk_style_context_add_provider_for_display (gdk_display_get_default (),
			GTK_STYLE_PROVIDER (cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
		g_object_unref (cssProvider);
	}

	string strAccelMap = g_szConfigDir + string("/quiver_keys.map");	
	(void)strAccelMap;

	PreferencesPtr prefsPtr = Preferences::GetInstance();

	GdkMonitor *monitor = NULL;
	{
		GdkDisplay *display = gdk_display_get_default();
		GListModel *monitors = gdk_display_get_monitors(display);
		if (g_list_model_get_n_items(monitors) > 0)
		{
			monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
		}
	}
	GdkRectangle screen_geom = {0, 0, 800, 600};
	if (monitor) {
		gdk_monitor_get_geometry(monitor, &screen_geom);
	}

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
	(void)strAccelMap;


	if (GDK_WINDOW_STATE_FULLSCREEN & m_QuiverImplPtr->m_WindowState)
	{
		PreferencesPtr prefsPtr = Preferences::GetInstance();
		prefsPtr->SetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WINDOW_FULLSCREEN, false);
		return;
	}
	
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_LEFT,m_QuiverImplPtr->m_iAppX);
	prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOP,m_QuiverImplPtr->m_iAppY);
	prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_WIDTH,m_QuiverImplPtr->m_iAppWidth);
	prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_HEIGHT,m_QuiverImplPtr->m_iAppHeight);
	prefsPtr->SetInteger(QUIVER_PREFS_APP,QUIVER_PREFS_APP_HPANE_POS,gtk_paned_get_position(GTK_PANED(m_QuiverImplPtr->m_pHPanedMainArea)));
}


void Quiver::SetViewerOrBrowser(std::list<std::string> &files)
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
}

void Quiver::SetImageList(list<string> &files, bool bRecursive /* = false */)
{
	SetViewerOrBrowser(files);

	m_QuiverImplPtr->m_ImageListPtr->SetImageList(&files, bRecursive);
}



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
	if (g_getenv("QUIVER_AUTOCLOSE_MS"))
	{
		g_timeout_add(atoi(g_getenv("QUIVER_AUTOCLOSE_MS")), Quiver::close_idle_cb, pQuiver);
	}
	return FALSE; // run once
}

static CreateQuiverData* g_pCreateData = NULL;

static void on_app_activate(GApplication* app, gpointer data)
{
	(void)app;
	(void)data;
	if (NULL != g_pCreateData)
	{
		CreateQuiver(g_pCreateData);
		delete g_pCreateData;
		g_pCreateData = NULL;
	}
}

int main (int argc, char **argv)
{
	(void)bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	(void)bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	(void)textdomain (GETTEXT_PACKAGE);

 	/* init threads */
	//g_type_init ();

	

	/* Initialize the widget set */
	gtk_init ();

	g_pApp = gtk_application_new("com.github.bikemike.quiver", G_APPLICATION_NON_UNIQUE);
	g_pApp = gtk_application_new("com.github.bikemike.quiver", G_APPLICATION_NON_UNIQUE);
	g_signal_connect(g_pApp, "activate", G_CALLBACK(on_app_activate), NULL);

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
		dir = g_get_home_dir();
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
	g_pCreateData = new CreateQuiverData(cqd);
	
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
                                             
	g_application_run (G_APPLICATION (g_pApp), 0, NULL);


	gst_deinit();
	
	if (g_pApp) g_object_unref(g_pApp);

	

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

	SetViewerOrBrowser(m_QuiverImplPtr->m_listImages);
	m_QuiverImplPtr->m_ImageListPtr->UpdateImageListAsync(&m_QuiverImplPtr->m_listImages,
		m_QuiverImplPtr->m_bListImagesRecursive);

	m_QuiverImplPtr->m_BrowserPtr->SetImageList(m_QuiverImplPtr->m_ImageListPtr);
	// the viewer browses a folder-filtered view of the shared list
	IImageListViewPtr pViewerList(
		new ImageListFilter(m_QuiverImplPtr->m_ImageListPtr,
			[](const QuiverFile& f) { return !f.IsFolder(); }));
	m_QuiverImplPtr->m_ViewerPtr->SetImageList(pViewerList);

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

	/* ── TEMPORARY DEBUG HOOK: automatic close for testing the quit/teardown
	 * path.  Activate with QUIVER_TEST_CLOSE_MS=<milliseconds>.  Remove once
	 * the graceful-exit teardown is verified. ───────────────────────────── */
	const char *pTestCloseMs = g_getenv("QUIVER_TEST_CLOSE_MS");
	if (pTestCloseMs != NULL && atoi(pTestCloseMs) > 0)
	{
		struct QuiverTestCloseCtx { Quiver *pQuiver; guint timeoutId; };
		QuiverTestCloseCtx *ctx = g_new0(QuiverTestCloseCtx, 1);
		ctx->pQuiver = this;
		ctx->timeoutId = g_timeout_add(
			(guint)atoi(pTestCloseMs),
			[](gpointer data) {
				QuiverTestCloseCtx *c = (QuiverTestCloseCtx*)data;
				printf("QUIVER_TEST: auto-close after %u ms, scheduling CloseReal\n", c->timeoutId);
				fflush(stdout);
				((Quiver*)c->pQuiver)->CloseReal();
				g_free(c);
				return FALSE;
			},
			ctx);
	}
	/* ── END TEMPORARY DEBUG HOOK ─────────────────────────────────────── */

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

		gdk_window_set_cursor (pQuiverImpl->m_pQuiverWindow->window, empty_cursor);
		
		g_object_unref(empty_bitmap);
		gdk_cursor_unref (empty_cursor);
		
		//remove the mouse cursor		
		gdk_threads_leave();
		*/
	}
	pQuiverImpl->m_iTimeoutMouseMotionNotify = 0;
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
			}
			else
			{
				gtk_widget_set_visible(widget, FALSE);
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
	}
	else
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pNBProperties, FALSE);
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
	GtkFileDialog* dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, "Open File");

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	GFile *result_file = NULL;
	g_object_ref(dialog);
	g_object_set_data(G_OBJECT(dialog), "loop", loop);

	gtk_file_dialog_open(dialog, GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow), NULL,
		GAsyncReadyCallback(+[](GObject *source, GAsyncResult *res, gpointer data) {
			GFile **out = (GFile**)data;
			*out = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, NULL);
			GMainLoop *l = (GMainLoop*)g_object_get_data(source, "loop");
			g_main_loop_quit(l);
		}), &result_file);

	g_main_loop_run(loop);
	g_main_loop_unref(loop);

	if (result_file)
	{
		char *filename = g_file_get_path(result_file);
		list<string> file_list;
		file_list.push_back(filename);
		m_QuiverImplPtr->m_ImageListPtr->SetImageList(&file_list);
		g_free (filename);
		g_object_unref(result_file);
	}

	g_object_unref(dialog);
}

void Quiver::OnOpenFolder()
{
	GtkFileDialog* dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, "Open Folder");

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	GFile *result_file = NULL;
	g_object_ref(dialog);
	g_object_set_data(G_OBJECT(dialog), "loop", loop);

	gtk_file_dialog_select_folder(dialog, GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow), NULL,
		GAsyncReadyCallback(+[](GObject *source, GAsyncResult *res, gpointer data) {
			GFile **out = (GFile**)data;
			*out = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), res, NULL);
			GMainLoop *l = (GMainLoop*)g_object_get_data(source, "loop");
			g_main_loop_quit(l);
		}), &result_file);

	g_main_loop_run(loop);
	g_main_loop_unref(loop);

	if (result_file)
	{
		char *filename = g_file_get_path(result_file);
		list<string> file_list;
		file_list.push_back(filename);
		m_QuiverImplPtr->m_ImageListPtr->SetImageList(&file_list);
		g_free (filename);
		g_object_unref(result_file);
	}

	g_object_unref(dialog);
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
		gtk_widget_set_visible(m_QuiverImplPtr->m_pToolbar, TRUE);
	}
	else
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pToolbar, FALSE);
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
		gtk_widget_set_visible(m_QuiverImplPtr->m_StatusbarPtr->GetWidget(), TRUE);
	}
	else
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_StatusbarPtr->GetWidget(), FALSE);
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
		gtk_widget_set_visible(m_QuiverImplPtr->m_pMenubar, TRUE);
	}
	else
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pMenubar, FALSE);
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
		/* heap-allocated: PreferencesDlg::Run() shows the dialog and
		 * returns immediately, so a stack object would be destroyed while
		 * the dialog's signal handlers (which use it as user_data) are still
		 * live.  The dialog self-deletes when it is destroyed. */
		PreferencesDlg *prefDlg = new PreferencesDlg();
		prefDlg->Run();
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
		/* heap-allocated: Run() shows the dialog and returns immediately,
		 * so a stack object would be destroyed while the dialog's signal
		 * handlers (which use it as user_data) are still live. */
		BookmarksDlg *bookmarkDlg = new BookmarksDlg();
		bookmarkDlg->Run();
	}
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

