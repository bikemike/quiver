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
    m_pWidget = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(m_pWidget), "Donate");
    gtk_window_set_transient_for(GTK_WINDOW(m_pWidget), m_pParent);
    gtk_window_set_modal(GTK_WINDOW(m_pWidget), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(m_pWidget), TRUE);

    gtk_dialog_add_button(GTK_DIALOG(m_pWidget), "_Close", GTK_RESPONSE_CLOSE);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);

    GtkWidget* label = gtk_label_new("If you enjoy using Quiver, please consider making a donation to support its development.");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget* button = gtk_button_new_with_label("Donate via PayPal");
    gtk_box_append(GTK_BOX(vbox), button);
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked), NULL);

    gtk_window_set_child(GTK_WINDOW(m_pWidget), vbox);
}

void DonateDlg::DonateDlgPriv::ConnectSignals()
{
	g_signal_connect(m_pWidget, "response", G_CALLBACK(gtk_window_destroy), NULL);
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
