#include <config.h>

#include <libquiver/quiver-icon-view.h>
#include <libquiver/quiver-image-view.h>

#include "Viewer.h"
#include "Timer.h"
//#include "ath.h"
#include "icons/nav_button.xpm"

#include "QuiverUtils.h"
#include "ImageLoader.h"


using namespace std;

#define ORIENTATION_ROTATE_CW	0
#define ORIENTATION_ROTATE_CCW 	1
#define ORIENTATION_FLIP_H    	2
#define ORIENTATION_FLIP_V    	3



static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void image_view_adjustment_changed (GtkAdjustment *adjustment, gpointer user_data);



static int orientation_matrix[4][9] = 
{ 
	{0,6,7,8,5,2,3,4,1}, //cw rotation
	{0,8,5,6,7,4,1,2,3}, //ccw rotation
	{0,2,1,4,3,6,5,8,7}, //flip h
	{0,4,3,2,1,8,7,6,5}, //flip v
};

static char *ui_viewer =
"<ui>"
"	<menubar name='MenubarMain'>"
"		<menu action='MenuEdit'>"
"			<placeholder name='CopyPaste'>"
"				<menuitem action='Cut'/>"
"				<menuitem action='Copy'/>"
"			</placeholder>"
"			<placeholder name='CopyPaste'>"
"				<menuitem action='ImageTrash'/>"
"			</placeholder>"
"		</menu>"
"		<menu action='MenuView'>"
"			<placeholder name='UIItems'>"
"				<menuitem action='ViewFilmStrip'/>"
"			</placeholder>"
"			<placeholder name='Zoom'>"
"				<menu action='MenuZoom'>"
"					<menuitem action='ZoomFit'/>"
"					<menuitem action='ZoomFitStretch'/>"
"					<menuitem action='Zoom100'/>"
"					<menuitem action='ZoomIn'/>"
"					<menuitem action='ZoomOut'/>"
"				</menu>"
"			</placeholder>"
"		</menu>"
"		<menu action='MenuGo'>"
"			<placeholder name='ImageNavigation'>"
"				<menuitem action='ImagePrevious'/>"
"				<menuitem action='ImageNext'/>"
"				<menuitem action='ImageFirst'/>"
"				<menuitem action='ImageLast'/>"
"			</placeholder>"
"			<separator/>"
"			<placeholder name='FolderNavigation'/>"
"			<separator/>"
"		</menu>"
"	</menubar>"
"	<toolbar name='ToolbarMain'>"
"	</toolbar>"
"</ui>";

static  GtkToggleActionEntry action_entries_toggle[] = {
	{ "ViewFilmStrip", GTK_STOCK_PROPERTIES,"Film Strip", "<Control><Shift>s", "Show/Hide Film Strip", G_CALLBACK(NULL),TRUE},
};

static GtkActionEntry action_entries[] = {
	
/*	{ "MenuFile", NULL, N_("_File") }, */
	{ "ViewerStuff", GTK_STOCK_DIRECTORY, "viewer stuff", "", "view images", G_CALLBACK(NULL)},
	{ "Cut", GTK_STOCK_CUT, "_Cut", "<Control>X", "Cut image", G_CALLBACK(NULL)},
	{ "Copy", GTK_STOCK_COPY, "Copy", "<Control>C", "Copy image", G_CALLBACK(NULL)},
	{ "ImageTrash", GTK_STOCK_DELETE, "_Move To Trash", "Delete", "Move image to the Trash", G_CALLBACK(NULL)},
	
	{ "ImagePrevious", GTK_STOCK_GO_BACK, "_Previous Image", NULL, "Go to previous image", G_CALLBACK(NULL)},
	{ "ImageNext", GTK_STOCK_GO_FORWARD, "_Next Image", NULL, "Go to next image", G_CALLBACK(NULL)},
	{ "ImageFirst", GTK_STOCK_GOTO_FIRST, "_First Image", "Home", "Go to first image", G_CALLBACK(NULL)},
	{ "ImageLast", GTK_STOCK_GOTO_LAST, "_Last Image", "End", "Go to last image", G_CALLBACK(NULL)},

	{ "ZoomFit", GTK_STOCK_ZOOM_FIT,"Zoom _Fit", "", "Fit to Screen", G_CALLBACK(NULL)},
	{ "ZoomFitStretch", GTK_STOCK_ZOOM_FIT,"Zoom _Fit Stretch", "", "Fit to Screen", G_CALLBACK(NULL)},
	{ "Zoom100", GTK_STOCK_ZOOM_100, "Zoom _100%", "", "Full Size", G_CALLBACK(NULL)},
	{ "ZoomIn", GTK_STOCK_ZOOM_IN,"Zoom _In", "", "Zoom In", G_CALLBACK(NULL)},
	{ "ZoomOut", GTK_STOCK_ZOOM_OUT,"Zoom _Out", "", "Zoom Out", G_CALLBACK(NULL)},

};


class ImageViewPixbufLoaderObserver : public IPixbufLoaderObserver
{
public:
	ImageViewPixbufLoaderObserver(QuiverImageView *imageview){m_ImageView = imageview;};
	virtual ~ImageViewPixbufLoaderObserver(){};

	virtual void ConnectSignals(GdkPixbufLoader *loader){
		quiver_image_view_connect_pixbuf_loader_signals(m_ImageView,loader);};
	virtual void ConnectSignalSizePrepared(GdkPixbufLoader * loader){quiver_image_view_connect_pixbuf_size_prepared_signal(m_ImageView,loader);};

	// custom calls
	virtual void SetPixbuf(GdkPixbuf * pixbuf){quiver_image_view_set_pixbuf(m_ImageView,pixbuf);};
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height){
		quiver_image_view_set_pixbuf_at_size(m_ImageView,pixbuf,width,height);
	};
	// the image that will be displayed immediately
	virtual void SetQuiverFile(QuiverFile quiverFile){};
	
	// the image that is being cached for future use
	virtual void SetCacheQuiverFile(QuiverFile quiverFile){};
	virtual void SignalBytesRead(long bytes_read,long total){};
private:
	QuiverImageView *m_ImageView;
};

typedef boost::shared_ptr<ImageViewPixbufLoaderObserver> ImageViewPixbufLoaderObserverPtr;


class ViewerImpl
{
public:

	typedef enum _ScrollbarType
	{
		HORIZONTAL,
		VERTICAL,
		BOTH,
	}ScrollbarType;
		

	ViewerImpl();
	//private member variables

	GtkWidget *m_pIconView;
	GtkWidget *m_pImageView;

	GtkWidget * m_pTable;
	GtkAdjustment * m_pAdjustmentH;
	GtkAdjustment * m_pAdjustmentV;

	GtkWidget * m_pScrollbarH;
	GtkWidget * m_pScrollbarV;

	GtkWidget *m_pNavigationBox;

	GtkWidget *m_pHBox;
	GtkWidget *m_pVBox;
	
	gdouble m_dAdjustmentValueLastH;
	gdouble m_dAdjustmentValueLastV;
	
	NavigationControl * m_pNavigationControl;
	
	bool m_bPressed;

	GdkPoint m_pointLastPointer;


	double m_dZoomLevel;
	
	int m_iLastLine;
	
	bool m_bConfigureTimeoutStarted;
	bool m_bConfigureTimeoutEnded;
	bool m_bConfigureTimeoutRestarted;
	QuiverFile m_QuiverFile;
	QuiverFile m_QuiverFileCached;
	int m_iCurrentOrientation;
	
	GtkShadowType m_GtkShadowType;
	
	GtkUIManager *m_pUIManager;
	guint m_iMergedViewerUI;

	ImageViewPixbufLoaderObserverPtr m_ImageViewPixbufLoaderObserverPtr;
	ImageLoader m_ImageLoader;
	ImageList m_ImageList;
	
};


static void callback(gpointer data)
{
	printf("callback!\n");
}

static gboolean viewer_scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer data )
{

	ViewerImpl *pViewerImpl;
	pViewerImpl = (ViewerImpl*)data;

	if (event->state & GDK_CONTROL_MASK || event->state & GDK_SHIFT_MASK)
		return FALSE;
	
	
	if (GDK_SCROLL_UP == event->direction)
	{
		if (pViewerImpl->m_ImageList.Previous())
		{
			QuiverFile f;
			f = pViewerImpl->m_ImageList.GetCurrent();
			pViewerImpl->m_ImageLoader.LoadImage(f);
			
			quiver_icon_view_set_cursor_cell(
				QUIVER_ICON_VIEW(pViewerImpl->m_pIconView)
				,pViewerImpl->m_ImageList.GetCurrentIndex());
			
			// cache the previous image if there is one
			if (pViewerImpl->m_ImageList.HasPrevious())
			{
				f = pViewerImpl->m_ImageList.GetPrevious();
				pViewerImpl->m_ImageLoader.CacheImage(f);	
			}		
			//ImageChanged();
		}
	}
	else if (GDK_SCROLL_DOWN == event->direction)
	{
		if (pViewerImpl->m_ImageList.Next())
		{
			QuiverFile f;
	
			f = pViewerImpl->m_ImageList.GetCurrent();
			pViewerImpl->m_ImageLoader.LoadImage(f);

			quiver_icon_view_set_cursor_cell(
				QUIVER_ICON_VIEW(pViewerImpl->m_pIconView)
				,pViewerImpl->m_ImageList.GetCurrentIndex());	
			// cache the next image if there is one
			if (pViewerImpl->m_ImageList.HasNext())
			{
				f = pViewerImpl->m_ImageList.GetNext();
				pViewerImpl->m_ImageLoader.CacheImage(f);
			}		
			//ImageChanged();
		}
	}
	return TRUE;
}

ViewerImpl::ViewerImpl()
{
	m_pNavigationControl = new NavigationControl();
	

	m_pIconView = quiver_icon_view_new();
	m_pImageView = quiver_image_view_new();

	m_iCurrentOrientation = 1;
	
	m_pUIManager = NULL;
	m_iMergedViewerUI = 0;
	

	m_pAdjustmentH = quiver_image_view_get_hadjustment(QUIVER_IMAGE_VIEW(m_pImageView));
	m_pAdjustmentV = quiver_image_view_get_vadjustment(QUIVER_IMAGE_VIEW(m_pImageView));

	m_pScrollbarV = gtk_vscrollbar_new (m_pAdjustmentV);
	m_pScrollbarH = gtk_hscrollbar_new (m_pAdjustmentH);
	
	m_pNavigationBox = gtk_event_box_new ();

	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_xpm_data ((const char**) nav_button_xpm);
	GtkWidget *image = gtk_image_new_from_pixbuf (pixbuf);
	g_object_unref (G_OBJECT (pixbuf));
	gtk_container_add (GTK_CONTAINER (m_pNavigationBox),image);
	gtk_widget_show(image);

	gtk_widget_set_no_show_all(m_pScrollbarV,TRUE);
	gtk_widget_set_no_show_all(m_pScrollbarH,TRUE);
	gtk_widget_set_no_show_all(m_pNavigationBox,TRUE);


//	g_signal_connect (G_OBJECT (m_pNavigationBox), 
//			  "button_press_event",
//			  G_CALLBACK (/*FIXME*/NULL), 
//			  this);

	

	m_pTable = gtk_table_new (2, 2, FALSE);
	
	gtk_table_attach (GTK_TABLE (m_pTable), m_pImageView, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);


	gtk_table_attach (GTK_TABLE (m_pTable), m_pScrollbarV, 1, 2, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
			  
	gtk_table_attach (GTK_TABLE (m_pTable), m_pScrollbarH, 0, 1, 1, 2,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
			  
	gtk_table_attach (GTK_TABLE (m_pTable), m_pNavigationBox, 1, 2, 1, 2,
			  (GtkAttachOptions) (0),
			  (GtkAttachOptions) (0), 0, 0);

	m_pointLastPointer.x = m_pointLastPointer.y=0;
	
//	GTK_WIDGET_SET_FLAGS(m_pTable,GTK_CAN_FOCUS);
	m_pHBox = gtk_hbox_new(FALSE,0);
	m_pVBox = gtk_vbox_new(FALSE,0);

	gtk_box_pack_start (GTK_BOX (m_pVBox), m_pTable, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (m_pHBox), m_pIconView, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (m_pHBox), m_pVBox, TRUE, TRUE, 0);



	GdkColor dark_grey;
	gdk_color_parse("#444",&dark_grey);
	gtk_widget_modify_bg (m_pIconView, GTK_STATE_NORMAL, &dark_grey );
/*
	GdkColor black;
	gdk_color_parse("black",&black);
*/
	gtk_widget_modify_bg (m_pImageView, GTK_STATE_NORMAL, &dark_grey );

	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);

	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)icon_pixbuf_callback,this,NULL);

	quiver_icon_view_set_n_columns(QUIVER_ICON_VIEW(m_pIconView),1);
	quiver_icon_view_set_smooth_scroll(QUIVER_ICON_VIEW(m_pIconView),TRUE);

	
	g_signal_connect(G_OBJECT(m_pIconView),"cell_activated",G_CALLBACK(callback),this);
	g_signal_connect(G_OBJECT(m_pIconView),"cursor_changed",G_CALLBACK(callback),this);
	
    g_signal_connect (G_OBJECT (m_pImageView), "scroll_event",
    			G_CALLBACK (viewer_scroll_event), this);

    g_signal_connect (G_OBJECT (m_pAdjustmentH), "changed",
    			G_CALLBACK (image_view_adjustment_changed), this);

    g_signal_connect (G_OBJECT (m_pAdjustmentV), "changed",
    			G_CALLBACK (image_view_adjustment_changed), this);

	//g_signal_connect(G_OBJECT(m_pIconView),"selection_changed",G_CALLBACK(iconview_selection_changed_cb),this);
	ImageViewPixbufLoaderObserverPtr tmp( new ImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView)));
	m_ImageViewPixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_ImageViewPixbufLoaderObserverPtr.get());
	
}


void Viewer::event_nav_button_clicked (GtkWidget *widget, GdkEventButton *event, void *data)
{
	((Viewer*)data)->EventNavButtonClicked(widget,event,NULL);
}

void Viewer::EventNavButtonClicked (GtkWidget *widget, GdkEventButton *event, void *data)
{
//	m_ViewerImplPtr->m_pNavigationControl->Show(GTK_VIEWPORT(m_pViewport),m_pixbuf,event->x_root,event->y_root);
}



Viewer::Viewer() : m_ViewerImplPtr(new ViewerImpl())
{
	

}

Viewer::~Viewer()
{
	delete 	m_ViewerImplPtr->m_pNavigationControl;
}

GtkWidget *Viewer::GetWidget()
{
	return m_ViewerImplPtr->m_pHBox;
}

void Viewer::SetImageList(ImageList imgList)
{
	m_ViewerImplPtr->m_ImageList = imgList;
}

void Viewer::Show()
{
	GError *tmp_error;
	tmp_error = NULL;

	gtk_widget_show(m_ViewerImplPtr->m_pHBox);
	if (m_ViewerImplPtr->m_pUIManager && 0 == m_ViewerImplPtr->m_iMergedViewerUI)
	{
		m_ViewerImplPtr->m_iMergedViewerUI = gtk_ui_manager_add_ui_from_string(m_ViewerImplPtr->m_pUIManager,
				ui_viewer,
				strlen(ui_viewer),
				&tmp_error);
		if (NULL != tmp_error)
		{
			g_warning("Browser::Show() Error: %s\n",tmp_error->message);
		}
	}

	quiver_icon_view_set_cursor_cell(
		QUIVER_ICON_VIEW(m_ViewerImplPtr->m_pIconView),
		m_ViewerImplPtr->m_ImageList.GetCurrentIndex() );
}
void Viewer::Hide()
{
	gtk_widget_hide(m_ViewerImplPtr->m_pHBox);
	if (m_ViewerImplPtr->m_pUIManager)
	{	
		gtk_ui_manager_remove_ui(m_ViewerImplPtr->m_pUIManager,
			m_ViewerImplPtr->m_iMergedViewerUI);
		m_ViewerImplPtr->m_iMergedViewerUI = 0;
	}
}
void Viewer::SetUIManager(GtkUIManager *ui_manager)
{
	GError *tmp_error;
	tmp_error = NULL;
	
	if (m_ViewerImplPtr->m_pUIManager)
	{
		g_object_unref(m_ViewerImplPtr->m_pUIManager);
	}

	m_ViewerImplPtr->m_pUIManager = ui_manager;
	
	g_object_ref(m_ViewerImplPtr->m_pUIManager);
	
	m_ViewerImplPtr->m_iMergedViewerUI = gtk_ui_manager_add_ui_from_string(m_ViewerImplPtr->m_pUIManager,
			ui_viewer,
			strlen(ui_viewer),
			&tmp_error);	
	if (NULL != tmp_error)
	{
		g_warning("Browser::Show() Error: %s\n",tmp_error->message);
	}
	guint n_entries = G_N_ELEMENTS (action_entries);

	
	GtkActionGroup* actions = gtk_action_group_new ("BrowserActions");
	
	gtk_action_group_add_actions(actions, action_entries, n_entries, this);
                                 
	gtk_action_group_add_toggle_actions(actions,
										action_entries_toggle, 
										G_N_ELEMENTS (action_entries_toggle),
										this);
	gtk_ui_manager_insert_action_group (m_ViewerImplPtr->m_pUIManager,actions,0);	
}

GtkTableChild * GetGtkTableChild(GtkTable * table,GtkWidget	*widget_to_get);

int Viewer::GetCurrentOrientation()
{
	return m_ViewerImplPtr->m_iCurrentOrientation;	
}

GtkTableChild * GetGtkTableChild(GtkTable * table,GtkWidget	*widget_to_get)
{
	GtkTableChild *table_child = NULL;
	GList *list;
	for (list = table->children; list; list = list->next)
	{
		table_child = (GtkTableChild*)list->data;
		if (table_child->widget == widget_to_get)
			break;
	}
	return table_child;
}
/*
 * FIXME
bool Viewer::GetHasFullPixbuf()
{
	// FIXME: need to create a "current rotation" state
	// and adjust the comparision accordingly
	int w,h;
	w = m_QuiverFile.GetWidth();
	h = m_QuiverFile.GetHeight();
	if (m_iCurrentOrientation >= 5)
	{
		w = h;
		h = m_QuiverFile.GetWidth();
	}
	
	if ( gdk_pixbuf_get_width(m_pixbuf_real) == w
		&& gdk_pixbuf_get_height(m_pixbuf_real) == h )
	{
		return true;
	}
	return false;
}
*/





static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data)
{
	ViewerImpl* pViewerImpl = (ViewerImpl*)user_data;
	return pViewerImpl->m_ImageList.GetSize();
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
{
	ViewerImpl* pViewerImpl = (ViewerImpl*)user_data;
	QuiverFile f = pViewerImpl->m_ImageList[cell];

	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width, &height);
	return f.GetIcon(width,height);
}

static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
{
	ViewerImpl* pViewerImpl = (ViewerImpl*)user_data;
	QuiverFile f = pViewerImpl->m_ImageList[cell];
	
	GdkPixbuf *pixbuf;
	
	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width,&height);
	
	if (width <= 128 && height <= 128)
	{
		pixbuf = f.GetThumbnail(false);
	}
	else
	{
		pixbuf = f.GetThumbnail(true);
	}
	return pixbuf;
}



static void image_view_adjustment_changed (GtkAdjustment *adjustment, gpointer user_data)
{
	ViewerImpl* pViewerImpl = (ViewerImpl*)user_data;

	GtkAdjustment *h = pViewerImpl->m_pAdjustmentH;
	GtkAdjustment *v = pViewerImpl->m_pAdjustmentV;
	
	GtkWidget *pScrollbarH = pViewerImpl->m_pScrollbarH;
	GtkWidget *pScrollbarV = pViewerImpl->m_pScrollbarV;
	GtkWidget * pNavigationBox = pViewerImpl->m_pNavigationBox;

	GtkTableChild * child = GetGtkTableChild(GTK_TABLE(pViewerImpl->m_pTable),pViewerImpl->m_pImageView);
	
	if (h->page_size >= h->upper &&
		v->page_size >= v->upper)
	{
		// hide h hide v
		gtk_widget_hide (pScrollbarV); 	
		gtk_widget_hide (pScrollbarH);
		gtk_widget_hide (pNavigationBox);

		child->bottom_attach = 1;
		child->right_attach = 1;
	}
	else if (h->page_size >= h->upper)
	{
		// hide h show v
		child->bottom_attach = 2;
		gtk_widget_show (pNavigationBox);	
		gtk_widget_show (pScrollbarV);
		gtk_widget_hide (pScrollbarH);
		
	}
	else if (v->page_size >= v->upper)
	{
		// show h hide v
		child->right_attach = 2;
		gtk_widget_show (pNavigationBox);	
		gtk_widget_show (pScrollbarH);
		gtk_widget_hide (pScrollbarV);

	}
	else
	{
		// show h show v
		child->bottom_attach = 1;
		child->right_attach = 1;

		gtk_widget_show (pScrollbarV); 	
		gtk_widget_show (pScrollbarH);
		gtk_widget_show (pNavigationBox);

	}

	
}
