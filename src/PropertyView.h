#ifndef PROPERTY_VIEW_H
#define PROPERTY_VIEW_H

#include <gtk/gtk.h>

class QuiverFile;

class PropertyView
{
public:
	PropertyView();
	~PropertyView();

	GtkWidget *GetWidget();
	void SetQuiverFile(const QuiverFile &file);
	void Clear();

private:
	GtkWidget *m_pScrolledWindow;
	GtkWidget *m_pLabel;
};

#endif
