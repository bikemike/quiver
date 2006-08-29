#include <config.h>

#include <libquiver/quiver-icon-view.h>
#include <libquiver/quiver-image-view.h>

#include "Viewer.h"
#include "Timer.h"
#include "icons/nav_button.xpm"

#include "QuiverUtils.h"
#include "ImageLoader.h"
#include "ImageList.h"

#include "NavigationControl.h"
#include "QuiverFile.h"

#include <gdk/gdkkeysyms.h>
using namespace std;

#define ORIENTATION_ROTATE_CW	0
#define ORIENTATION_ROTATE_CCW 	1
#define ORIENTATION_FLIP_H    	2
#define ORIENTATION_FLIP_V    	3


static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data);
static guint n_cells_callback(QuiverIconView *iconview, gpointer user_data);
static void image_view_adjustment_changed (GtkAdjustment *adjustment, gpointer user_data);

static void viewer_action_handler_cb(GtkAction *action, gpointer data);

static gboolean viewer_scrollwheel_event(GtkWidget *widget, GdkEventScroll *event, gpointer data );
static gboolean viewer_imageview_activated(QuiverImageView *imageview,gpointer data);
static gboolean viewer_imageview_reload(QuiverImageView *imageview,gpointer data);
static gboolean viewer_iconview_cell_activated(QuiverIconView *iconview,gint cell,gpointer data);
static gboolean viewer_iconview_cursor_changed(QuiverIconView *iconview,gint cell,gpointer data);

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
"		<placeholder name='MenuImage'>"
"			<menu action='MenuImage'>"
"				<menuitem action='RotateCW'/>"
"				<menuitem action='RotateCCW'/>"
"				<separator/>"
"				<menuitem action='FlipH'/>"
"				<menuitem action='FlipV'/>"
"			</menu>"
"		</placeholder>"
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
	{ "ViewFilmStrip", GTK_STOCK_PROPERTIES,"Film Strip", "<Control><Shift>s", "Show/Hide Film Strip", G_CALLBACK(viewer_action_handler_cb),TRUE},
};

static GtkActionEntry action_entries[] = {
	
/*	{ "MenuFile", NULL, N_("_File") }, */
	{ "ViewerStuff", GTK_STOCK_DIRECTORY, "viewer stuff", "", "view images", G_CALLBACK(viewer_action_handler_cb)},
	{ "Cut", GTK_STOCK_CUT, "_Cut", "<Control>X", "Cut image", G_CALLBACK(viewer_action_handler_cb)},
	{ "Copy", GTK_STOCK_COPY, "Copy", "<Control>C", "Copy image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ImageTrash", GTK_STOCK_DELETE, "_Move To Trash", "Delete", "Move image to the Trash", G_CALLBACK(viewer_action_handler_cb)},
	
	{ "ImagePrevious", GTK_STOCK_GO_BACK, "_Previous Image", "BackSpace", "Go to previous image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ImageNext", GTK_STOCK_GO_FORWARD, "_Next Image", "space", "Go to next image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ImageFirst", GTK_STOCK_GOTO_FIRST, "_First Image", "Home", "Go to first image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ImageLast", GTK_STOCK_GOTO_LAST, "_Last Image", "End", "Go to last image", G_CALLBACK(viewer_action_handler_cb)},

	{ "ZoomFit", GTK_STOCK_ZOOM_FIT,"Zoom _Fit", "<Control>0", "Fit to Screen", G_CALLBACK(viewer_action_handler_cb)},
	{ "ZoomFitStretch", GTK_STOCK_ZOOM_FIT,"Zoom _Fit Stretch", "", "Fit to Screen", G_CALLBACK(viewer_action_handler_cb)},
	{ "Zoom100", GTK_STOCK_ZOOM_100, "_Actual Size", "", "Full Size", G_CALLBACK(viewer_action_handler_cb)},
	{ "ZoomIn", GTK_STOCK_ZOOM_IN,"Zoom _In", "equal", "Zoom In", G_CALLBACK(viewer_action_handler_cb)},
	{ "ZoomOut", GTK_STOCK_ZOOM_OUT,"Zoom _Out", "minus", "Zoom Out", G_CALLBACK(viewer_action_handler_cb)},
	
	{ "RotateCW", NULL, "_Rotate Clockwise", "r", "Rotate Clockwise", G_CALLBACK(viewer_action_handler_cb)},
	{ "RotateCCW", NULL, "Rotate _Counterclockwise", "l", "Rotate Counterclockwise", G_CALLBACK(viewer_action_handler_cb)},
	{ "FlipH", NULL, "Flip _Horizontally", "h", "Flip Horizontally", G_CALLBACK(viewer_action_handler_cb)},
	{ "FlipV", NULL, "Flip _Vertically", "v", "Flip Vertically", G_CALLBACK(viewer_action_handler_cb)},
	

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
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height, bool bResetViewMode = true ){
		gboolean bReset = FALSE;
		if (bResetViewMode) bReset = TRUE;
		quiver_image_view_set_pixbuf_at_size_ex(m_ImageView,pixbuf,width,height,bReset);
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
		

	ViewerImpl(Viewer *pViewer);

	void SetImageIndex(int index, bool bDirectionForward, bool bBlockCursorChangeSignal = true );

	// member variables

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

	int m_iCurrentOrientation;
	
	GtkShadowType m_GtkShadowType;
	
	GtkUIManager *m_pUIManager;
	guint m_iMergedViewerUI;

	ImageViewPixbufLoaderObserverPtr m_ImageViewPixbufLoaderObserverPtr;
	ImageLoader m_ImageLoader;
	ImageList m_ImageList;
	
	Viewer *m_pViewer;
	
};

void ViewerImpl::SetImageIndex(int index, bool bDirectionForward, bool bBlockCursorChangeSignal /* = true */)
{
	if (m_ImageList.SetCurrentIndex(index))
	{
		m_pViewer->EmitCursorChangedEvent();

		if (bBlockCursorChangeSignal)
		{
			g_signal_handlers_block_by_func(m_pIconView,(gpointer)viewer_iconview_cursor_changed,this);
		}
		
		QuiverFile f;

		f = m_ImageList.GetCurrent();
		m_ImageLoader.LoadImage(f);
		
		m_iCurrentOrientation = f.GetOrientation();

		quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
		      m_ImageList.GetCurrentIndex() );	

		if (bDirectionForward)
		{
			// cache the next image if there is one
			if (m_ImageList.HasNext())
			{
				f = m_ImageList.GetNext();
				m_ImageLoader.CacheImage(f);
			}
		}
		else
		{
			// cache the next image if there is one
			if (m_ImageList.HasPrevious())
			{
				f = m_ImageList.GetPrevious();
				m_ImageLoader.CacheImage(f);
			}
			
		}
		
		if (bBlockCursorChangeSignal)
		{
			g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)viewer_iconview_cursor_changed,this);
		}
	}
}


static void viewer_action_handler_cb(GtkAction *action, gpointer data)
{
	ViewerImpl *pViewerImpl;
	pViewerImpl = (ViewerImpl*)data;
	
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView);
	
	//printf("Action: %s\n",gtk_action_get_name(action));
	
	const gchar * szAction = gtk_action_get_name(action);
	
	if (0 == strcmp(szAction,"ZoomFit"))
	{
		quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW);
	}
	else if (0 == strcmp(szAction,"ZoomFitStretch"))
	{
		quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_FIT_WINDOW_STRETCH);
	}
	else if (0 == strcmp(szAction, "Zoom100"))
	{
		quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE);
	}
	else if (0 == strcmp(szAction, "ZoomIn"))
	{		
		if (QUIVER_IMAGE_VIEW_MODE_ZOOM != quiver_image_view_get_view_mode(imageview))
			quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM);
				
		quiver_image_view_set_magnification(imageview,
						quiver_image_view_get_magnification(imageview)*2);
	}
	else if (0 == strcmp(szAction, "ZoomOut"))
	{
		if (QUIVER_IMAGE_VIEW_MODE_ZOOM != quiver_image_view_get_view_mode(imageview))
			quiver_image_view_set_view_mode(imageview,QUIVER_IMAGE_VIEW_MODE_ZOOM);
				
		quiver_image_view_set_magnification(imageview,
			quiver_image_view_get_magnification(imageview)/2);
	}
	else if (0 == strcmp(szAction,"RotateCW"))
	{
		quiver_image_view_rotate(imageview,TRUE);
		pViewerImpl->m_iCurrentOrientation = orientation_matrix[ORIENTATION_ROTATE_CW][pViewerImpl->m_iCurrentOrientation];
	}
	else if (0 == strcmp(szAction,"RotateCCW"))
	{
		quiver_image_view_rotate(imageview,FALSE);
		pViewerImpl->m_iCurrentOrientation = orientation_matrix[ORIENTATION_ROTATE_CCW][pViewerImpl->m_iCurrentOrientation];
	}
	else if (0 == strcmp(szAction,"FlipH"))
	{
		quiver_image_view_flip(imageview,TRUE);
		pViewerImpl->m_iCurrentOrientation = orientation_matrix[ORIENTATION_FLIP_H][pViewerImpl->m_iCurrentOrientation];
	}
	else if (0 == strcmp(szAction,"FlipV"))
	{
		quiver_image_view_flip(imageview,FALSE);
		pViewerImpl->m_iCurrentOrientation = orientation_matrix[ORIENTATION_FLIP_V][pViewerImpl->m_iCurrentOrientation];
	}
	else if (0 == strcmp(szAction, "ImageFirst"))
	{
		pViewerImpl->SetImageIndex(0,true,true);
	}
	else if (0 == strcmp(szAction, "ImagePrevious"))
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageList.GetCurrentIndex()-1,false,true);
	}
	else if (0 == strcmp(szAction, "ImageNext"))
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageList.GetCurrentIndex()+1,true,true);
	}
	else if (0 == strcmp(szAction, "ImageLast"))
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageList.GetSize()-1,false,true);
	}
}

static gboolean viewer_scrollwheel_event(GtkWidget *widget, GdkEventScroll *event, gpointer data )
{

	ViewerImpl *pViewerImpl;
	pViewerImpl = (ViewerImpl*)data;

	if (event->state & GDK_CONTROL_MASK || event->state & GDK_SHIFT_MASK)
		return FALSE;

	g_signal_handlers_block_by_func(pViewerImpl->m_pIconView,(gpointer)viewer_iconview_cursor_changed,pViewerImpl);
	
	if (GDK_SCROLL_UP == event->direction)
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageList.GetCurrentIndex()-1,false,true);
	}
	else if (GDK_SCROLL_DOWN == event->direction)
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageList.GetCurrentIndex()+1,true,true);
	}

	g_signal_handlers_unblock_by_func(pViewerImpl->m_pIconView,(gpointer)viewer_iconview_cursor_changed,pViewerImpl);

	return TRUE;
}


static gboolean viewer_imageview_activated(QuiverImageView *imageview,gpointer data)
{
	ViewerImpl *pViewerImpl;
	pViewerImpl = (ViewerImpl*)data;
	
	pViewerImpl->m_pViewer->EmitItemActivatedEvent();
	return TRUE;
}

static gboolean viewer_imageview_reload(QuiverImageView *imageview,gpointer data)
{
	ViewerImpl *pViewerImpl;
	pViewerImpl = (ViewerImpl*)data;
	ImageLoader::LoadParams params = {0};

	params.orientation = pViewerImpl->m_iCurrentOrientation;
	params.reload = true;
	params.fullsize = true;
	params.no_thumb_preview = true;
	params.state = ImageLoader::LOAD;

	pViewerImpl->m_ImageLoader.LoadImage(pViewerImpl->m_ImageList.GetCurrent(),params);

	return TRUE;
}


static gboolean viewer_iconview_cell_activated(QuiverIconView *iconview,gint cell,gpointer data)
{
	ViewerImpl *pViewerImpl;
	pViewerImpl = (ViewerImpl*)data;

	//pViewerImpl->m_pViewer->EmitItemActivatedEvent();
	return TRUE;
}

static gboolean viewer_iconview_cursor_changed(QuiverIconView *iconview,gint cell,gpointer data)
{
	ViewerImpl *pViewerImpl;
	pViewerImpl = (ViewerImpl*)data;
	
	pViewerImpl->SetImageIndex(cell,true,false);

	return TRUE;
}

ViewerImpl::ViewerImpl(Viewer *pViewer)
{
	m_pViewer = pViewer;
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
//	gtk_box_pack_start (GTK_BOX (m_pVBox), m_pIconView, FALSE, TRUE, 0);
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
//	quiver_icon_view_set_n_rows(QUIVER_ICON_VIEW(m_pIconView),1);
	quiver_icon_view_set_smooth_scroll(QUIVER_ICON_VIEW(m_pIconView),TRUE);

	g_signal_connect(G_OBJECT(m_pIconView),"cell_activated",G_CALLBACK(viewer_iconview_cell_activated),this);
	g_signal_connect(G_OBJECT(m_pIconView),"cursor_changed",G_CALLBACK(viewer_iconview_cursor_changed),this);
	
    g_signal_connect (G_OBJECT (m_pImageView), "scroll_event",
    			G_CALLBACK (viewer_scrollwheel_event), this);

    g_signal_connect (G_OBJECT (m_pImageView), "activated",
    			G_CALLBACK (viewer_imageview_activated), this);

    g_signal_connect (G_OBJECT (m_pImageView), "reload",
    			G_CALLBACK (viewer_imageview_reload), this);


    g_signal_connect (G_OBJECT (m_pAdjustmentH), "changed",
    			G_CALLBACK (image_view_adjustment_changed), this);

    g_signal_connect (G_OBJECT (m_pAdjustmentV), "changed",
    			G_CALLBACK (image_view_adjustment_changed), this);

	//g_signal_connect(G_OBJECT(m_pIconView),"selection_changed",G_CALLBACK(iconview_selection_changed_cb),this);
	ImageViewPixbufLoaderObserverPtr tmp( new ImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView)));
	m_ImageViewPixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_ImageViewPixbufLoaderObserverPtr.get());
	
	gtk_widget_show_all(m_pHBox);
	gtk_widget_hide(m_pHBox);
	gtk_widget_set_no_show_all(m_pHBox,TRUE);
	
}


void Viewer::event_nav_button_clicked (GtkWidget *widget, GdkEventButton *event, void *data)
{
//	((Viewer*)data)->EventNavButtonClicked(widget,event,NULL);
}


Viewer::Viewer() : m_ViewerImplPtr(new ViewerImpl(this))
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


	gint cursor_cell = quiver_icon_view_get_cursor_cell(QUIVER_ICON_VIEW(m_ViewerImplPtr->m_pIconView));
 
	if ( (gint)m_ViewerImplPtr->m_ImageList.GetCurrentIndex()
		!= cursor_cell  )
	{
		quiver_image_view_set_pixbuf(QUIVER_IMAGE_VIEW(m_ViewerImplPtr->m_pImageView),NULL);
	}

	if (0 != m_ViewerImplPtr->m_ImageList.GetSize())
	{
		quiver_icon_view_set_cursor_cell(
			QUIVER_ICON_VIEW(m_ViewerImplPtr->m_pIconView),
			m_ViewerImplPtr->m_ImageList.GetCurrentIndex() );
	}

	gtk_widget_show(m_ViewerImplPtr->m_pHBox);
	if (m_ViewerImplPtr->m_pUIManager && 0 == m_ViewerImplPtr->m_iMergedViewerUI)
	{
		m_ViewerImplPtr->m_iMergedViewerUI = gtk_ui_manager_add_ui_from_string(m_ViewerImplPtr->m_pUIManager,
				ui_viewer,
				strlen(ui_viewer),
				&tmp_error);
		if (NULL != tmp_error)
		{
			g_warning("Viewer::Show() Error: %s\n",tmp_error->message);
		}
	}

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

	guint n_entries = G_N_ELEMENTS (action_entries);

	
	GtkActionGroup* actions = gtk_action_group_new ("BrowserActions");
	
	gtk_action_group_add_actions(actions, action_entries, n_entries, m_ViewerImplPtr.get());
                                 
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
		child->right_attach = 1;
		gtk_widget_show (pNavigationBox);	
		gtk_widget_show (pScrollbarV);
		gtk_widget_hide (pScrollbarH);
		
	}
	else if (v->page_size >= v->upper)
	{
		// show h hide v
		child->right_attach = 2;
		child->bottom_attach = 1;
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
