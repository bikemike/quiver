#include "Statusbar.h"
#include <math.h>

Statusbar::Statusbar()
{
	
	m_pStatusbar = gtk_statusbar_new();
	//gtk_frame_set_shadow_type(GTK_FRAME(m_pStatusbar),GTK_SHADOW_OUT);
	
	 
	/*
	m_pLabelStatus = gtk_label_new ("");
	gtk_label_set_ellipsize(GTK_LABEL(m_pLabelStatus),PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment (GTK_MISC (m_pLabelStatus), 0, 0.5);
	//gtk_label_set_selectable (GTK_LABEL (m_pLabelStatus), TRUE);
	gtk_box_pack_start (GTK_BOX (m_pStatusbar), m_pLabelStatus, TRUE, TRUE, 2);
	*/

	/*
	GtkWidget *vseparator = gtk_vseparator_new ();
	gtk_box_pack_start (GTK_BOX (m_pStatusbar), vseparator, FALSE, FALSE, 5);
	*/

	GtkWidget* frame;
	

	m_pLabelZoom = gtk_label_new ("");
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(frame),m_pLabelZoom);
	gtk_box_pack_start (GTK_BOX (m_pStatusbar), frame, FALSE, FALSE, 0);


	m_pLabelListPosition = gtk_label_new ("");
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(frame),m_pLabelListPosition);
	gtk_box_pack_start (GTK_BOX (m_pStatusbar), frame, FALSE, FALSE, 0);
	
	
	m_pLabelLoadTime = gtk_label_new ("0.000s");
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(frame),m_pLabelLoadTime);
	gtk_box_pack_start (GTK_BOX (m_pStatusbar), frame, FALSE, FALSE, 0);

	m_pProgressbar = gtk_progress_bar_new ();
	gtk_widget_set_size_request (m_pProgressbar, 75, 0);
	
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(frame),m_pProgressbar);
	gtk_box_pack_start (GTK_BOX (m_pStatusbar), frame, FALSE, FALSE, 0);
	
	
	m_iDefaultContext = gtk_statusbar_get_context_id (GTK_STATUSBAR(m_pStatusbar),"default");

	m_pWidget = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(m_pWidget),GTK_SHADOW_OUT);;
	gtk_container_add(GTK_CONTAINER(m_pWidget), m_pStatusbar);
	
}

GtkWidget * Statusbar::GetWidget()
{
	return m_pWidget;	
}

void Statusbar::SetPosition(int pos, int n)
{
	char list_pos[50];
	sprintf(list_pos,"%d of %d",pos,n);
	gtk_label_set_text(GTK_LABEL(m_pLabelListPosition),list_pos);
}

void Statusbar::SetZoomPercent(int percent)
{
	if (0 <= percent)
	{
		char list_pos[50];
		sprintf(list_pos,"%d%%",percent);
		gtk_label_set_text(GTK_LABEL(m_pLabelZoom),list_pos);
	}
	else
	{
		gtk_label_set_text(GTK_LABEL(m_pLabelZoom),"");
	}
}	

void Statusbar::SetLoadTime()
{
	SetLoadTime(m_CurrentQuiverFile.GetLoadTimeInSeconds());
}

void Statusbar::SetLoadTime(double seconds)
{
	if (0 <= seconds)
	{
		char loadtime[20];
		sprintf(loadtime,"%0.3fs",seconds);
		gtk_label_set_text(GTK_LABEL(m_pLabelLoadTime),loadtime);
	}
	else
	{
		gtk_label_set_text(GTK_LABEL(m_pLabelLoadTime),"");
	}
}
void Statusbar::SetText(std::string s)
{
	gtk_statusbar_pop(GTK_STATUSBAR(m_pStatusbar),m_iDefaultContext);
	gtk_statusbar_push(GTK_STATUSBAR(m_pStatusbar),m_iDefaultContext,s.c_str());
}

void Statusbar::PushText(std::string s)
{
	gtk_statusbar_push(GTK_STATUSBAR(m_pStatusbar),m_iDefaultContext,s.c_str());
}
void Statusbar::PopText()
{
	gtk_statusbar_pop(GTK_STATUSBAR(m_pStatusbar),m_iDefaultContext);
}

void Statusbar::SetText()
{
	GnomeVFSFileInfo * info = m_CurrentQuiverFile.GetFileInfo();

	
	
	double bytes = (double)info->size;
	gnome_vfs_file_info_unref(info);
	
	double abytes;
	char unit = 'B';
	
	abytes = fabs(bytes);
	if (abytes < 1024)
	{
		unit = 'B';
	}
	else if (abytes < 1024 * 1024ULL)
	{
		unit = 'K';
		bytes /= 1024;
	}
	else if (abytes < 1024 * 1024 * 1024ULL)
	{
		unit = 'M';
		bytes /= 1024 * 1024;
	}
	else if (abytes < 1024 * 1024 * 1024 * 1024ULL)
	{
		unit = 'G';
		bytes /= 1024 * 1024 * 1024ULL;
	}
	else
	{
		unit = 'T';
		bytes /= 1024 * 1024 * 1024 * 1024ULL;
	}


	char status_text[1024];
	if (bytes > 10)
	{
		sprintf(status_text,"%s (%.0f%c) %dx%d",info->name, bytes, unit,m_CurrentQuiverFile.GetWidth(),m_CurrentQuiverFile.GetHeight());
	}
	else
	{
		sprintf(status_text,"%s (%.1f%c) %dx%d",info->name, bytes, unit,m_CurrentQuiverFile.GetWidth(),m_CurrentQuiverFile.GetHeight());
	}	
	
	gtk_statusbar_pop(GTK_STATUSBAR(m_pStatusbar),m_iDefaultContext);
	gtk_statusbar_push(GTK_STATUSBAR(m_pStatusbar),m_iDefaultContext,status_text);
	

}


void Statusbar::SignalAreaPrepared(GdkPixbufLoader *loader)
{

}
void Statusbar::SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height)
{

}

void Statusbar::SignalBytesRead(long bytes_read,long total)
{
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(m_pProgressbar),bytes_read / (double)total);
}
void Statusbar::SignalClosed(GdkPixbufLoader *loader)
{

	
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(m_pProgressbar),1);
	SetText();
	SetLoadTime();
}
void Statusbar::SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height)
{
	
	//gtk_box_pack_start (GTK_BOX (m_pStatusbar), m_pProgressbar, FALSE, FALSE, 3);	
}
void Statusbar::SetPixbuf(GdkPixbuf * pixbuf)
{
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(m_pProgressbar),1);
	SetText();
	SetLoadTime();
}
void Statusbar::SetPixbufFromThread(GdkPixbuf * pixbuf)
{
}


void Statusbar::SetQuiverFile(QuiverFile quiverFile)
{
	m_CurrentQuiverFile = quiverFile;
}

void Statusbar::SetCacheQuiverFile(QuiverFile quiverFile)
{
}
