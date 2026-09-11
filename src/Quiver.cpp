#include <config.h>

#include <gst/gst.h>

#include "Quiver.h"

GtkApplication *g_pApp = NULL;

#if HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf-animation.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>

#include <errno.h>
#include <exiv2/error.hpp>

#include "QuiverStockIcons.h"

#include "IBrowserEventHandler.h"
#include "IViewerEventHandler.h"
#include "IPreferencesEventHandler.h"
#include "IImageListEventHandler.h"
#include "IBookmarksEventHandler.h"


#include "QuiverUtils.h"
#include "ShortcutManager.h"
#include "ImageDecoder.h"

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

#include "TaskManager.h"
#include "TaskManagerDlg.h"

#include "ThreadUtil.h"

#include "ExternalTools.h"
#include "ExternalToolsDlg.h"
#include "ExternalToolTask.h"
#include "IExternalToolsEventHandler.h"

#include "ImageSaveManager.h"

#include <boost/algorithm/string.hpp>
#include "quiver-i18n.h"

#include <set>


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
	/* Save the current (modified) file, either silently or via a prompt,
	 * depending on the "ask"/"always" preference.  When bAllowCancel is true
	 * (called from the close path) a Cancel button is included and returns
	 * false to abort the close.  On navigation bAllowCancel is false. */
	bool MaybeSaveModified(bool bAllowCancel = false);
	
	bool CanClose();

	void UpdateUI();
	
	static void ShowViewerUIItems(QuiverImpl *pQuiverImpl, bool bShow);
	static void ShowBrowserUIItems(QuiverImpl *pQuiverImpl, bool bShow);
	static void SetViewerNavigationAccelerators(bool bEnable);
	static void CreateToolbarButtons(QuiverImpl *pQuiverImpl);
	void RebuildMenubar();
	/* Recursively clone a GMenuModel keeping only the items whose "location"
	 * attribute matches one of the active contexts (plus the always-on
	 * bookmark/tools placeholders).  Returns a new GMenu owned by the caller. */
	GMenu* FilterMenuModel(GMenuModel *model, const std::set<std::string> &contexts);

// member variables
	Quiver *m_pQuiver;

	BrowserPtr m_BrowserPtr;
	ViewerPtr m_ViewerPtr;
	PropertyView m_PropertyView;
	
	StatusbarPtr m_StatusbarPtr;

	BookmarksPtr m_BookmarksPtr;
	ExternalToolsPtr m_ExternalToolsPtr;

	GtkWidget *m_pQuiverWindow;

	GtkWidget *m_pHeaderBar;
	GtkWidget *m_pMenuButton;
	GtkWidget *m_pPrefButton;
	GtkWidget *m_pMenubar;
	/* Persistent GtkPopoverMenu backing the hamburger button.  It is created
	 * once; RebuildMenubar() swaps the filtered menu model on it and the
	 * zoom/rotate rows are registered once as custom children ("zoom-row" /
	 * "rotate-row", referenced by <item custom="..."> in quiver-menus.ui). */
	GtkWidget *m_pMenuPopover;
	GtkWidget *m_pMenuZoomRow;
	GtkWidget *m_pMenuRotateRow;
	/* Pristine, never-mutated GtkBuilder of data/quiver-menus.ui.  The visible
	 * menu model is a filtered clone produced on every mode switch. */
	GtkBuilder *m_pMenubarBuilder;
	GMenuModel *m_pAppMenuModel;
	GtkWidget *m_pToolbar;
	GtkWidget *m_pToolbarSharedBox;
	GtkWidget *m_pToolbarBrowserBox;
	GtkWidget *m_pToolbarViewerBox;
	GtkWidget *m_pUIModeViewerBtn;
	GtkWidget *m_pUIModeBrowserBtn;
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
	/* True when the current viewer item is a video.  RebuildMenubar() uses this
	 * (together with m_bViewerMode) to pick the active menu contexts; it is
	 * updated in ImageChanged() so switching still <-> video re-filters the
	 * hamburger menu. */
	bool m_bCurrentIsVideo;
	
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
	m_bCurrentIsVideo = false;
	m_pHeaderBar = NULL;
	m_pMenuButton = NULL;
	m_pPrefButton = NULL;
	m_pMenubar = NULL;
	m_pMenuPopover = NULL;
	m_pMenuZoomRow = NULL;
	m_pMenuRotateRow = NULL;
	m_pMenubarBuilder = NULL;
	m_pAppMenuModel = NULL;
	m_pToolbar = NULL;
	m_pToolbarSharedBox = NULL;
	m_pToolbarBrowserBox = NULL;
	m_pToolbarViewerBox = NULL;
	m_pUIModeViewerBtn = NULL;
	m_pUIModeBrowserBtn = NULL;
	
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
	if (0 != m_iTimeoutMouseMotionNotify)
	{
		g_source_remove(m_iTimeoutMouseMotionNotify);
		m_iTimeoutMouseMotionNotify = 0;
	}
	if (0 != m_iTimeoutKeepScreenOn)
	{
		g_source_remove(m_iTimeoutKeepScreenOn);
		m_iTimeoutKeepScreenOn = 0;
	}

	m_BookmarksPtr->RemoveEventHandler(m_BookmarksEventHandler);
	m_ImageListPtr->RemoveEventHandler(m_ImageListEventHandler);	
	if (m_ExternalToolsPtr)
	{
		m_ExternalToolsPtr->RemoveEventHandler(m_ExternalToolsEventHandler);
	}	

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

	/* Rebuild the whole menu so it always has the static section followed by a
	 * dynamic section of bookmarks.  GTK draws a separator between the two
	 * non-empty adjacent sections, replacing the old blank separator item. */
	while (g_menu_model_get_n_items(G_MENU_MODEL(m_pBookmarkMenu)) > 0)
	{
		g_menu_remove(m_pBookmarkMenu, 0);
	}

	GMenu *staticSection = g_menu_new();
	g_menu_append(staticSection, "_Add Bookmark", "quiver.BookmarksAdd");
	g_menu_append(staticSection, "_Edit Bookmarks...", "quiver.BookmarksEdit");
	g_menu_append_section(m_pBookmarkMenu, NULL, G_MENU_MODEL(staticSection));
	g_object_unref(staticSection);

	vector<Bookmark> bookmarks = m_BookmarksPtr->GetBookmarks();
	if (!bookmarks.empty())
	{
		GMenu *dynSection = g_menu_new();
		for (unsigned int i = 0; i < bookmarks.size(); ++i)
		{
			stringstream ss;
			ss << "Bookmark_" << bookmarks[i].GetID();
			string name = ss.str();

			QuiverUtils::RemoveAction(name.c_str());
			QuiverUtils::AddSimpleAction(name.c_str(), "", quiver_new_action_handler_cb, this);

			string full_name = "quiver." + name;
			GMenuItem *item = g_menu_item_new(bookmarks[i].GetName().c_str(), full_name.c_str());
			g_menu_append_item(dynSection, item);
			g_object_unref(item);
		}
		g_menu_append_section(m_pBookmarkMenu, NULL, G_MENU_MODEL(dynSection));
		g_object_unref(dynSection);
	}
}

void QuiverImpl::LoadExternalTools()
{
	if (NULL == m_pExternalToolsMenu)
	{
		return;
	}

	/* Rebuild the whole menu so it always has the static section followed by a
	 * dynamic section of external tools.  GTK draws a separator between the two
	 * non-empty adjacent sections, replacing the old blank separator item. */
	while (g_menu_model_get_n_items(G_MENU_MODEL(m_pExternalToolsMenu)) > 0)
	{
		g_menu_remove(m_pExternalToolsMenu, 0);
	}

	GMenu *staticSection = g_menu_new();
	g_menu_append(staticSection, "Task Manager...", "quiver.TaskManager");
	g_menu_append(staticSection, "Adjust Date...", "quiver.AdjustDate");
	g_menu_append(staticSection, "Rename...", "quiver.Rename");
	g_menu_append(staticSection, "Organize...", "quiver.Organize");
	g_menu_append(staticSection, "External Tools...", "quiver.ExternalTools");
	g_menu_append_section(m_pExternalToolsMenu, NULL, G_MENU_MODEL(staticSection));
	g_object_unref(staticSection);

	vector<ExternalTool> externaltools = m_ExternalToolsPtr->GetExternalTools();
	if (!externaltools.empty())
	{
		GMenu *dynSection = g_menu_new();
		for (unsigned int i = 0; i < externaltools.size(); ++i)
		{
			stringstream ss;
			ss << "ExternalTool_" << externaltools[i].GetID();
			string name = ss.str();

			QuiverUtils::RemoveAction(name.c_str());
			QuiverUtils::AddSimpleAction(name.c_str(), "", quiver_new_action_handler_cb, this);

			string full_name = "quiver." + name;
			GMenuItem *item = g_menu_item_new(externaltools[i].GetName().c_str(), full_name.c_str());
			g_menu_append_item(dynSection, item);
			g_object_unref(item);
		}
		g_menu_append_section(m_pExternalToolsMenu, NULL, G_MENU_MODEL(dynSection));
		g_object_unref(dynSection);
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

bool QuiverImpl::MaybeSaveModified(bool bAllowCancel)
{
	/* Only offer to save a modified file we can write back to. */
	if (!m_CurrentQuiverFile.Modified() || !m_CurrentQuiverFile.IsWriteable())
	{
		return true;
	}

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	const std::string mode = prefsPtr->GetString(QUIVER_PREFS_APP, QUIVER_PREFS_SAVE_ON_NAVIGATE, "ask");
	if (mode == "always")
	{
		Save();
		return true;
	}

	/* Build a custom prompt window with a "don't ask again" check box,
	 * because GtkAlertDialog in this GTK version lacks extra_child. */

	struct PromptData {
		gint response; /* -1 = waiting, 0 = save, 1 = discard, 2 = cancel */
		gboolean neverAsk;
		GMainLoop *loop;
	};

	PromptData data = { -1, FALSE, g_main_loop_new(NULL, FALSE) };

	GtkWidget *dlg = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dlg), _("Save changes?"));
	gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(m_pQuiverWindow));
	gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start(vbox, 18);
	gtk_widget_set_margin_end(vbox, 18);
	gtk_widget_set_margin_top(vbox, 18);
	gtk_widget_set_margin_bottom(vbox, 18);
	gtk_window_set_child(GTK_WINDOW(dlg), vbox);

	GtkWidget *label = gtk_label_new(
		_("This image has unsaved changes.\nDo you want to save your changes?"));
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_box_append(GTK_BOX(vbox), label);

	GtkWidget *check = gtk_check_button_new_with_label(
		_("Always save automatically - don't ask again"));
	g_signal_connect(check, "toggled",
		G_CALLBACK(+[](GtkCheckButton *btn, gpointer ud) {
			auto *d = static_cast<PromptData*>(ud);
			d->neverAsk = gtk_check_button_get_active(btn) ? TRUE : FALSE;
		}), &data);
	gtk_box_append(GTK_BOX(vbox), check);

	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(hbox, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(vbox), hbox);

	/* Pack Cancel on the left, Discard, Save on the right. */
	if (bAllowCancel)
	{
		GtkWidget *btnCancel = gtk_button_new_with_label(_("Cancel"));
		g_signal_connect(btnCancel, "clicked",
			G_CALLBACK(+[](gpointer ud) {
				auto *d = static_cast<PromptData*>(ud);
				d->response = 2;
				g_main_loop_quit(d->loop);
			}), &data);
		gtk_box_append(GTK_BOX(hbox), btnCancel);
	}

	GtkWidget *btnDiscard = gtk_button_new_with_label(_("Discard Changes"));
	g_signal_connect(btnDiscard, "clicked",
		G_CALLBACK(+[](gpointer ud) {
			auto *d = static_cast<PromptData*>(ud);
			d->response = 1;
			g_main_loop_quit(d->loop);
		}), &data);
	gtk_box_append(GTK_BOX(hbox), btnDiscard);

	GtkWidget *btnSave = gtk_button_new_with_label(_("Save"));
	g_signal_connect(btnSave, "clicked",
		G_CALLBACK(+[](gpointer ud) {
			auto *d = static_cast<PromptData*>(ud);
			d->response = 0;
			g_main_loop_quit(d->loop);
		}), &data);
	gtk_box_append(GTK_BOX(hbox), btnSave);

	/* Treat the close button as Cancel / Discard. */
	g_signal_connect(dlg, "close-request",
		G_CALLBACK(+[](GtkWidget *, gpointer ud) -> gboolean {
			auto *d = static_cast<PromptData*>(ud);
			d->response = 1;
			g_main_loop_quit(d->loop);
			return TRUE;
		}), &data);

	gtk_window_present(GTK_WINDOW(dlg));
	g_main_loop_run(data.loop);
	g_main_loop_unref(data.loop);

	/* Read checkbox before destroying the window. */
	const gboolean bNeverAsk = data.neverAsk;
	const gint response = data.response;

	gtk_window_destroy(GTK_WINDOW(dlg));

	if (bNeverAsk)
	{
		prefsPtr->SetString(QUIVER_PREFS_APP, QUIVER_PREFS_SAVE_ON_NAVIGATE, "always");
	}

	if (response == 0)
	{
		Save();
	}
	else if (response == 1)
	{
		/* Discard: nothing to do. */
	}
	else
	{
		/* Cancel: abort the operation that led here. */
		return false;
	}

	return true;
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
#define ACTION_QUIVER_TASK_MANAGER                           "TaskManager"
#define ACTION_QUIVER_ADJUST_DATE                            "AdjustDate"
#define ACTION_QUIVER_ORGANIZE                               "Organize"
#define ACTION_QUIVER_RENAME                                 "Rename"
#define ACTION_QUIVER_ABOUT                                  "About"
#define ACTION_QUIVER_UI_MODE_BROWSER                        "UIModeBrowser"
#define ACTION_QUIVER_UI_MODE_VIEWER                         "UIModeViewer"
#define ACTION_QUIVER_ESCAPE                                 "QuiverEscape"
#define ACTION_VIEWER_VIEW_FILM_STRIP                        "ViewFilmStrip"
#define ACTION_QUIVER_CLOSE_2                                ACTION_QUIVER_CLOSE"_2"
#define ACTION_QUIVER_CLOSE_3                                ACTION_QUIVER_CLOSE"_3"
#define ACTION_QUIVER_CLOSE_4                                ACTION_QUIVER_CLOSE"_4"

/* GMenu-based menu handling for GtkPopoverMenuBar.

 * The shared menu skeleton (including both states' items) is defined in
 * data/quiver-menus.ui and loaded once into a pristine GMenuModel.  Every
 * mode-specific item / column carries a custom "location" attribute naming the
 * state it belongs to ("browser" or "viewer"), or a live placeholder
 * ("bookmarks"/"tools").  RebuildMenubar() clones that pristine model on every
 * mode switch via FilterMenuModel(), keeping shared items plus the items whose
 * "location" matches the currently-active state.
 *
 * Because each context's items are tagged and the visible model is always a fresh
 * clone of the pristine source, merge/unmerge never moves items across contexts
 * and never corrupts the other context's layout: unmerging the no-longer-active
 * context simply means the clone omits that context's tagged items.
 */
GMenu* QuiverImpl::FilterMenuModel(GMenuModel *model, const std::set<std::string> &contexts)
{
	GMenu *dst = g_menu_new();
	const int count = g_menu_model_get_n_items(model);

	for (int i = 0; i < count; ++i)
	{
		g_autoptr(GMenuModel) section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
		g_autoptr(GMenuModel) submenu = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);

		g_autofree gchar *location = NULL;
		g_menu_model_get_item_attribute(model, i, "location", "s", &location);

		gboolean bKeep = TRUE;
		/* Which explicit submenu model to substitute (live placeholders). */
		GMenu *replacement = NULL;

		if (location != NULL)
		{
			if (strcmp(location, "bookmarks") == 0)
				replacement = m_pBookmarkMenu;
			else if (strcmp(location, "tools") == 0)
				replacement = m_pExternalToolsMenu;
			else if (contexts.find(location) == contexts.end())
				bKeep = FALSE;
		}

		if (!bKeep)
			continue;

		/* Section: filter its contents, keep only if non-empty. */
		if (section)
		{
			GMenu *newSection = FilterMenuModel(section, contexts);
			if (g_menu_model_get_n_items(G_MENU_MODEL(newSection)) > 0)
				g_menu_append_section(dst, NULL, G_MENU_MODEL(newSection));
			g_object_unref(newSection);
			continue;
		}

		GMenuItem *item = g_menu_item_new_from_model(model, i);

		/* Live placeholders are referenced directly, never cloned, so the
		 * bookmarks / external-tools submenus stay editable by
		 * LoadBookmarks()/LoadExternalTools() across mode switches. */
		if (replacement != NULL)
		{
			g_menu_item_set_submenu(item, G_MENU_MODEL(replacement));
			g_menu_append_item(dst, item);
			g_object_unref(item);
			continue;
		}

		/* Regular submenu item: clone it, recursively filtering its contents,
		 * and drop the item entirely if its (filtered) submenu is empty. */
		if (submenu)
		{
			GMenu *newSub = FilterMenuModel(submenu, contexts);
			if (g_menu_model_get_n_items(G_MENU_MODEL(newSub)) == 0)
			{
				g_object_unref(item);
				g_object_unref(newSub);
				continue;
			}
			g_menu_item_set_submenu(item, G_MENU_MODEL(newSub));
			g_object_unref(newSub);
		}

		g_menu_append_item(dst, item);
		g_object_unref(item);
	}

	return dst;
}

/* Build a horizontal row for the hamburger popover: a caption label on the
 * left (e.g. "Zoom" / "Rotate") followed by one icon button per entry.
 * icons/actions/tips are parallel arrays of length n.  Clicking a button only
 * activates its action — the popover stays open so zoom/rotate can be applied
 * repeatedly without reopening the menu. */
static GtkWidget *BuildPopoverButtonRow(const gchar *pszCaption,
                                        const gchar *const icons[],
                                        const gchar *const actions[],
                                        const gchar *const tips[],
                                        gint n)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_set_margin_top(row, 4);
	gtk_widget_set_margin_bottom(row, 4);
	gtk_widget_add_css_class(row, "wide");

	GtkWidget *label = gtk_label_new(pszCaption);
	/* Left-align the caption with the menu labels above/below it and keep it
	 * vertically centered against the icon buttons.  Give it a fixed character
	 * width so the "Zoom" and "Rotate" rows start their button groups at the
	 * same column, keeping the rows tidily aligned with each other. */
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_label_set_width_chars(GTK_LABEL(label), 6);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
	gtk_widget_set_margin_end(label, 8);
	gtk_box_append(GTK_BOX(row), label);

	for (gint i = 0; i < n; ++i)
	{
		GtkWidget *button = gtk_button_new_from_icon_name(icons[i]);
		gtk_widget_add_css_class(button, "image-button");
		gtk_widget_set_focus_on_click(button, FALSE);
		gtk_actionable_set_action_name(GTK_ACTIONABLE(button), actions[i]);
		if (tips[i] != NULL && tips[i][0] != '\0')
			gtk_widget_set_tooltip_text(button, tips[i]);
		gtk_box_append(GTK_BOX(row), button);
	}

	return row;
}

void QuiverImpl::RebuildMenubar()
{
	/* Load the pristine menu skeleton (with per-state "location" tags) once.
	 * It is never mutated; every build is a fresh filtered clone so the two
	 * states never collide in a shared placeholder. */
	if (NULL == m_pMenubarBuilder)
	{
		m_pMenubarBuilder = gtk_builder_new_from_file(QUIVER_DATADIR "/" "quiver-menus.ui");
		if (NULL == m_pMenubarBuilder)
		{
			g_warning("Failed to load menu UI file quiver-menus.ui");
			return;
		}
		m_pAppMenuModel = G_MENU_MODEL(gtk_builder_get_object(m_pMenubarBuilder, "app_menu"));

		/* The bookmarks / external-tools menus are persistent live placeholders
		 * defined in the same file and populated by LoadBookmarks()/
		 * LoadExternalTools(). */
		m_pBookmarkMenu = G_MENU(gtk_builder_get_object(m_pMenubarBuilder, "bookmark_menu"));
		m_pExternalToolsMenu = G_MENU(gtk_builder_get_object(m_pMenubarBuilder, "external_tools_menu"));
	}

	/* Active menu contexts for the current state: "browser", or "viewer" plus
	 * either "viewer-image" or "viewer-video" depending on the current item.
	 * FilterMenuModel() drops everything not in this set, so the "_Browser"
	 * switch entry (location="viewer") and the zoom/rotate rows only appear in
	 * the modes where they belong. */
	std::set<std::string> contexts;
	if (m_bViewerMode)
	{
		contexts.insert("viewer");
		contexts.insert(m_bCurrentIsVideo ? "viewer-video" : "viewer-image");
	}
	else
	{
		contexts.insert("browser");
	}
	GMenu *appMenu = FilterMenuModel(m_pAppMenuModel, contexts);

	if (NULL == m_pMenuButton)
	{
		m_pMenuButton = gtk_menu_button_new();
		gtk_widget_set_name(m_pMenuButton, "QuiverMenuButton");
		gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(m_pMenuButton), "open-menu-symbolic");
		gtk_widget_set_tooltip_text(m_pMenuButton, "Main Menu");
		/* Don't let the menu button steal focus on click, otherwise arrow-key
		 * navigation in the browser/icon view and viewer is lost until the
		 * user clicks back into the content area.  Also make it completely
		 * non-focusable so Tab skips it. */
		gtk_widget_set_focus_on_click(m_pMenuButton, FALSE);
		gtk_widget_set_focusable(m_pMenuButton, FALSE);

		/* Use an explicit GtkPopoverMenu so the zoom/rotate rows can be
		 * attached as custom children (Nautilus-style inline rows) and only
		 * the filtered GMenu is swapped on context changes.  The rows are
		 * registered against the model below via add_child() (which only
		 * resolves ids present in the model at call time), so the model is
		 * re-set and the rows re-added on every rebuild. */
		m_pMenuPopover = gtk_popover_menu_new_from_model(G_MENU_MODEL(appMenu));

		/* Zoom row: fit-stretch, 1:1, zoom in, zoom out. */
		static const gchar *const zoomIcons[] = {
			"zoom-fit-best-symbolic",
			"zoom-original-symbolic",
			"zoom-in-symbolic",
			"zoom-out-symbolic"
		};
		static const gchar *const zoomActions[] = {
			"quiver.ZoomFitStretch",
			"quiver.Zoom100",
			"quiver.ZoomIn",
			"quiver.ZoomOut"
		};
		static const gchar *const zoomTips[] = {
			"Zoom to Fit Window (Stretch)",
			"Actual Size (100%)",
			"Zoom In",
			"Zoom Out"
		};
		m_pMenuZoomRow = BuildPopoverButtonRow("Zoom",
			zoomIcons, zoomActions, zoomTips, G_N_ELEMENTS(zoomIcons));
		/* Take a strong reference on each row so it survives the content
		 * rebuild that gtk_popover_menu_set_menu_model() performs.  Every
		 * rebuild below destroys the old slots (which would otherwise drain the
		 * rows' only reference and leave the members dangling, crashing
		 * add_child() on the next rebuild — brackets re-parent warnings).
		 * with our own ref the row is merely unparented; add_child() can then
		 * attach it to the freshly-created slot. */

		/* Rotate row: clockwise, counterclockwise, flip horizontal, flip
		 * vertical.  Still images only (location="viewer-image"). */
		static const gchar *const rotateIcons[] = {
			"object-rotate-right-symbolic",
			"object-rotate-left-symbolic",
			"object-flip-horizontal-symbolic",
			"object-flip-vertical-symbolic"
		};
		static const gchar *const rotateActions[] = {
			"quiver.RotateCW",
			"quiver.RotateCCW",
			"quiver.FlipH",
			"quiver.FlipV"
		};
		static const gchar *const rotateTips[] = {
			"Rotate Clockwise",
			"Rotate Counterclockwise",
			"Flip Horizontally",
			"Flip Vertically"
		};
		m_pMenuRotateRow = BuildPopoverButtonRow("Rotate",
			rotateIcons, rotateActions, rotateTips, G_N_ELEMENTS(rotateIcons));

		/* Own both rows (see note above) so menu-model rebuilds can never free
		 * them underneath us. */
		g_object_ref_sink(m_pMenuZoomRow);
		g_object_ref_sink(m_pMenuRotateRow);

		gtk_menu_button_set_popover(GTK_MENU_BUTTON(m_pMenuButton), m_pMenuPopover);
	}

	/* Set the filtered model, then (re-)register the custom rows.  These two
	 * must go together: gtk_popover_menu_add_child() only resolves ids that are
	 * present in the popover's *current* model, so starting up in browser mode
	 * (whose model has no zoom/rotate rows) would otherwise leave the rows
	 * unregistered forever.  add_child() returns FALSE harmlessly whenever the
	 * current model lacks the row's slot, and — because we own the rows via
	 * g_object_ref_sink() above — each rebuild finds them still alive and simply
	 * re-attaches them to the freshly-created slots. */
	gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(m_pMenuPopover), G_MENU_MODEL(appMenu));
	gtk_popover_menu_add_child(GTK_POPOVER_MENU(m_pMenuPopover), m_pMenuZoomRow, "zoom-row");
	gtk_popover_menu_add_child(GTK_POPOVER_MENU(m_pMenuPopover), m_pMenuRotateRow, "rotate-row");
	m_pMenubar = m_pMenuButton;
	g_object_unref(appMenu);
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
	(void)widget; (void)drag_context; (void)user_data;
	// disable drop 
	gtk_drag_dest_unset(m_QuiverImplPtr->m_pQuiverWindow);
	
#if HAVE_GDK_PIXBUF
	// set icon
	GdkPixbuf *thumb = m_QuiverImplPtr->m_ImageListPtr->GetCurrent().GetThumbnail();

	if (NULL != thumb)
	{
		gtk_drag_set_icon_pixbuf(drag_context,thumb,-2,-2);
		g_object_unref(thumb);
	}
#endif
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
		
		/* Save the outgoing file: per-preference, either silently or with a
		 * confirmation prompt.  Dialog is a message box because the File menu
		 * was removed. */
		m_QuiverImplPtr->MaybeSaveModified();
		
		m_QuiverImplPtr->m_CurrentQuiverFile = f;
		
		/* Rebuild the hamburger menu when the still <-> video context flips
		 * so its Image / Video submenus match the current file. */
		bool bIsVideo = f.IsVideo();
		if (bIsVideo != m_QuiverImplPtr->m_bCurrentIsVideo)
		{
			m_QuiverImplPtr->m_bCurrentIsVideo = bIsVideo;
			m_QuiverImplPtr->RebuildMenubar();
		}
		
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
		if (0 != pQuiverImpl->m_iTimeoutMouseMotionNotify)
		{
			g_source_remove(pQuiverImpl->m_iTimeoutMouseMotionNotify);
		}
		pQuiverImpl->m_iTimeoutMouseMotionNotify = g_timeout_add(1500, timeout_event_motion_notify, pQuiverImpl);

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

	/* Ask to save unsaved changes before closing (File menu is gone). */
	if (m_QuiverImplPtr && !m_QuiverImplPtr->MaybeSaveModified(true))
	{
		/* Cancel - keep the window open. */
		return;
	}
	m_bClosing = true;

	if (0 != m_iIdleInitID)
	{
		g_source_remove(m_iIdleInitID);
		m_iIdleInitID = 0;
	}

	// Defer the teardown to an idle callback: Close() is often reached from a
	// keyboard accelerator (e.g. "q" via gtk_accel_groups_activate), and
	// destroying the window inside the accel-group dispatch would leave GTK
	// walking freed accel-group state (SIGSEGV in gtk_accel_groups_activate).
	if (0 == m_iCloseIdleID)
	{
		m_iCloseIdleID = g_idle_add(close_idle_cb, this);
	}
}

gboolean Quiver::close_idle_cb(gpointer data)
{
	Quiver *pQuiver = (Quiver*)data;
	pQuiver->m_iCloseIdleID = 0;
	pQuiver->CloseReal();
	return FALSE;
}

void Quiver::CloseReal()
{
	if (0 != m_iCloseIdleID)
	{
		g_source_remove(m_iCloseIdleID);
		m_iCloseIdleID = 0;
	}
	if (0 != m_iIdleInitID)
	{
		g_source_remove(m_iIdleInitID);
		m_iIdleInitID = 0;
	}

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
		if (!m_QuiverImplPtr->MaybeSaveModified(true))
		{
			/* Cancel - keep the window open. */
			return TRUE;
		}
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
		m_bClosing(false),
		m_iIdleInitID(0),
		m_iCloseIdleID(0)
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

	m_QuiverImplPtr->m_pHeaderBar = gtk_header_bar_new();
	gtk_widget_set_name(m_QuiverImplPtr->m_pHeaderBar, "Quiver HeaderBar");
	gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(m_QuiverImplPtr->m_pHeaderBar), TRUE);
	gtk_window_set_titlebar(GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow), m_QuiverImplPtr->m_pHeaderBar);


	if (LoadSettings())
	{	
		//set the size and position of the window
		gtk_window_set_default_size (GTK_WINDOW(m_QuiverImplPtr->m_pQuiverWindow),m_QuiverImplPtr->m_iAppWidth,m_QuiverImplPtr->m_iAppHeight);

	}

	ApplyForceDarkTheme();
	
	gchar *icon_path = g_build_filename(QUIVER_DATADIR, "icons", "48x48", "quiver-icon-app.png", NULL);
	(void)icon_path;
	gtk_window_set_default_icon_name("quiver-icon-app");
	g_free(icon_path);	

	/* Set up GUI elements */

	/* Build GMenu-based menubar (GTK4 replacement for GtkMenuBar).  The menu
	 * skeleton and the live bookmark / external-tools placeholder menus are
	 * loaded from data/quiver-menus.ui inside RebuildMenubar(). */
	m_QuiverImplPtr->RebuildMenubar();

	/* GSimpleAction based action system (replaces GtkUIManager/GtkAction).
	 * Actions are registered here and their widgets bound with
	 * QuiverUtils::BindWidget() / BindToggleWidget() / BindRadioWidget(). */
	QuiverUtils::InitActions();
	ShortcutManager::GetInstance().Init();
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
	QuiverUtils::AddSimpleAction(ACTION_QUIVER_TASK_MANAGER, "", quiver_new_action_handler_cb, m_QuiverImplPtr.get());
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

	/* Build the toolbar from data/quiver-toolbar.ui (JSON/XML UI file).  It is
	 * a plain GtkBox whose widgets bind to GSimpleActions via action-name, and
	 * is grouped into shared / browser-only / viewer-only boxes so the boxes
	 * can be shown or hidden when the UI mode changes. */
	QuiverImpl::CreateToolbarButtons(m_QuiverImplPtr.get());

	/* Move toolbar items to the headerbar (start side) */
	gtk_header_bar_pack_start(GTK_HEADER_BAR(m_QuiverImplPtr->m_pHeaderBar), m_QuiverImplPtr->m_pToolbar);

	/* Create preferences button with gear icon */
	m_QuiverImplPtr->m_pPrefButton = gtk_button_new_from_icon_name("preferences-system-symbolic");
	gtk_widget_set_name(m_QuiverImplPtr->m_pPrefButton, "Quiver Preferences Button");
	gtk_actionable_set_action_name(GTK_ACTIONABLE(m_QuiverImplPtr->m_pPrefButton), "quiver.Preferences");
	gtk_widget_set_tooltip_text(m_QuiverImplPtr->m_pPrefButton, "Preferences");
	gtk_widget_set_focus_on_click(m_QuiverImplPtr->m_pPrefButton, FALSE);
	gtk_widget_set_focusable(m_QuiverImplPtr->m_pPrefButton, FALSE);

	/* Pack headerbar end items: hamburger menu button at far right, preferences gear to its left */
	gtk_header_bar_pack_end(GTK_HEADER_BAR(m_QuiverImplPtr->m_pHeaderBar), m_QuiverImplPtr->m_pMenuButton);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(m_QuiverImplPtr->m_pHeaderBar), m_QuiverImplPtr->m_pPrefButton);

	/* Give the browser the headerbar so it can insert its thumb-size widget at the end */
	m_QuiverImplPtr->m_BrowserPtr->SetToolbar(m_QuiverImplPtr->m_pHeaderBar);

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
	/* gtk_notebook_append_page() above auto-shows the notebook, so re-apply
	 * the preference here regardless of its value. */
	gtk_widget_set_visible(m_QuiverImplPtr->m_pNBProperties, prefs_show);
	
	// statusbar
	statusbar =  m_QuiverImplPtr->m_StatusbarPtr->GetWidget();
	gtk_widget_set_visible(statusbar, FALSE);

	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW, true);
	if (prefs_show)
	{
		gtk_widget_set_visible(statusbar, TRUE);
	}
	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_STATUSBAR, prefs_show);

	// menubar / hamburger menu button
	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW, true);
	if (prefs_show)
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pMenuButton, TRUE);
	}
	else
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pMenuButton, FALSE);
	}
	QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_MENUBAR, prefs_show);
	
	// toolbar
	prefs_show = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW, true);
	if (prefs_show)
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pToolbar, TRUE);
	}
	else
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pToolbar, FALSE);
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
					
	m_iIdleInitID = g_idle_add(idle_quiver_init,this);
	
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
	if (0 != m_iIdleInitID)
	{
		g_source_remove(m_iIdleInitID);
		m_iIdleInitID = 0;
	}
	if (0 != m_iCloseIdleID)
	{
		g_source_remove(m_iCloseIdleID);
		m_iCloseIdleID = 0;
	}
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

void Quiver::ApplyForceDarkTheme()
{
	GdkDisplay *display = gdk_display_get_default();
	if (display == NULL)
		return;

	GtkSettings *settings = gtk_settings_get_for_display(display);

	PreferencesPtr prefsPtr = Preferences::GetInstance();
	bool bForceDark = prefsPtr->GetBoolean(QUIVER_PREFS_APP, QUIVER_PREFS_APP_FORCE_DARK_THEME, false);

	g_object_set(settings, "gtk-application-prefer-dark-theme", bForceDark, NULL);
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
	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
		{
			g_print("Usage: quiver [OPTIONS] [FILES or DIRECTORIES...]\n\n"
			        "Options:\n"
			        "  --decoder=glycin|pixbuf|auto   Select image decoding backend (default: auto)\n"
			        "  -h, --help                     Show this help message\n\n"
			        "Environment Variables:\n"
			        "  QUIVER_DECODER=glycin|pixbuf|auto\n");
			return 0;
		}
	}

	(void)bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	(void)bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	(void)textdomain (GETTEXT_PACKAGE);

	/* Suppress Exiv2 stderr spam for corrupt/truncated metadata */
	Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);

 	/* init threads */
	//g_type_init ();

	/* Initialize the widget set */
	gtk_init ();

	g_pApp = gtk_application_new("com.github.bikemike.quiver", G_APPLICATION_NON_UNIQUE);
	g_signal_connect(g_pApp, "activate", G_CALLBACK(on_app_activate), NULL);

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

	PreferencesPtr prefsPtr = Preferences::GetInstance();

	// 1. Decoder backend from preferences
	if (prefsPtr->HasKey(QUIVER_PREFS_APP, QUIVER_PREFS_APP_IMAGE_DECODER))
	{
		string dec = prefsPtr->GetString(QUIVER_PREFS_APP, QUIVER_PREFS_APP_IMAGE_DECODER, "auto");
		if (g_ascii_strcasecmp(dec.c_str(), "glycin") == 0)
			ImageDecoder::SetBackend(ImageDecoder::Backend::GLYCIN);
		else if (g_ascii_strcasecmp(dec.c_str(), "pixbuf") == 0)
			ImageDecoder::SetBackend(ImageDecoder::Backend::PIXBUF);
		else if (g_ascii_strcasecmp(dec.c_str(), "auto") == 0)
			ImageDecoder::SetBackend(ImageDecoder::Backend::AUTO);
	}

	// 2. Decoder backend from environment variable QUIVER_DECODER
	const char *env_decoder = g_getenv("QUIVER_DECODER");
	if (env_decoder != NULL)
	{
		if (g_ascii_strcasecmp(env_decoder, "glycin") == 0)
			ImageDecoder::SetBackend(ImageDecoder::Backend::GLYCIN);
		else if (g_ascii_strcasecmp(env_decoder, "pixbuf") == 0)
			ImageDecoder::SetBackend(ImageDecoder::Backend::PIXBUF);
		else if (g_ascii_strcasecmp(env_decoder, "auto") == 0)
			ImageDecoder::SetBackend(ImageDecoder::Backend::AUTO);
	}

	list<string> files;
	for (int i = 1; i < argc; i++)
	{
		if (g_str_has_prefix(argv[i], "--decoder="))
		{
			const char *dec = argv[i] + 10;
			if (g_ascii_strcasecmp(dec, "glycin") == 0)
				ImageDecoder::SetBackend(ImageDecoder::Backend::GLYCIN);
			else if (g_ascii_strcasecmp(dec, "pixbuf") == 0)
				ImageDecoder::SetBackend(ImageDecoder::Backend::PIXBUF);
			else if (g_ascii_strcasecmp(dec, "auto") == 0)
				ImageDecoder::SetBackend(ImageDecoder::Backend::AUTO);
			continue;
		}

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
	
	if (files.empty())
	{	
		const gchar* dir;
		// default to a directory
		// specified in preferences
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
	
#if HAVE_GDK_PIXBUF
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
#endif
                                             
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
	m_iIdleInitID = 0;
	if (m_bClosing || !m_QuiverImplPtr)
	{
		return FALSE;
	}

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

/* Recursively disable keyboard focus for every widget in a control bar
 * (menubar / toolbar) so that:
 *  - clicking a control never steals keyboard focus from the content area
 *    (which would break arrow-key navigation in the browser/icon view and
 *    viewer), and
 *  - Tab skips the whole bar (no tab stop), keeping Tab focus in the content.
 * The content grabs focus again explicitly, so removing the tab stop here does
 * not strand keyboard input. */
static void quiver_toolbar_set_focus_on_click(GtkWidget *widget, gboolean focus)
{
	if (NULL == widget)
		return;
	gtk_widget_set_focus_on_click(widget, focus);
	gtk_widget_set_focusable(widget, FALSE);
	for (GtkWidget *child = gtk_widget_get_first_child(widget);
	     child != NULL;
	     child = gtk_widget_get_next_sibling(child))
	{
		quiver_toolbar_set_focus_on_click(child, focus);
	}
}

void QuiverImpl::CreateToolbarButtons(QuiverImpl *pQuiverImpl)
{
	/* The toolbar (shared + browser + viewer control groups) is defined as
	 * data/quiver-toolbar.ui, so its structure and action-name bindings live
	 * in markup.  Merge/unmerge between the browser and viewer states is done
	 * by toggling the visibility of the two distinct, per-state boxes
	 * (browser_box / viewer_box); the shared box is always visible.  Because
	 * they are separate boxes, one state's controls can never be removed while
	 * the other state is active. */
	GtkBuilder *builder = gtk_builder_new_from_file(QUIVER_DATADIR "/" "quiver-toolbar.ui");
	if (NULL == builder)
	{
		g_error("Failed to load toolbar UI file");
		return;
	}

	GtkWidget *toolbar = GTK_WIDGET(gtk_builder_get_object(builder, "QuiverToolbar"));
	pQuiverImpl->m_pToolbarBrowserBox = GTK_WIDGET(gtk_builder_get_object(builder, "browser_box"));
	pQuiverImpl->m_pToolbarSharedBox   = GTK_WIDGET(gtk_builder_get_object(builder, "shared_box"));
	pQuiverImpl->m_pToolbarViewerBox   = GTK_WIDGET(gtk_builder_get_object(builder, "viewer_box"));
	pQuiverImpl->m_pUIModeViewerBtn    = GTK_WIDGET(gtk_builder_get_object(builder, "button_uimode_viewer"));
	pQuiverImpl->m_pUIModeBrowserBtn   = GTK_WIDGET(gtk_builder_get_object(builder, "button_uimode_browser"));

	/* Keep the builder alive so its objects stay referenced; the widgets are
	 * later parented into the window tree. */
	pQuiverImpl->m_pToolbar = toolbar;
	g_object_set_data_full(G_OBJECT(pQuiverImpl->m_pQuiverWindow), "toolbar-builder",
	                       builder, (GDestroyNotify)g_object_unref);

	/* Toolbar and headerbar buttons must not steal keyboard focus from the content area,
	 * otherwise arrow-key navigation in the browser/icon view and viewer is
	 * lost until the user clicks back in. */
	quiver_toolbar_set_focus_on_click(toolbar, FALSE);
	if (pQuiverImpl->m_pHeaderBar)
	{
		quiver_toolbar_set_focus_on_click(pQuiverImpl->m_pHeaderBar, FALSE);
	}

	/* start in browser mode: hide the viewer controls until ShowViewer() */
	gtk_widget_set_visible(pQuiverImpl->m_pToolbarViewerBox, FALSE);
}

void QuiverImpl::ShowViewerUIItems(QuiverImpl *pQuiverImpl, bool bShow)
{
	if (NULL != pQuiverImpl)
	{
		gtk_widget_set_visible(pQuiverImpl->m_pToolbarViewerBox, bShow);
	}
}

void QuiverImpl::ShowBrowserUIItems(QuiverImpl *pQuiverImpl, bool bShow)
{
	if (NULL != pQuiverImpl)
	{
		gtk_widget_set_visible(pQuiverImpl->m_pToolbarBrowserBox, bShow);
	}
}

/* Enable/disable the naked "space" / "<Shift>space" accelerators that navigate
 * the image list (ImageNext / ImagePrevious_2).  They are only meaningful in
 * the viewer; in the browser the space key is reserved for the folder tree
 * checkbox selection, so the global accelerators must not steal it. */
void QuiverImpl::SetViewerNavigationAccelerators(bool bEnable)
{
	ShortcutManager::GetInstance().SetViewerMode(bEnable);
}

void Quiver::ShowViewer()
{
	m_QuiverImplPtr->m_BrowserPtr->Hide();
	m_QuiverImplPtr->m_ViewerPtr->Show();

	m_QuiverImplPtr->m_bViewerMode = true;
	QuiverImpl::ShowViewerUIItems(m_QuiverImplPtr.get(), true);
	QuiverImpl::ShowBrowserUIItems(m_QuiverImplPtr.get(), false);
	m_QuiverImplPtr->RebuildMenubar();

	// Naked space/arrow navigation shortcuts are only active in the viewer.
	QuiverImpl::SetViewerNavigationAccelerators(true);

	m_QuiverImplPtr->m_ViewerPtr->GrabFocus();
}

void Quiver::ShowBrowser()
{
	m_QuiverImplPtr->m_ViewerPtr->Hide();
	m_QuiverImplPtr->m_BrowserPtr->Show();

	m_QuiverImplPtr->m_bViewerMode = false;
	QuiverImpl::ShowViewerUIItems(m_QuiverImplPtr.get(), false);
	QuiverImpl::ShowBrowserUIItems(m_QuiverImplPtr.get(), true);
	m_QuiverImplPtr->RebuildMenubar();

	// Naked space should not advance images while the browser (folder tree,
	// image list) is visible - it is instead used for the folder tree
	// checkbox selection.
	QuiverImpl::SetViewerNavigationAccelerators(false);

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
		if (m_pHeaderBar)
		{
			gtk_widget_set_visible(m_pHeaderBar, FALSE);
		}
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
			bool bShowTB = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_TOOLBAR_SHOW_FS, false);
			QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_TOOLBAR_MAIN, bShowTB);

			bool bShowMB = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_MENUBAR_SHOW_FS, false);
			QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_MENUBAR, bShowMB);
			
			bool bShow = prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_STATUSBAR_SHOW_FS, false);
			QuiverUtils::ToggleActionSetActive(ACTION_QUIVER_VIEW_STATUSBAR, bShow);

			if (m_pHeaderBar)
			{
				gtk_widget_set_visible(m_pHeaderBar, (bShowTB || bShowMB) ? TRUE : FALSE);
			}
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

			if (m_pHeaderBar)
			{
				gtk_widget_set_visible(m_pHeaderBar, TRUE);
			}
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
{
	if (event != NULL &&
		event->GetSection() == QUIVER_PREFS_APP &&
		event->GetKey() == QUIVER_PREFS_APP_FORCE_DARK_THEME)
	{
		parent->m_pQuiver->ApplyForceDarkTheme();
	}
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
		gtk_widget_set_visible(m_QuiverImplPtr->m_pNBProperties, TRUE);
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

	if (m_QuiverImplPtr->m_pToolbar)
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pToolbar, bShow ? TRUE : FALSE);
	}

	if (bShow)
	{
		if (m_QuiverImplPtr->m_pHeaderBar)
		{
			gtk_widget_set_visible(m_QuiverImplPtr->m_pHeaderBar, TRUE);
		}
	}
	else if (GDK_WINDOW_STATE_FULLSCREEN & m_QuiverImplPtr->m_WindowState)
	{
		bool bShowMB = QuiverUtils::ToggleActionGetActive(ACTION_QUIVER_VIEW_MENUBAR);
		if (!bShowMB && m_QuiverImplPtr->m_pHeaderBar)
		{
			gtk_widget_set_visible(m_QuiverImplPtr->m_pHeaderBar, FALSE);
		}
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

	if (m_QuiverImplPtr->m_pMenuButton)
	{
		gtk_widget_set_visible(m_QuiverImplPtr->m_pMenuButton, bShow ? TRUE : FALSE);
	}

	if (bShow)
	{
		if (m_QuiverImplPtr->m_pHeaderBar)
		{
			gtk_widget_set_visible(m_QuiverImplPtr->m_pHeaderBar, TRUE);
		}
	}
	else if (GDK_WINDOW_STATE_FULLSCREEN & m_QuiverImplPtr->m_WindowState)
	{
		bool bShowTB = QuiverUtils::ToggleActionGetActive(ACTION_QUIVER_VIEW_TOOLBAR_MAIN);
		if (!bShowTB && m_QuiverImplPtr->m_pHeaderBar)
		{
			gtk_widget_set_visible(m_QuiverImplPtr->m_pHeaderBar, FALSE);
		}
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
		/* PreferencesDialog is a singleton: ShowDialog() reuses an already-open
		 * window instead of stacking duplicates. */
		PreferencesDlg::ShowDialog();
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
				std::string strDate = dlg.GetDateString();
				int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
				if (sscanf(strDate.c_str(), "%04d:%02d:%02d %02d:%02d:%02d",
				           &year, &month, &day, &hour, &min, &sec) == 6)
				{
					struct tm tmDate = {};
					tmDate.tm_year = year - 1900;
					tmDate.tm_mon = month - 1;
					tmDate.tm_mday = day;
					tmDate.tm_hour = hour;
					tmDate.tm_min = min;
					tmDate.tm_sec = sec;
					tmDate.tm_isdst = -1;

					AdjustDateTaskPtr adjustDateTaskPtr(new AdjustDateTask(tmDate));

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

					std::list<unsigned int> items = pQuiverImpl->m_BrowserPtr->GetSelection();
					std::list<unsigned int>::iterator itr;
					for (itr = items.begin(); items.end() != itr; ++itr)
					{
						QuiverFile f = (*pQuiverImpl->m_ImageListPtr)[*itr];
						adjustDateTaskPtr->AddFile(f);
					}

					TaskManager::GetInstance()->AddTask(adjustDateTaskPtr);
				}
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

			if (!commands.empty())
			{
				std::vector<std::string> vectCommands(commands.begin(), commands.end());
				ExternalToolTaskPtr toolTaskPtr(new ExternalToolTask(extTool->GetName(), vectCommands));
				TaskManager::GetInstance()->AddTask(toolTaskPtr);
			}
		}
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_EXTERNAL_TOOLS))
	{
		ExternalToolsDlg::ShowDialog();
	}
	else if(0 == strcmp(szAction,ACTION_QUIVER_TASK_MANAGER))
	{
		TaskManagerDlg::GetInstance()->Show();
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
		/* BookmarksDialog is a singleton: ShowDialog() reuses an already-open
		 * window instead of stacking duplicates. */
		BookmarksDlg::ShowDialog();
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

