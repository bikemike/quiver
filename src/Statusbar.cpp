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


	
	guint m_iTimeoutPulse;
	guint m_iPulseCount;

	guint m_uiIdleSourceID;

};


gboolean progress_bar_pulse (gpointer data);

Statusbar::StatusbarImpl::StatusbarImpl(Statusbar* pStatusbar) : m_uiIdleSourceID(0)
{
	m_pParent = pStatusbar;
	
	m_iTimeoutPulse = 0;
	m_iPulseCount = 0;
	
	m_pStatusbar = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(m_pStatusbar), 0.);
	gtk_label_set_ellipsize(GTK_LABEL(m_pStatusbar), PANGO_ELLIPSIZE_END);
	m_pWidget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_box_append (GTK_BOX (m_pWidget), m_pStatusbar);
	gtk_widget_set_hexpand(m_pStatusbar, TRUE);

	GtkWidget* frame;

	m_pLabelDateTime = gtk_label_new ("");

	frame = gtk_frame_new(NULL);
	gtk_frame_set_child(GTK_FRAME(frame),m_pLabelDateTime);
	gtk_box_append (GTK_BOX (m_pWidget), frame);
	
	m_pLabelImageSize  = gtk_label_new ("");
	frame = gtk_frame_new(NULL);
	gtk_frame_set_child(GTK_FRAME(frame),m_pLabelImageSize);
	gtk_box_append (GTK_BOX (m_pWidget), frame);
	
	
	m_pLabelZoom = gtk_label_new ("");
	frame = gtk_frame_new(NULL);
	gtk_frame_set_child(GTK_FRAME(frame),m_pLabelZoom);
	gtk_box_append (GTK_BOX (m_pWidget), frame);


	m_pLabelListPosition = gtk_label_new ("");
	frame = gtk_frame_new(NULL);
	gtk_frame_set_child(GTK_FRAME(frame),m_pLabelListPosition);
	gtk_box_append (GTK_BOX (m_pWidget), frame);
	
	
	m_pLabelLoadTime = gtk_label_new ("0.000s");
	frame = gtk_frame_new(NULL);
	gtk_frame_set_child(GTK_FRAME(frame),m_pLabelLoadTime);
	gtk_box_append (GTK_BOX (m_pWidget), frame);

	m_pProgressbar = gtk_progress_bar_new ();
	gtk_widget_set_size_request (m_pProgressbar, 75, -1);
	gtk_widget_set_valign(m_pProgressbar, GTK_ALIGN_CENTER);
	gtk_box_append (GTK_BOX (m_pWidget), m_pProgressbar);

	/* m_pWidget is returned floating; it is later parented into the window
	 * tree (Quiver::Init), which owns it.  Do not ref or unref it here. */
	
}

Statusbar::StatusbarImpl::~StatusbarImpl()
{
	if (0 != m_iTimeoutPulse)
	{
		g_source_remove(m_iTimeoutPulse);
		m_iTimeoutPulse = 0;
	}
	if (0 != m_uiIdleSourceID)
	{
		g_source_remove(m_uiIdleSourceID);
		m_uiIdleSourceID = 0;
	}
	/* m_pWidget is parented into the window tree and owned by it; no manual
	 * ref is kept and it must not be unref'd here. */
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
		if (m_StatusbarImplPtr->m_CurrentQuiverFile.IsWidthHeightSet())
			SetImageSize(m_StatusbarImplPtr->m_CurrentQuiverFile.GetWidth(),m_StatusbarImplPtr->m_CurrentQuiverFile.GetHeight());
		else
			SetImageSize(-1, -1);
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
	gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pStatusbar), s.c_str());
}

void Statusbar::PushText(std::string s)
{
	gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pStatusbar), s.c_str());
}
void Statusbar::PopText()
{
	gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pStatusbar), "");
}

void Statusbar::SetText()
{
	GFileInfo* info = m_StatusbarImplPtr->m_CurrentQuiverFile.GetFileInfo();

	if (NULL != info)
	{
		double bytes = g_file_info_get_size(info);
		
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
	
		const char* display_name = g_file_info_get_display_name(info);
	
		char status_text[1024];
		if (bytes > 10)
		{
			g_snprintf(status_text,1024,"%s (%.0f%c)",display_name, bytes, unit);
		}
		else
		{
			g_snprintf(status_text,1024,"%s (%.1f%c)",display_name, bytes, unit);
		}	

		g_object_unref(info);		
		
		gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pStatusbar), status_text);
	}
	else
	{
		gtk_label_set_text(GTK_LABEL(m_StatusbarImplPtr->m_pStatusbar), "");
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
{ (void)loader; 

}
void Statusbar::SignalAreaUpdated(GdkPixbufLoader *loader,gint x, gint y, gint width,gint height)
{ (void)height;  (void)width;  (void)y;  (void)x;  (void)loader; 

}

class IdleBytesReadData
{
public:
	IdleBytesReadData() : pStatusBarImpl(NULL), progress(0.), setLoadTime(false), clearText(false)
	{
	}
	Statusbar::StatusbarImpl* pStatusBarImpl;
	double progress;
	bool setLoadTime;
	bool clearText;
};

static gboolean idle_update_progress(gpointer data)
{
	IdleBytesReadData* pData = static_cast<IdleBytesReadData*>(data);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(pData->pStatusBarImpl->m_pProgressbar), pData->progress);
	if (pData->setLoadTime)
	{
		pData->pStatusBarImpl->m_CurrentQuiverFile.GetLoadTimeInSeconds();
		char loadtime[20];
		double seconds = pData->pStatusBarImpl->m_CurrentQuiverFile.GetLoadTimeInSeconds();
		if (0 <= seconds)
		{
			g_snprintf(loadtime,20,"%0.3fs",seconds);
			gtk_label_set_text(GTK_LABEL(pData->pStatusBarImpl->m_pLabelLoadTime),loadtime);
		}
	}
	if (pData->clearText)
	{
		gtk_label_set_text(GTK_LABEL(pData->pStatusBarImpl->m_pLabelLoadTime),"");
	}
	pData->pStatusBarImpl->m_uiIdleSourceID = 0;
	return FALSE;
}

void idle_deleter(gpointer data)
{
	IdleBytesReadData* pData = static_cast<IdleBytesReadData*>(data);
	delete pData;
}

void Statusbar::SignalBytesRead(long bytes_read,long total)
{
	IdleBytesReadData* data = new IdleBytesReadData();
	data->pStatusBarImpl = m_StatusbarImplPtr.get();
	data->progress =  (bytes_read / (double) total);
	m_StatusbarImplPtr->m_uiIdleSourceID = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, idle_update_progress, data, idle_deleter);
}
void Statusbar::SignalClosed(GdkPixbufLoader *loader)
{ (void)loader; 
	IdleBytesReadData* data = new IdleBytesReadData();
	data->pStatusBarImpl = m_StatusbarImplPtr.get();
	data->progress =  1.;
	data->clearText = true;
	data->setLoadTime = true;
	if (0 != m_StatusbarImplPtr->m_uiIdleSourceID)
	{
		g_source_remove(m_StatusbarImplPtr->m_uiIdleSourceID);
	}
	m_StatusbarImplPtr->m_uiIdleSourceID = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, idle_update_progress, data, idle_deleter);
}
void Statusbar::SignalSizePrepared(GdkPixbufLoader *loader,gint width, gint height)
{ (void)height;  (void)width;  (void)loader; 
	
	//gtk_box_pack_start (GTK_BOX (m_pStatusbar), m_pProgressbar, FALSE, FALSE, 3);	
}
void Statusbar::SetPixbuf(GdkPixbuf * pixbuf)
{ (void)pixbuf; 
	IdleBytesReadData* data = new IdleBytesReadData();
	data->pStatusBarImpl = m_StatusbarImplPtr.get();
	data->progress =  1.;
	data->setLoadTime = true;
	if (0 != m_StatusbarImplPtr->m_uiIdleSourceID)
	{
		g_source_remove(m_StatusbarImplPtr->m_uiIdleSourceID);
	}
	m_StatusbarImplPtr->m_uiIdleSourceID = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, idle_update_progress, data, idle_deleter);
}

void Statusbar::SetPixbufAtSize(GdkPixbuf * pixbuf,gint width, gint height, bool bResetViewMode/* = true*/)
{ (void)bResetViewMode;  (void)height;  (void)width;  (void)pixbuf; 
	IdleBytesReadData* data = new IdleBytesReadData();
	data->pStatusBarImpl = m_StatusbarImplPtr.get();
	data->progress =  1.;
	data->setLoadTime = true;
	if (0 != m_StatusbarImplPtr->m_uiIdleSourceID)
	{
		g_source_remove(m_StatusbarImplPtr->m_uiIdleSourceID);
	}
	m_StatusbarImplPtr->m_uiIdleSourceID = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, idle_update_progress, data, idle_deleter);
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
	gtk_progress_bar_pulse(GTK_PROGRESS_BAR(pStatusbarImpl->m_pProgressbar));
	return TRUE;
}
