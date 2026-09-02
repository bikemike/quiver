#include <config.h>

#include "BookmarksDlg.h"

extern GtkApplication *g_pApp;

#include <list>
#include <vector>

#include "Bookmarks.h"
#include "IBookmarksEventHandler.h"
#include "BookmarkAddEditDlg.h"

#include "QuiverStockIcons.h"

using namespace std;

enum 
{
	COLUMN_ID,
	COLUMN_ICON,
	COLUMN_NAME,
	COLUMN_COUNT
};

class BookmarksDlg::BookmarksDlgPriv
{
public:
// constructor, destructor
	BookmarksDlgPriv(BookmarksDlg *parent);
	~BookmarksDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void SelectionChanged();
	void ConnectSignals();

// variables
	BookmarksDlg*     m_pBookmarksDlg;
	GtkBuilder*         m_pGtkBuilder;
	BookmarksPtr      m_BookmarksPtr;

	bool m_bLoadedDlg;
	
	// dlg widgets
	GtkWidget*             m_pWidget;
	GtkWidget*             m_pTreeViewBookmarks;
	GListStore*            m_pListStoreBookmarks;
	GtkMultiSelection*     m_pSelectionBookmarks;
	GtkButton*             m_pButtonMoveUp;
	GtkButton*             m_pButtonMoveDown;
	GtkButton*             m_pButtonAdd;
	GtkButton*             m_pButtonEdit;
	GtkButton*             m_pButtonRemove;
	GtkButton*             m_pButtonClose;

// nested classes
	class BookmarksEventHandler : public IBookmarksEventHandler
	{
	public:
		BookmarksEventHandler(BookmarksDlgPriv* parent) {this->parent = parent;};
		virtual void HandleBookmarkChanged(BookmarksEventPtr event);
	private:
		BookmarksDlgPriv* parent;
	};
	IBookmarksEventHandlerPtr m_BookmarksEventHandler;
	
};


// row item type for the bookmarks column view
typedef struct {
	GObject  parent_instance;
	int      id;
	gchar*   icon;
	gchar*   name;
} BookmarkItem;

typedef struct {
	GObjectClass parent_class;
} BookmarkItemClass;

#define BOOKMARK_ITEM_TYPE (bookmark_item_get_type())
#define BOOKMARK_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), BOOKMARK_ITEM_TYPE, BookmarkItem))

G_DEFINE_TYPE(BookmarkItem, bookmark_item, G_TYPE_OBJECT)

static void bookmark_item_finalize (GObject* object)
{
	BookmarkItem* item = BOOKMARK_ITEM(object);
	g_free(item->icon);
	g_free(item->name);
	G_OBJECT_CLASS(g_type_class_peek_parent(
		G_OBJECT_GET_CLASS(object)))->finalize(object);
}

static void bookmark_item_class_init (BookmarkItemClass* klass)
{
	G_OBJECT_CLASS(klass)->finalize = bookmark_item_finalize;
}

static void bookmark_item_init (BookmarkItem* item)
{
	item->id = 0;
	item->icon = NULL;
	item->name = NULL;
}

static BookmarkItem* bookmark_item_new (int id, const gchar* icon, const gchar* name)
{
	BookmarkItem* item = static_cast<BookmarkItem*>(
		g_object_new(BOOKMARK_ITEM_TYPE, NULL));
	item->id = id;
	item->icon = g_strdup(icon);
	item->name = g_strdup(name);
	return item;
}

static void bookmark_icon_setup (GtkListItem* list_item, gpointer user_data)
{ (void)user_data;
	GtkWidget* image = gtk_image_new();
	gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_NORMAL);
	gtk_widget_set_margin_start(image, 6);
	gtk_widget_set_margin_end(image, 6);
	gtk_list_item_set_child(list_item, image);
}

static void bookmark_icon_bind (GtkListItem* list_item, gpointer user_data)
{ (void)user_data;
	BookmarkItem* item = BOOKMARK_ITEM(gtk_list_item_get_item(list_item));
	GtkWidget* image = gtk_list_item_get_child(list_item);
	gtk_image_set_from_icon_name(GTK_IMAGE(image), item->icon);
}

static void bookmark_name_edited (GtkEditableLabel* editable, GParamSpec* pspec,
	BookmarksDlg::BookmarksDlgPriv* priv);
static void bookmark_name_setup (GtkListItem* list_item, gpointer user_data)
{
	BookmarksDlg::BookmarksDlgPriv* priv =
		static_cast<BookmarksDlg::BookmarksDlgPriv*>(user_data);
	GtkWidget* editable = gtk_editable_label_new(NULL);
	gtk_widget_set_hexpand(editable, TRUE);
	g_signal_connect(editable, "notify::text",
		G_CALLBACK(bookmark_name_edited), priv);
	gtk_list_item_set_child(list_item, editable);
}

static void bookmark_name_bind (GtkListItem* list_item, gpointer user_data)
{ (void)user_data;
	BookmarkItem* item = BOOKMARK_ITEM(gtk_list_item_get_item(list_item));
	GtkWidget* editable = gtk_list_item_get_child(list_item);
	g_object_set_data(G_OBJECT(editable), "bookmark-id",
		GINT_TO_POINTER(item->id));
	gtk_editable_set_text(GTK_EDITABLE(editable), item->name);
}

static void bookmark_name_edited (GtkEditableLabel* editable, GParamSpec* pspec,
	BookmarksDlg::BookmarksDlgPriv* priv)
{ (void)pspec;
	int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(editable), "bookmark-id"));
	const Bookmark* b = priv->m_BookmarksPtr->GetBookmark(id);
	if (NULL != b)
	{
		Bookmark modified = *b;
		modified.SetName(gtk_editable_get_text(GTK_EDITABLE(editable)));
		priv->m_BookmarksPtr->UpdateBookmark(modified);
	}
}

static GtkListItemFactory* bookmark_column_factory (int iCol,
	BookmarksDlg::BookmarksDlgPriv* priv)
{
	GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
	if (COLUMN_ICON == iCol)
	{
		g_signal_connect(factory, "setup", G_CALLBACK(bookmark_icon_setup), NULL);
		g_signal_connect(factory, "bind", G_CALLBACK(bookmark_icon_bind), NULL);
	}
	else
	{
		g_signal_connect(factory, "setup", G_CALLBACK(bookmark_name_setup), priv);
		g_signal_connect(factory, "bind", G_CALLBACK(bookmark_name_bind), NULL);
	}
	return factory;
}


BookmarksDlg::BookmarksDlg() : m_PrivPtr(new BookmarksDlg::BookmarksDlgPriv(this))
{
	
}


GtkWidget* BookmarksDlg::GetWidget()
{
	  return NULL;
}


static gboolean bookmarks_dlg_delete_idle(gpointer user_data)
{
	BookmarksDlg *dlg = static_cast<BookmarksDlg*>(user_data);
	delete dlg;
	return G_SOURCE_REMOVE;
}

static void bookmarks_dlg_destroy_cb(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	g_idle_add(bookmarks_dlg_delete_idle, user_data);
}

void BookmarksDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		/* The dialog is heap-allocated (see the ACTION_QUIVER_BOOKMARKS_EDIT
		 * handler) so that its signal handlers (which use this object as
		 * user_data) outlive the show-and-return Run(). */
		GtkWindow *mainWin = gtk_application_get_active_window(g_pApp);
		if (mainWin != NULL)
			gtk_window_set_transient_for(GTK_WINDOW(m_PrivPtr->m_pWidget), mainWin);
		g_signal_connect(m_PrivPtr->m_pWidget, "destroy", G_CALLBACK(bookmarks_dlg_destroy_cb), this);
		gtk_widget_set_visible(m_PrivPtr->m_pWidget, TRUE);
		gtk_window_present(GTK_WINDOW(m_PrivPtr->m_pWidget));
	}
}

// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer user_data);
static void selection_changed (GtkSelectionModel* selection, gpointer user_data);


BookmarksDlg::BookmarksDlgPriv::BookmarksDlgPriv(BookmarksDlg *parent) :
        m_pBookmarksDlg(parent),
        m_BookmarksEventHandler( new BookmarksEventHandler(this) )
{
	m_BookmarksPtr = Bookmarks::GetInstance();
	m_BookmarksPtr->AddEventHandler(m_BookmarksEventHandler);
	m_bLoadedDlg = false;
	m_pListStoreBookmarks = NULL;
	m_pSelectionBookmarks = NULL;

	m_pGtkBuilder = gtk_builder_new();
	const gchar* objectids[] = {
		"BookmarksDialog", NULL};
	gtk_builder_add_objects_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", objectids, NULL);
	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

BookmarksDlg::BookmarksDlgPriv::~BookmarksDlgPriv()
{
	m_BookmarksPtr->RemoveEventHandler(m_BookmarksEventHandler);
	if (NULL != m_pSelectionBookmarks)
	{
		g_object_unref(m_pSelectionBookmarks);
		m_pSelectionBookmarks = NULL;
	}
	if (NULL != m_pListStoreBookmarks)
	{
		g_object_unref(m_pListStoreBookmarks);
		m_pListStoreBookmarks = NULL;
	}
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}
}


void BookmarksDlg::BookmarksDlgPriv::LoadWidgets()
{

	if (NULL != m_pGtkBuilder)
	{
		m_pWidget                = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "BookmarksDialog"));
		m_pTreeViewBookmarks     = GTK_WIDGET(     gtk_builder_get_object (m_pGtkBuilder, "treeview_bookmarks") );

		m_pButtonClose           = GTK_BUTTON( gtk_button_new_with_mnemonic("_Close") );

		if (m_pTreeViewBookmarks)
		{
			m_pListStoreBookmarks = g_list_store_new(BOOKMARK_ITEM_TYPE);
			m_pSelectionBookmarks = gtk_multi_selection_new(
				G_LIST_MODEL(m_pListStoreBookmarks));
			gtk_column_view_set_model(GTK_COLUMN_VIEW(m_pTreeViewBookmarks),
				GTK_SELECTION_MODEL(m_pSelectionBookmarks));

			GtkColumnViewColumn* column =
				gtk_column_view_column_new("icon",
					bookmark_column_factory(COLUMN_ICON, this));
			gtk_column_view_append_column(GTK_COLUMN_VIEW(m_pTreeViewBookmarks), column);

			column = gtk_column_view_column_new("bookmark",
				bookmark_column_factory(COLUMN_NAME, this));
			gtk_column_view_append_column(GTK_COLUMN_VIEW(m_pTreeViewBookmarks), column);
		}

		m_pButtonMoveUp          = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "button_move_up") );
		m_pButtonMoveDown        = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "button_move_down") );
		m_pButtonAdd             = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "button_add") );
		m_pButtonEdit            = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "button_edit") );
		m_pButtonRemove          = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "button_remove") );
		//m_pButtonClose           = GTK_BUTTON(     gtk_builder_get_object (m_pGtkBuilder, "button_close") );

		m_bLoadedDlg = (
				NULL != m_pWidget && 
				NULL != m_pTreeViewBookmarks && 
				NULL != m_pButtonMoveDown && 
				NULL != m_pButtonMoveUp && 
				NULL != m_pButtonClose && 
				NULL != m_pButtonRemove && 
				NULL != m_pButtonEdit && 
				NULL != m_pButtonAdd); 
	}
}

void BookmarksDlg::BookmarksDlgPriv::SelectionChanged()
{
	GtkSelectionModel* sel = GTK_SELECTION_MODEL(m_pSelectionBookmarks);
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m_pListStoreBookmarks));
	guint selection_count = 0;
	bool bTop = false, bBottom = false;

	if (n)
	{
		bBottom = gtk_selection_model_is_selected(sel, n - 1);
	}
	for (guint i = 0 ; i < n ; i++)
	{
		if (gtk_selection_model_is_selected(sel, i))
		{
			selection_count++;
			if (0 == i)
				bTop = true;
		}
	}

	if (0 == selection_count)
	{
		// disable edit, remove
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonEdit),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),FALSE);
	}
	else if (1 == selection_count)
	{
		if (bTop)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),FALSE);
		}
		else
		{
			gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),TRUE);
		}
		if (bBottom)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),FALSE);
		}
		else
		{
			gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),TRUE);
		}

		// enable edit,remove
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonEdit),TRUE);
	}
	else
	{
		// disable edit, enable remove
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonRemove),TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonEdit),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveUp),FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(m_pButtonMoveDown),FALSE);
	}
}

void BookmarksDlg::BookmarksDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		BookmarksPtr bookmarksPtr = m_BookmarksPtr;
		
		g_list_store_remove_all(m_pListStoreBookmarks);
		vector<Bookmark> bookmarks = bookmarksPtr->GetBookmarks();
		vector<Bookmark>::iterator itr;

		for (itr = bookmarks.begin(); bookmarks.end() != itr; ++itr)
		{
			BookmarkItem* item = bookmark_item_new(
				itr->GetID(),
				itr->GetIcon().c_str(),
				itr->GetName().c_str());
			g_list_store_append(m_pListStoreBookmarks, G_OBJECT(item));
			g_object_unref(item);
		}

		SelectionChanged();

	}	

}


void BookmarksDlg::BookmarksDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pButtonMoveUp,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonMoveDown,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonAdd,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonEdit,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonRemove,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonClose,
			"clicked",(GCallback)on_clicked,this);

		g_signal_connect(G_OBJECT(m_pSelectionBookmarks),
			"selection-changed",G_CALLBACK(selection_changed),this);
	}
}


static void  on_clicked (GtkButton *button, gpointer user_data)
{
	BookmarksDlg::BookmarksDlgPriv *priv = static_cast<BookmarksDlg::BookmarksDlgPriv*>(user_data);
	BookmarksPtr bookmarksPtr = priv->m_BookmarksPtr;
	
	list<int> values;

	GtkSelectionModel* sel = GTK_SELECTION_MODEL(priv->m_pSelectionBookmarks);
	guint n = g_list_model_get_n_items(G_LIST_MODEL(priv->m_pListStoreBookmarks));

	for (guint i = 0 ; i < n ; i++)
	{
		if (gtk_selection_model_is_selected(sel, i))
		{
			BookmarkItem* item = BOOKMARK_ITEM(
				g_list_model_get_item(G_LIST_MODEL(priv->m_pListStoreBookmarks), i));
			values.push_back(item->id);
			g_object_unref(item);
		}
	}
	// have to remove after iterating the model because
	// removing modifiees the model
	if (button == priv->m_pButtonMoveUp)
	{ 
		for (list<int>::iterator itr = values.begin(); values.end() != itr; ++itr)
		{
			priv->m_BookmarksPtr->MoveUp(*itr);
			// make sure the item is selected again
			// because the model has changed
			for (guint i = 0 ; i < n ; i++)
			{
				BookmarkItem* item = BOOKMARK_ITEM(
					g_list_model_get_item(G_LIST_MODEL(priv->m_pListStoreBookmarks), i));
				int id = item->id;
				g_object_unref(item);
				if (id == *itr)
				{
					gtk_selection_model_select_item(sel, i, FALSE);
				}
			}
		}
	}
	else if (button == priv->m_pButtonMoveDown)
	{
		for (list<int>::iterator itr = values.begin(); values.end() != itr; ++itr)
		{
			priv->m_BookmarksPtr->MoveDown(*itr);
			// make sure the item is selected again
			// because the model has changed
			for (guint i = 0 ; i < n ; i++)
			{
				BookmarkItem* item = BOOKMARK_ITEM(
					g_list_model_get_item(G_LIST_MODEL(priv->m_pListStoreBookmarks), i));
				int id = item->id;
				g_object_unref(item);
				if (id == *itr)
				{
					gtk_selection_model_select_item(sel, i, FALSE);
				}
			}
		}
	}
	else if (button == priv->m_pButtonAdd)
	{
		BookmarkAddEditDlg dlg;
		dlg.Run();
		if (!dlg.Cancelled())
		{
			Bookmark newbm = dlg.GetBookmark();
			if (!newbm.GetName().empty())
			{
				priv->m_BookmarksPtr->AddBookmark(newbm);
			}
		}

	}
	else if (button == priv->m_pButtonEdit)
	{

		for (list<int>::iterator itr = values.begin(); values.end() != itr; ++itr)
		{
			const Bookmark* b = priv->m_BookmarksPtr->GetBookmark(*itr);
			if (NULL != b)
			{
				BookmarkAddEditDlg dlg(*b);
				dlg.Run();
				if (!dlg.Cancelled())
				{
					Bookmark newbm = dlg.GetBookmark();
					if (!newbm.GetName().empty())
					{
						priv->m_BookmarksPtr->UpdateBookmark(newbm);
					}
				}
			}
		}
	}
	else if (button == priv->m_pButtonRemove)
	{
		for (list<int>::iterator itr = values.begin(); values.end() != itr; ++itr)
		{
			priv->m_BookmarksPtr->Remove(*itr);
		}
	}
	else if (button == priv->m_pButtonClose)
	{
		gtk_window_destroy(GTK_WINDOW(priv->m_pWidget));
	}
}

static void selection_changed (GtkSelectionModel* selection, gpointer user_data)
{ (void)selection; 
	BookmarksDlg::BookmarksDlgPriv *priv = static_cast<BookmarksDlg::BookmarksDlgPriv*>(user_data);
	priv->SelectionChanged();
}


// nested class

void BookmarksDlg::BookmarksDlgPriv::BookmarksEventHandler::HandleBookmarkChanged(BookmarksEventPtr event)
{ (void)event; 
	if (parent->m_bLoadedDlg)
	{
		parent->UpdateUI();
	}
}


