#include <config.h>
#include "BookmarkAddEditDlg.h"
#include <boost/bind.hpp>

using namespace std;

class BookmarkAddEditDlg::BookmarkAddEditDlgPriv
{
public:
	BookmarkAddEditDlgPriv(BookmarkAddEditDlg *pPublic);
	~BookmarkAddEditDlgPriv();
	
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();
	
	BookmarkAddEditDlg*    m_pPublic;
	GtkBuilder*            m_pGtkBuilder;
	GtkWidget*             m_pDialog;
	GtkEntry*              m_pEntryName;
	GtkEntry*              m_pEntryLocation;
	Bookmark               m_Bookmark;
	std::function<void(int)> m_callback;
};

// --- Static Callbacks ---
static void on_dialog_response(GtkDialog* dialog, gint response_id, gpointer user_data);

BookmarkAddEditDlg::BookmarkAddEditDlg() : m_PrivPtr (new BookmarkAddEditDlgPriv(this))
{
}

BookmarkAddEditDlg::BookmarkAddEditDlg(Bookmark bookmark) : m_PrivPtr (new BookmarkAddEditDlgPriv(this))
{
	m_PrivPtr->m_Bookmark = bookmark;
}

void BookmarkAddEditDlg::Run(std::function<void(int)> callback)
{
    m_PrivPtr->m_callback = callback;
    gtk_widget_set_visible(m_PrivPtr->m_pDialog, TRUE);
}

Bookmark BookmarkAddEditDlg::GetBookmark() const
{
	return m_PrivPtr->m_Bookmark;
}

BookmarkAddEditDlg::BookmarkAddEditDlgPriv::BookmarkAddEditDlgPriv(BookmarkAddEditDlg* pPublic)
{
	m_pPublic = pPublic;

	m_pGtkBuilder = gtk_builder_new();
	GError *error = NULL;
	if (!gtk_builder_add_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", &error)) {
        g_warning("Could not load UI file: %s", error->message);
        g_error_free(error);
    }

	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

BookmarkAddEditDlg::BookmarkAddEditDlgPriv::~BookmarkAddEditDlgPriv()
{
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}
}

void BookmarkAddEditDlg::BookmarkAddEditDlgPriv::LoadWidgets()
{
	m_pDialog = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "dialog_add_bookmark"));
	m_pEntryName = GTK_ENTRY(gtk_builder_get_object (m_pGtkBuilder, "entry_bookmark_name"));
	m_pEntryLocation = GTK_ENTRY(gtk_builder_get_object (m_pGtkBuilder, "entry_bookmark_location"));
}

void BookmarkAddEditDlg::BookmarkAddEditDlgPriv::UpdateUI()
{
	gtk_editable_set_text(GTK_EDITABLE(m_pEntryName), m_Bookmark.GetName().c_str());
	gtk_editable_set_text(GTK_EDITABLE(m_pEntryLocation), m_Bookmark.GetDescription().c_str());
}

void BookmarkAddEditDlg::BookmarkAddEditDlgPriv::ConnectSignals()
{
	g_signal_connect(m_pDialog, "response", G_CALLBACK(on_dialog_response), this);
}

static void on_dialog_response(GtkDialog* dialog, gint response_id, gpointer user_data)
{
	BookmarkAddEditDlg::BookmarkAddEditDlgPriv* priv = (BookmarkAddEditDlg::BookmarkAddEditDlgPriv*)user_data;
	if (response_id == GTK_RESPONSE_OK)
	{
		priv->m_Bookmark.SetName(gtk_editable_get_text(GTK_EDITABLE(priv->m_pEntryName)));
		priv->m_Bookmark.SetDescription(gtk_editable_get_text(GTK_EDITABLE(priv->m_pEntryLocation)));
	}
    if (priv->m_callback) {
        priv->m_callback(response_id);
    }
	gtk_window_destroy(GTK_WINDOW(dialog));
}
