#include "PropertyView.h"

#include "QuiverFile.h"

PropertyView::PropertyView()
	: m_pScrolledWindow(nullptr)
	, m_pLabel(nullptr)
{
	m_pLabel = gtk_label_new("");
	gtk_label_set_wrap(GTK_LABEL(m_pLabel), TRUE);
	gtk_label_set_selectable(GTK_LABEL(m_pLabel), TRUE);
	gtk_widget_set_halign(m_pLabel, GTK_ALIGN_START);
	gtk_widget_set_valign(m_pLabel, GTK_ALIGN_START);

	m_pScrolledWindow = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_pScrolledWindow), m_pLabel);
	gtk_widget_set_vexpand(m_pScrolledWindow, TRUE);
}

PropertyView::~PropertyView()
{
}

GtkWidget *PropertyView::GetWidget()
{
	return m_pScrolledWindow;
}

void PropertyView::SetQuiverFile(const QuiverFile &file)
{
	std::string text;
	text += "Path: ";
	text += file.GetFilePath();
	text += "\n";
	gtk_label_set_text(GTK_LABEL(m_pLabel), text.c_str());
}

void PropertyView::Clear()
{
	gtk_label_set_text(GTK_LABEL(m_pLabel), "");
}
