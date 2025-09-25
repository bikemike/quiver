#include "config.h"

#include "DonateDlg.h"

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include <string>

#define DONATION_URL "http://www.kainjow.com/quiver/donate.html"

class DonateDlg::DonateDlgPriv
{
public:
	DonateDlgPriv(GtkWindow* pParent);
	~DonateDlgPriv();
	
	void LoadWidgets();
	void ConnectSignals();

	GtkWindow* m_pParent;
	GtkWidget* m_pWidget;
};

// --- Static Callbacks ---
static void  on_clicked (GtkButton *button, gpointer user_data);
static void on_response(GtkDialog *dialog, gint response_id, gpointer user_data);

DonateDlg::DonateDlg(GtkWindow* pParent) : m_PrivPtr(new DonateDlg::DonateDlgPriv(pParent))
{
}

GtkWidget* DonateDlg::GetWidget() const
{
	return m_PrivPtr->m_pWidget;
}

void DonateDlg::Run()
{
    gtk_widget_set_visible(m_PrivPtr->m_pWidget, TRUE);
}


DonateDlg::DonateDlgPriv::DonateDlgPriv(GtkWindow* pParent) : m_pParent(pParent)
{
	LoadWidgets();
	ConnectSignals();
}

DonateDlg::DonateDlgPriv::~DonateDlgPriv()
{
	if (NULL != m_pWidget)
	{
		gtk_window_destroy(GTK_WINDOW(m_pWidget));
		m_pWidget = NULL;
	}
}


void DonateDlg::DonateDlgPriv::LoadWidgets()
{
    m_pWidget = gtk_dialog_new_with_buttons("Donate", m_pParent, GTK_DIALOG_MODAL, "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(m_pWidget), 300, 150);

    GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(m_pWidget));

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);
    gtk_box_append(GTK_BOX(content_area), vbox);

    GtkWidget* label = gtk_label_new("If you enjoy using Quiver, please consider making a donation to support its development.");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget* button = gtk_button_new_with_label("Donate via PayPal");
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked), NULL);
    gtk_box_append(GTK_BOX(vbox), button);
}

void DonateDlg::DonateDlgPriv::ConnectSignals()
{
	g_signal_connect(m_pWidget, "response", G_CALLBACK(on_response), NULL);
}

static void on_clicked(GtkButton *button, gpointer user_data)
{
    GError *error = NULL;
    g_app_info_launch_default_for_uri(DONATION_URL, NULL, &error);
    if (error) {
        g_warning("Failed to open donation URL: %s", error->message);
        g_error_free(error);
    }
}

static void on_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    gtk_window_destroy(GTK_WINDOW(dialog));
}
