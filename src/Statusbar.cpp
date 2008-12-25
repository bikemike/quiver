#include <config.h>

#include "Statusbar.h"
#include <math.h>


class Statusbar::StatusbarImpl
{
public:
//constructor, destructor
	StatusbarImpl(Statusbar* pStatusbar);
	~StatusbarImpl();
//methods


//member variables
	Statusbar* m_pParent;
	
	QuiverFile m_CurrentQuiverFile;

	GtkWidget* m_pWidget;
	GtkWidget* m_pStatusbar;
	
	GtkWidget* m_pLabelDateTime;
	GtkWidget* m_pLabelLoadTime;

	GtkWidget* m_pLabelListPosition;
	GtkWidget* m_pProgressbar;
	
	GtkWidget* m_pLabelImageSize;
	GtkWidget* m_pLabelZoom;


	
	guint m_iDefaultContext;
	guint m_iTimeoutPulse;
	guint m_iPulseCount;

};


gboolean progress_bar_pulse (gpointer data);

Statusbar::StatusbarImpl::StatusbarImpl(Statusbar* pStatusbar)
{
	m_pParent = pStatusbar;
	
	m_iTimeoutPulse = 0;
	m_iPulseCount = 0;
	
	m_pStatusbar = gtk_statusbar_new();
	//gtk_frame_set_shadow_type(GTK_FRAME(m_pStatusbar),GTK_SHADOW_OUT);
	
#ifdef QUIVER_MAEMO
	gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(m_pStatusbar),FALSE);
#endif
	 
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
	
	m_pLabelDateTime = gtk_label_new ("");
	
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(frame),m_pLabelDateTime);
	gtk_box_pack_start (GTK_BOX (m_pStatusbar), frame, FALSE, FALSE, 0);
	
	m_pLabelImageSize  = gtk_label_new ("");
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(frame),m_pLabelImageSize);
	gtk_box_pack_start (GTK_BOX (m_pStatusbar), frame, FALSE, FALSE, 0);
	
	
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

	//m_pWidget = gtk_frame_new(NULL);
	//gtk_frame_set_shadow_type(GTK_FRAME(m_pWidget),GTK_SHADOW_OUT);;
	//gtk_container_add(GTK_CONTAINER(m_pWidget), m_pStatusbar);
	m_pWidget = m_pStatusbar;
	
}

Statusbar::StatusbarImpl::~StatusbarImpl()
{
}

Statusbar::Statusbar() : m_StatusbarImplPtr ( new StatusbarImpl(this) )
{
	

	
}

GtkWidget* Statusbar::GetWidget()
{
	return m_StatusbarImplPtr->m_pWidget;	
}

void Statusbar::SetPosition(int pos, int n)
{
	char list_pos[50];
	sprintf(list_pos,"%d of %d",pos,n);
	gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pLabelListPosition),list_pos);
}

void Statusbar::SetImageSize()
{
	if (m_StatusbarImplPtr->m_CurrentQuiverFile.GetURI())
	{
		SetImageSize(m_StatusbarImplPtr->m_CurrentQuiverFile.GetWidth(),m_StatusbarImplPtr->m_CurrentQuiverFile.GetHeight());
	}
}

void Statusbar::SetImageSize(int width, int height)
{
	if (-1 != width && -1 != height)
	{
		char szImgSize[32];
		g_snprintf (szImgSize,32,"%dx%d",width,height);
		gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pLabelImageSize),szImgSize);
	}
	else
	{
		gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pLabelImageSize),NULL);
	}
}


void Statusbar::SetMagnification(int percent)
{
	if (0 <= percent)
	{
		char list_pos[50];
		g_snprintf(list_pos,50,"%d%%",percent);
		gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pLabelZoom),list_pos);
	}
	else
	{
		gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pLabelZoom),"");
	}
}	

void Statusbar::SetLoadTime()
{
	SetLoadTime(m_StatusbarImplPtr->m_CurrentQuiverFile.GetLoadTimeInSeconds());
}

void Statusbar::SetLoadTime(double seconds)
{
	if (0 <= seconds)
	{
		char loadtime[20];
		g_snprintf(loadtime,20,"%0.3fs",seconds);
		gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pLabelLoadTime),loadtime);
	}
	else
	{
		gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pLabelLoadTime),"");
	}
}
void Statusbar::SetText(std::string s)
{
	gtk_statusbar_pop(GTK_STATUSBAR(m_StatusbarImplPtr->m_pStatusbar),m_StatusbarImplPtr->m_iDefaultContext);
	gtk_statusbar_push(GTK_STATUSBAR(m_StatusbarImplPtr->m_pStatusbar),m_StatusbarImplPtr->m_iDefaultContext,s.c_str());
}

void Statusbar::PushText(std::string s)
{
	gtk_statusbar_push(GTK_STATUSBAR(m_StatusbarImplPtr->m_pStatusbar),m_StatusbarImplPtr->m_iDefaultContext,s.c_str());
}
void Statusbar::PopText()
{
	gtk_statusbar_pop(GTK_STATUSBAR(m_StatusbarImplPtr->m_pStatusbar),m_StatusbarImplPtr->m_iDefaultContext);
}

void Statusbar::SetText()
{
	GnomeVFSFileInfo * info = m_StatusbarImplPtr->m_CurrentQuiverFile.GetFileInfo();

	if (NULL != info)
	{
		double bytes = (double)info->size;
		
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
			g_snprintf(status_text,1024,"%s (%.0f%c)",info->name, bytes, unit);
		}
		else
		{
			g_snprintf(status_text,1024,"%s (%.1f%c)",info->name, bytes, unit);
		}	

		gnome_vfs_file_info_unref(info);		
		
		gtk_statusbar_pop(GTK_STATUSBAR(m_StatusbarImplPtr->m_pStatusbar),m_StatusbarImplPtr->m_iDefaultContext);
		gtk_statusbar_push(GTK_STATUSBAR(m_StatusbarImplPtr->m_pStatusbar),m_StatusbarImplPtr->m_iDefaultContext,status_text);
	}
	else
	{
		gtk_statusbar_pop(GTK_STATUSBAR(m_StatusbarImplPtr->m_pStatusbar),m_StatusbarImplPtr->m_iDefaultContext);
		gtk_statusbar_push(GTK_STATUSBAR(m_StatusbarImplPtr->m_pStatusbar),m_StatusbarImplPtr->m_iDefaultContext,"");
	}

}

void Statusbar::SetDateTime()
{
	if (m_StatusbarImplPtr->m_CurrentQuiverFile.GetURI())
	{
		time_t time = m_StatusbarImplPtr->m_CurrentQuiverFile.GetTimeT();
		SetDateTime( time );
	}	
}

void Statusbar::SetDateTime(time_t time)
{
	char sz_time[64];
	struct tm tm_time;
	localtime_r(&time, &tm_time);

    // Format and print the time, "ddd yyyy-mm-dd hh:mm:ss"
    strftime(sz_time, sizeof(sz_time), "%Y-%m-%d %H:%M:%S", &tm_time);
    

	gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pLabelDateTime),sz_time);
} 
	

void Statusbar::StartProgressPulse()
{
	if (0 == m_StatusbarImplPtr->m_iTimeoutPulse)
	{
		m_StatusbarImplPtr->m_iTimeoutPulse = g_timeout_add(100, progress_bar_pulse, m_StatusbarImplPtr.get());
	}
	m_StatusbarImplPtr->m_iPulseCount++;
}


void Statusbar::StopProgressPulse()
{
	m_StatusbarImplPtr->m_iPulseCount--;
	if (0 == m_StatusbarImplPtr->m_iPulseCount && 0 != m_StatusbarImplPtr->m_iTimeoutPulse)
	{
		g_source_remove(m_StatusbarImplPtr->m_iTimeoutPulse);
		m_StatusbarImplPtr->m_iTimeoutPulse = 0;
	}
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(m_StatusbarImplPtr->m_pProgressbar),1);
}	


void Statusbar::SignalAreaPrepared(GdkPixbufLoader *loader)
{

}
void Statusbar::SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height)
{

}

void Statusbar::SignalBytesRead(long bytes_read,long total)
{
	gdk_threads_enter();
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(m_StatusbarImplPtr->m_pProgressbar),bytes_read / (double)total);
	gdk_threads_leave();
}
void Statusbar::SignalClosed(GdkPixbufLoader *loader)
{
	gdk_threads_enter();
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(m_StatusbarImplPtr->m_pProgressbar),1);
	SetText();
	SetLoadTime();
	gdk_threads_leave();
}
void Statusbar::SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height)
{
	
	//gtk_box_pack_start (GTK_BOX (m_pStatusbar), m_pProgressbar, FALSE, FALSE, 3);	
}
void Statusbar::SetPixbuf(GdkPixbuf * pixbuf)
{
	gdk_threads_enter();
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(m_StatusbarImplPtr->m_pProgressbar),1.);
	SetLoadTime();
	gdk_threads_leave();
}

void Statusbar::SetPixbufAtSize(GdkPixbuf * pixbuf,gint width, gint height, bool bResetViewMode/* = true*/)
{
	gdk_threads_enter();
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(m_StatusbarImplPtr->m_pProgressbar),1.);
	SetLoadTime();
	gdk_threads_leave();
}

void Statusbar::SetQuiverFile(QuiverFile quiverFile)
{
	m_StatusbarImplPtr->m_CurrentQuiverFile = quiverFile;
	SetText();
	SetLoadTime();
	SetDateTime();
	SetImageSize();
}





//=============================================================================
// callback functions
//=============================================================================

gboolean progress_bar_pulse (gpointer data)
{
	Statusbar::StatusbarImpl* pStatusbarImpl = (Statusbar::StatusbarImpl*)data;
	gdk_threads_enter();
	gtk_progress_bar_pulse(GTK_PROGRESS_BAR(pStatusbarImpl->m_pProgressbar));
	gdk_threads_leave();
	return TRUE;
}
