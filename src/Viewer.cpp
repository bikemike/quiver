#include <config.h>

#include <libquiver/quiver-icon-view.h>
#include <libquiver/quiver-image-view.h>
#include <libquiver/quiver-navigation-control.h>

#include "Viewer.h"
#include "Timer.h"
#include "icons/nav_button.xpm"

#include "QuiverUtils.h"
#include "ImageLoader.h"
#include "ImageList.h"

#include "QuiverFile.h"

#include "QuiverPrefs.h"
#include "IPreferencesEventHandler.h"

#include "QuiverStockIcons.h"
#include "QuiverFileOps.h"

#include "IImageListEventHandler.h"


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

static gboolean viewer_navigation_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer userdata);
gboolean navigation_control_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer data );




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
"				<menuitem action='ViewerCut'/>"
"				<menuitem action='ViewerCopy'/>"
"			</placeholder>"
"			<placeholder name='Trash'>"
"				<menuitem action='ViewerTrash'/>"
"			</placeholder>"
"		</menu>"
"		<menu action='MenuView'>"
"			<placeholder name='UIItems'>"
"				<menuitem action='ViewFilmStrip'/>"
"			</placeholder>"
"			<placeholder name='Zoom'>"
"				<menu action='MenuZoom'>"
"					<menuitem action='ZoomIn'/>"
"					<menuitem action='ZoomOut'/>"
"					<menuitem action='Zoom100'/>"
"					<menuitem action='ZoomFit'/>"
"					<menuitem action='ZoomFitStretch'/>"
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
"				<menuitem action='ImageFirst'/>"
"				<menuitem action='ImagePrevious'/>"
"				<menuitem action='ImageNext'/>"
"				<menuitem action='ImageLast'/>"
"			</placeholder>"
"			<separator/>"
"			<placeholder name='FolderNavigation'/>"
"			<separator/>"
"		</menu>"
"	</menubar>"
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='NavToolItems'>"
"			<separator/>"
"			<toolitem action='ImagePrevious'/>"
"			<toolitem action='ImageNext'/>"
"			<separator/>"
"		</placeholder>"
"		<placeholder name='ZoomToolItems'>"
"			<toolitem action='ZoomIn'/>"
"			<toolitem action='ZoomOut'/>"
"			<toolitem action='Zoom100'/>"
"			<toolitem action='ZoomFit'/>"
//"			<toolitem action='ZoomFitStretch'/>"
"		</placeholder>"
"		<placeholder name='TransformToolItems'>"
"			<toolitem action='RotateCCW'/>"
"			<toolitem action='RotateCW'/>"
"		</placeholder>"
"		<placeholder name='Trash'>"
"			<toolitem action='ViewerTrash'/>"
"		</placeholder>"
"	</toolbar>"
"	<accelerator action='RotateCW_2'/>"
"	<accelerator action='RotateCCW_2'/>"
"	<accelerator action='FlipH_2'/>"
"	<accelerator action='FlipV_2'/>"
"	<accelerator action='ImagePrevious_2'/>"
"	<accelerator action='ImageNext_2'/>"
"</ui>";

static  GtkToggleActionEntry action_entries_toggle[] = {
	{ "ViewFilmStrip", GTK_STOCK_PROPERTIES,"Film Strip", "<Control><Shift>f", "Show/Hide Film Strip", G_CALLBACK(viewer_action_handler_cb),TRUE},
};

static GtkActionEntry action_entries[] = {
	
/*	{ "MenuFile", NULL, N_("_File") }, */
	{ "ViewerCut", GTK_STOCK_CUT, "_Cut", "<Control>X", "Cut image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ViewerCopy", GTK_STOCK_COPY, "Copy", "<Control>C", "Copy image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ViewerTrash", GTK_STOCK_DELETE, "_Move To Trash", "Delete", "Move image to the Trash", G_CALLBACK(viewer_action_handler_cb)},
	
	{ "ImagePrevious", GTK_STOCK_GO_BACK, "_Previous Image", "BackSpace", "Go to previous image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ImagePrevious_2", GTK_STOCK_GO_BACK, "_Previous Image", "<Shift>space", "Go to previous image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ImageNext", GTK_STOCK_GO_FORWARD, "_Next Image", "space", "Go to next image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ImageNext_2", GTK_STOCK_GO_FORWARD, "_Next Image", "<Shift>BackSpace", "Go to next image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ImageFirst", GTK_STOCK_GOTO_FIRST, "_First Image", "Home", "Go to first image", G_CALLBACK(viewer_action_handler_cb)},
	{ "ImageLast", GTK_STOCK_GOTO_LAST, "_Last Image", "End", "Go to last image", G_CALLBACK(viewer_action_handler_cb)},

	{ "ZoomFit", GTK_STOCK_ZOOM_FIT,"Zoom _Fit", "<Control>1", "Fit to Window", G_CALLBACK(viewer_action_handler_cb)},
	{ "ZoomFitStretch", GTK_STOCK_ZOOM_FIT,"Zoom _Fit Stretch", "", "Fit to Window Stretch", G_CALLBACK(viewer_action_handler_cb)},
	{ "Zoom100", GTK_STOCK_ZOOM_100, "_Actual Size", "<Control>0", "Actual Size", G_CALLBACK(viewer_action_handler_cb)},
	{ "ZoomIn", GTK_STOCK_ZOOM_IN,"Zoom _In", "equal", "Zoom In", G_CALLBACK(viewer_action_handler_cb)},
	{ "ZoomOut", GTK_STOCK_ZOOM_OUT,"Zoom _Out", "minus", "Zoom Out", G_CALLBACK(viewer_action_handler_cb)},
	
	{ "RotateCW", QUIVER_STOCK_ROTATE_CW, "_Rotate Clockwise", "r", "Rotate Clockwise", G_CALLBACK(viewer_action_handler_cb)},
	{ "RotateCW_2", QUIVER_STOCK_ROTATE_CW, "_Rotate Clockwise", "<Shift>l", "Rotate Clockwise", G_CALLBACK(viewer_action_handler_cb)},
	{ "RotateCCW", QUIVER_STOCK_ROTATE_CCW, "Rotate _Counterclockwise", "l", "Rotate Counterclockwise", G_CALLBACK(viewer_action_handler_cb)},
	{ "RotateCCW_2", QUIVER_STOCK_ROTATE_CCW, "Rotate _Counterclockwise", "<Shift>r", "Rotate Counterclockwise", G_CALLBACK(viewer_action_handler_cb)},
	{ "FlipH", NULL, "Flip _Horizontally", "h", "Flip Horizontally", G_CALLBACK(viewer_action_handler_cb)},
	{ "FlipH_2", NULL, "Flip _Horizontally", "<Shift>v", "Flip Horizontally", G_CALLBACK(viewer_action_handler_cb)},
	{ "FlipV", NULL, "Flip _Vertically", "v", "Flip Vertically", G_CALLBACK(viewer_action_handler_cb)},
	{ "FlipV_2", NULL, "Flip _Vertically", "<Shift>h", "Flip Vertically", G_CALLBACK(viewer_action_handler_cb)},
};


class ViewerImageViewPixbufLoaderObserver : public IPixbufLoaderObserver
{
public:
	ViewerImageViewPixbufLoaderObserver(QuiverImageView *imageview){m_pImageView = imageview;};
	virtual ~ViewerImageViewPixbufLoaderObserver(){};

	virtual void ConnectSignals(GdkPixbufLoader *loader){
		quiver_image_view_connect_pixbuf_loader_signals(m_pImageView,loader);};
	virtual void ConnectSignalSizePrepared(GdkPixbufLoader * loader){quiver_image_view_connect_pixbuf_size_prepared_signal(m_pImageView,loader);};

	// custom calls
	virtual void SetPixbuf(GdkPixbuf * pixbuf){quiver_image_view_set_pixbuf(m_pImageView,pixbuf);};
	virtual void SetPixbufAtSize(GdkPixbuf *pixbuf, gint width, gint height, bool bResetViewMode = true ){
		gboolean bReset = FALSE;
		if (bResetViewMode) bReset = TRUE;
		quiver_image_view_set_pixbuf_at_size_ex(m_pImageView,pixbuf,width,height,bReset);
	};
	// the image that will be displayed immediately
	virtual void SetQuiverFile(QuiverFile quiverFile){};
	
	// the image that is being cached for future use
	virtual void SetCacheQuiverFile(QuiverFile quiverFile){};
	virtual void SignalBytesRead(long bytes_read,long total){};
private:
	QuiverImageView *m_pImageView;
};

typedef boost::shared_ptr<IPixbufLoaderObserver> IPixbufLoaderObserverPtr;


class Viewer::ViewerImpl
{
public:

	typedef enum _ScrollbarType
	{
		HORIZONTAL,
		VERTICAL,
		BOTH,
	}ScrollbarType;
		
// constructor / destructor 
	ViewerImpl(Viewer *pViewer);
	~ViewerImpl();

// methods
	void SetImageList(ImageList imgList);
	void SetImageIndex(int index, bool bDirectionForward, bool bBlockCursorChangeSignal = true );
	int  GetCurrentOrientation();
	void SetCurrentOrientation(int iOrientation, bool bUpdateExif = true);
	void AddFilmstrip();

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
	
	GtkWidget *m_pNavigationWindow;
	GtkWidget *m_pNavigationControl;
	
	QuiverFile m_QuiverFile;

	int m_iCurrentOrientation;
	
	GtkUIManager *m_pUIManager;
	guint m_iMergedViewerUI;

	IPixbufLoaderObserverPtr m_PixbufLoaderObserverPtr;
	ImageLoader m_ImageLoader;
	ImageList m_ImageList;

	Viewer *m_pViewer;
	
	guint m_iTimeoutSlideshowID;
	int   m_iSlideShowDuration;
	bool  m_bSlideShowLoop;

/* nested classes */
	//class ViewerEventHandler;
	class ImageListEventHandler : public IImageListEventHandler
	{
	public:
		ImageListEventHandler(Viewer::ViewerImpl* parent){this->parent = parent;};
		virtual void HandleContentsChanged(ImageListEventPtr event);
		virtual void HandleCurrentIndexChanged(ImageListEventPtr event) ;
		virtual void HandleItemAdded(ImageListEventPtr event);
		virtual void HandleItemRemoved(ImageListEventPtr event);
		virtual void HandleItemChanged(ImageListEventPtr event);
	private:
		Viewer::ViewerImpl *parent;
	};

	class PreferencesEventHandler : public IPreferencesEventHandler
	{
	public:
		PreferencesEventHandler(ViewerImpl* parent) {this->parent = parent;};
		virtual void HandlePreferenceChanged(PreferencesEventPtr event);
	private:
		ViewerImpl* parent;
	};
	
	IPreferencesEventHandlerPtr  m_PreferencesEventHandlerPtr;
	IImageListEventHandlerPtr    m_ImageListEventHandlerPtr;
};

void Viewer::ViewerImpl::SetImageList(ImageList imgList)
{
	m_ImageList.RemoveEventHandler(m_ImageListEventHandlerPtr);
	
	m_ImageList = imgList;
	
	m_ImageList.AddEventHandler(m_ImageListEventHandlerPtr);
	
}

void Viewer::ViewerImpl::SetImageIndex(int index, bool bDirectionForward, bool bBlockCursorChangeSignal /* = true */)
{
	gint width=0, height=0;

	QuiverImageViewMode mode = quiver_image_view_get_view_mode_unmagnified(QUIVER_IMAGE_VIEW(m_pImageView));
	
	if (mode != QUIVER_IMAGE_VIEW_MODE_ACTUAL_SIZE && GTK_WIDGET_REALIZED(m_pImageView))
	{

		width = m_pImageView->allocation.width;
		height = m_pImageView->allocation.height;
	}

	m_ImageList.BlockHandler(m_ImageListEventHandlerPtr);
	
	if (m_ImageList.SetCurrentIndex(index))
	{
		m_pViewer->EmitCursorChangedEvent();

		if (bBlockCursorChangeSignal)
		{
			g_signal_handlers_block_by_func(m_pIconView,(gpointer)viewer_iconview_cursor_changed,this);
		}
		
		QuiverFile f;

		f = m_ImageList.GetCurrent();

		gtk_window_resize (GTK_WINDOW (m_pNavigationWindow),1,1);
		GdkPixbuf *pixbuf = f.GetThumbnail();

		quiver_navigation_control_set_pixbuf(QUIVER_NAVIGATION_CONTROL(m_pNavigationControl),pixbuf);
		
		if (NULL != pixbuf)
		{
			g_object_unref(pixbuf);
		}
		
		m_ImageLoader.LoadImageAtSize(f,width,height);
		
		SetCurrentOrientation(f.GetOrientation(), false);

		quiver_icon_view_set_cursor_cell( QUIVER_ICON_VIEW(m_pIconView),
		      m_ImageList.GetCurrentIndex() );	

		if (bDirectionForward)
		{
			// cache the next image if there is one
			if (m_ImageList.HasNext())
			{
				f = m_ImageList.GetNext();
				m_ImageLoader.CacheImageAtSize(f,width,height);
			}
		}
		else
		{
			// cache the next image if there is one
			if (m_ImageList.HasPrevious())
			{
				f = m_ImageList.GetPrevious();
				m_ImageLoader.CacheImageAtSize(f, width, height);
			}
			
		}
		
		if (bBlockCursorChangeSignal)
		{
			g_signal_handlers_unblock_by_func(m_pIconView,(gpointer)viewer_iconview_cursor_changed,this);
		}
	}
	
	m_ImageList.UnblockHandler(m_ImageListEventHandlerPtr);
}

int  Viewer::ViewerImpl::GetCurrentOrientation()
{
	QuiverFile f = m_ImageList.GetCurrent();
	return m_iCurrentOrientation;
}

void Viewer::ViewerImpl::SetCurrentOrientation(int iOrientation, bool bUpdateExif /*= true*/)
{
	m_iCurrentOrientation = iOrientation;
	
	if (bUpdateExif)
	{
		QuiverFile f = m_ImageList.GetCurrent();
		ExifData* pExifData = f.GetExifData();
		
		if (NULL != pExifData)
		{
			ExifEntry *pExifEntry;
		
			// If the entry doesn't exist, create it. /
			pExifEntry = exif_content_get_entry (pExifData->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
			if (!pExifEntry) 
			{
				pExifEntry = exif_entry_new ();
				exif_content_add_entry (pExifData->ifd[EXIF_IFD_0], pExifEntry);
				exif_entry_initialize (pExifEntry, EXIF_TAG_ORIENTATION);
			}
		
			// Now set the value and save the data. /
			exif_set_short (pExifEntry->data , exif_data_get_byte_order (pExifData), m_iCurrentOrientation);
			
			f.SetExifData(pExifData);
			
			exif_data_unref(pExifData);
		}
	}
}

void Viewer::ViewerImpl::AddFilmstrip()
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	int iFilmstripPos = prefsPtr->GetInteger(QUIVER_PREFS_VIEWER, QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION, FSTRIP_POS_LEFT);

	GtkBox* box = NULL;
	
	switch (iFilmstripPos)
	{
		case FSTRIP_POS_TOP:
		case FSTRIP_POS_BOTTOM:
			box = GTK_BOX(m_pVBox);
			quiver_icon_view_set_n_rows(QUIVER_ICON_VIEW(m_pIconView),1);
			gtk_box_pack_start (box, m_pIconView, FALSE, TRUE, 0);
			
			break;
		case FSTRIP_POS_LEFT:
		case FSTRIP_POS_RIGHT:
			box = GTK_BOX(m_pHBox);
			quiver_icon_view_set_n_columns(QUIVER_ICON_VIEW(m_pIconView),1);
			gtk_box_pack_start (box, m_pIconView, FALSE, TRUE, 0);
			break;		
	}

	if (NULL != box)
	{
		switch (iFilmstripPos)
		{
			case FSTRIP_POS_TOP:
			case FSTRIP_POS_LEFT:
				gtk_box_reorder_child (box, m_pIconView,0);
				break;
			case FSTRIP_POS_BOTTOM:
			case FSTRIP_POS_RIGHT:
				gtk_box_reorder_child (box, m_pIconView,-1);
				break;		
		}
	}

}

static void viewer_action_handler_cb(GtkAction *action, gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	
	QuiverImageView *imageview = QUIVER_IMAGE_VIEW(pViewerImpl->m_pImageView);
	
	//printf("Viewer Action: %s\n",gtk_action_get_name(action));
	
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
	else if (0 == strcmp(szAction,"RotateCW") || 0 == strcmp(szAction,"RotateCW_2"))
	{
		quiver_image_view_rotate(imageview,TRUE);
		pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_ROTATE_CW][pViewerImpl->GetCurrentOrientation()] );
	}
	else if (0 == strcmp(szAction,"RotateCCW") || 0 == strcmp(szAction,"RotateCCW_2"))
	{
		quiver_image_view_rotate(imageview,FALSE);
		pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_ROTATE_CCW][pViewerImpl->GetCurrentOrientation()] );
	}
	else if (0 == strcmp(szAction,"FlipH") || 0 == strcmp(szAction,"FlipH_2"))
	{
		quiver_image_view_flip(imageview,TRUE);
		pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_FLIP_H][pViewerImpl->GetCurrentOrientation()] );
	}
	else if (0 == strcmp(szAction,"FlipV") || 0 == strcmp(szAction,"FlipV_2"))
	{
		quiver_image_view_flip(imageview,FALSE);
		pViewerImpl->SetCurrentOrientation( orientation_matrix[ORIENTATION_FLIP_V][pViewerImpl->GetCurrentOrientation()] );
	}
	else if (0 == strcmp(szAction, "ImageFirst"))
	{
		pViewerImpl->SetImageIndex(0,true,true);
	}
	else if (0 == strcmp(szAction, "ImagePrevious") || 0 == strcmp(szAction, "ImagePrevious_2"))
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageList.GetCurrentIndex()-1,false,true);
	}
	else if (0 == strcmp(szAction, "ImageNext") || 0 == strcmp(szAction, "ImageNext_2") )
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageList.GetCurrentIndex()+1,true,true);
	}
	else if (0 == strcmp(szAction, "ImageLast"))
	{
		pViewerImpl->SetImageIndex(pViewerImpl->m_ImageList.GetSize()-1,false,true);
	}
	else if (0 == strcmp(szAction, "ViewFilmStrip"))
	{
		PreferencesPtr prefsPtr = Preferences::GetInstance();

		if( gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)) )
		{
			gtk_widget_show(pViewerImpl->m_pIconView);			
		}
		else
		{
			gtk_widget_hide(pViewerImpl->m_pIconView);
		}
		prefsPtr->SetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW,gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
	}
	else if (0 == strcmp(szAction, "ViewerTrash"))
	{
		gint rval = GTK_RESPONSE_YES;

		QuiverFile f = pViewerImpl->m_ImageList.GetCurrent();

		if (0) // FIXME: add preference to display trash dialog
		{
	
			GtkWidget* dialog = gtk_message_dialog_new (NULL,GTK_DIALOG_MODAL,
									GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO,("Move the selected image to the trash?"));
	
			char *for_display = gnome_vfs_format_uri_for_display(f.GetURI());
			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog), for_display);
			g_free(for_display);
			
			rval = gtk_dialog_run(GTK_DIALOG(dialog));
	
			gtk_widget_destroy(dialog);
		}

	
		switch (rval)
		{
			case GTK_RESPONSE_YES:
			{
				// delete the items!
				if (QuiverFileOps::MoveToTrash(f))
				{
					pViewerImpl->m_ImageList.Remove(pViewerImpl->m_ImageList.GetCurrentIndex());
					pViewerImpl->SetImageIndex(pViewerImpl->m_ImageList.GetCurrentIndex(),true,true);
				}
				break;
			}
			case GTK_RESPONSE_NO:
				//fall through
			default:
				// do not delete
				cout << "not trashing file : " << endl;//m_QuiverImplPtr->m_ImageList.GetCurrent().GetURI() << endl;
				break;
		}
	}
}

static gboolean viewer_scrollwheel_event(GtkWidget *widget, GdkEventScroll *event, gpointer data )
{

	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

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
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	
	pViewerImpl->m_pViewer->EmitItemActivatedEvent();
	return TRUE;
}

static gboolean viewer_imageview_reload(QuiverImageView *imageview,gpointer data)
{
	//printf("#### got a reload message from the imageview\n");
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	ImageLoader::LoadParams params = {0};

	params.orientation = pViewerImpl->GetCurrentOrientation();
	params.reload = true;
	params.fullsize = true;
	params.no_thumb_preview = true;
	params.state = ImageLoader::LOAD;

	pViewerImpl->m_ImageLoader.LoadImage(pViewerImpl->m_ImageList.GetCurrent(),params);

	return TRUE;
}


static gboolean viewer_iconview_cell_activated(QuiverIconView *iconview,gint cell,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	//pViewerImpl->m_pViewer->EmitItemActivatedEvent();
	return TRUE;
}

static gboolean viewer_iconview_cursor_changed(QuiverIconView *iconview,gint cell,gpointer data)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;
	
	pViewerImpl->SetImageIndex(cell,true,false);

	return TRUE;
}

static gboolean
viewer_navigation_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)userdata;
	
	GdkModifierType state;
	gint x =0, y=0;

	x = (gint)event->x_root;
	y = (gint)event->y_root;
	state = (GdkModifierType)event->state;
	

	gtk_widget_show_all (pViewerImpl->m_pNavigationWindow);

	gint w,h;
	gtk_window_get_size(GTK_WINDOW (pViewerImpl->m_pNavigationWindow),&w,&h);
  	int ww, wh, pos_x,pos_y;
  	ww = w+2;
  	wh = h+2;
  	
  	pos_x = (int)x - ww/2;
  	pos_y = (int)y - wh/2;
  		
	
  	if (gdk_screen_width () < pos_x + ww)
  		pos_x = gdk_screen_width() - ww;
  	else if (0 > pos_x)
  		pos_x = 0;
	
  	if (gdk_screen_height () < pos_y + wh)
  		pos_y = gdk_screen_height() - wh;
  	else if (0 > pos_y)
  		pos_y = 0;
	
	gtk_window_move (GTK_WINDOW (pViewerImpl->m_pNavigationWindow), pos_x, pos_y);

	GdkCursor *cursor;	
	cursor = gdk_cursor_new (GDK_FLEUR); 
		
	gdk_pointer_grab (pViewerImpl->m_pNavigationWindow->window,  TRUE, (GdkEventMask)(GDK_BUTTON_RELEASE_MASK  | GDK_POINTER_MOTION_HINT_MASK    | GDK_BUTTON_MOTION_MASK   | GDK_EXTENSION_EVENTS_ALL),
			  pViewerImpl->m_pNavigationControl->window, 
			  cursor,
			  GDK_CURRENT_TIME);

	gdk_cursor_unref (cursor);
	return TRUE;
}

gboolean navigation_control_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer data )
{
	Viewer::ViewerImpl *pViewerImpl;
	pViewerImpl = (Viewer::ViewerImpl*)data;

	gdk_pointer_ungrab (event->time);

	gtk_widget_hide_all (pViewerImpl->m_pNavigationWindow);
	return TRUE;	
}

Viewer::ViewerImpl::~ViewerImpl()
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
}

Viewer::ViewerImpl::ViewerImpl(Viewer *pViewer) : 
	m_PreferencesEventHandlerPtr ( new PreferencesEventHandler(this) ),
	m_ImageListEventHandlerPtr( new ImageListEventHandler(this) )
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	prefsPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
	
	m_pViewer = pViewer;

	m_pIconView = quiver_icon_view_new();
	m_pImageView = quiver_image_view_new();

	m_iCurrentOrientation = 1;
	
	m_pUIManager = NULL;
	m_iMergedViewerUI = 0;
	
	m_iTimeoutSlideshowID = 0;

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


	g_signal_connect (G_OBJECT (m_pNavigationBox), 
			  "button_press_event",
			  G_CALLBACK (viewer_navigation_button_press_event), 
			  this);

	

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

//	GTK_WIDGET_SET_FLAGS(m_pTable,GTK_CAN_FOCUS);
	m_pHBox = gtk_hbox_new(FALSE,0);
	m_pVBox = gtk_vbox_new(FALSE,0);

	gtk_box_pack_start (GTK_BOX (m_pVBox), m_pTable, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (m_pHBox), m_pVBox, TRUE, TRUE, 0);

	AddFilmstrip();

	string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
	string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);

	if (!prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true))
	{
		if (!strBGColorImg.empty())
		{
			GdkColor color;
			gdk_color_parse(strBGColorImg.c_str(),&color);
			gtk_widget_modify_bg (m_pImageView, GTK_STATE_NORMAL, &color );
		}
		
		if (!strBGColorThumb.empty())
		{
			GdkColor color;
			gdk_color_parse(strBGColorThumb.c_str(),&color);
			gtk_widget_modify_bg (m_pIconView, GTK_STATE_NORMAL, &color );
		}
	}

	m_iSlideShowDuration = prefsPtr->GetInteger(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_DURATION, 2500);

	m_bSlideShowLoop = prefsPtr->GetBoolean(QUIVER_PREFS_SLIDESHOW,QUIVER_PREFS_SLIDESHOW_LOOP);

	quiver_image_view_set_magnification_mode(QUIVER_IMAGE_VIEW(m_pImageView),QUIVER_IMAGE_VIEW_MAGNIFICATION_MODE_SMOOTH);

	quiver_icon_view_set_n_items_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetNItemsFunc)n_cells_callback,this,NULL);
	quiver_icon_view_set_thumbnail_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)thumbnail_pixbuf_callback,this,NULL);
	quiver_icon_view_set_icon_pixbuf_func(QUIVER_ICON_VIEW(m_pIconView),(QuiverIconViewGetThumbnailPixbufFunc)icon_pixbuf_callback,this,NULL);
	quiver_icon_view_set_smooth_scroll(QUIVER_ICON_VIEW(m_pIconView),TRUE);
	int iIconSize = prefsPtr->GetInteger(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE, 128);
	quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(m_pIconView),iIconSize,iIconSize);

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
	IPixbufLoaderObserverPtr tmp( new ViewerImageViewPixbufLoaderObserver(QUIVER_IMAGE_VIEW(m_pImageView)));
	m_PixbufLoaderObserverPtr = tmp;
	m_ImageLoader.AddPixbufLoaderObserver(m_PixbufLoaderObserverPtr.get());
	
	gtk_widget_show_all(m_pHBox);
	gtk_widget_hide(m_pHBox);
	gtk_widget_set_no_show_all(m_pHBox,TRUE);
	
	
	m_pNavigationWindow = gtk_window_new (GTK_WINDOW_POPUP);
	m_pNavigationControl = quiver_navigation_control_new_with_adjustments (m_pAdjustmentH, m_pAdjustmentV);

	g_signal_connect (G_OBJECT (m_pNavigationControl), "button_release_event",  
				G_CALLBACK (navigation_control_button_release_event), this);

 	gtk_widget_add_events (m_pNavigationControl, GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);
	gtk_widget_add_events (m_pNavigationControl, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);

	GtkWidget * frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_ETCHED_IN);	

	
	gtk_container_add (GTK_CONTAINER (frame), m_pNavigationControl);
	gtk_container_add (GTK_CONTAINER (m_pNavigationWindow), frame);
	
	bool bShowFilmstrip = prefsPtr->GetBoolean(QUIVER_PREFS_VIEWER,QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW);
	if (!bShowFilmstrip)
	{
		gtk_widget_hide(m_pIconView);
	}
	
	
}


Viewer::Viewer() : m_ViewerImplPtr(new Viewer::ViewerImpl(this))
{
	

}

Viewer::~Viewer()
{
	SlideShowStop();
	
	gtk_widget_destroy(m_ViewerImplPtr->m_pNavigationWindow);
}

GtkWidget *Viewer::GetWidget()
{
	return m_ViewerImplPtr->m_pHBox;
}

void Viewer::SetImageList(ImageList imgList)
{
	m_ViewerImplPtr->SetImageList(imgList);
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
	
	m_ViewerImplPtr->m_ImageList.UnblockHandler(m_ViewerImplPtr->m_ImageListEventHandlerPtr);

}
void Viewer::Hide()
{
	SlideShowStop();
	
	gtk_widget_hide(m_ViewerImplPtr->m_pHBox);
	if (m_ViewerImplPtr->m_pUIManager)
	{	
		gtk_ui_manager_remove_ui(m_ViewerImplPtr->m_pUIManager,
			m_ViewerImplPtr->m_iMergedViewerUI);
		m_ViewerImplPtr->m_iMergedViewerUI = 0;
	}
	
	m_ViewerImplPtr->m_ImageList.BlockHandler(m_ViewerImplPtr->m_ImageListEventHandlerPtr);
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
										m_ViewerImplPtr.get());
	gtk_ui_manager_insert_action_group (m_ViewerImplPtr->m_pUIManager,actions,0);	
}

GtkTableChild * GetGtkTableChild(GtkTable * table,GtkWidget	*widget_to_get);

int Viewer::GetCurrentOrientation()
{
	return m_ViewerImplPtr->GetCurrentOrientation();	
}


static gboolean timeout_advance_slideshow (gpointer data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)data;
	
	int iNextIndex = pViewerImpl->m_ImageList.GetCurrentIndex()+1;
	
	if (!pViewerImpl->m_ImageList.HasNext() && pViewerImpl->m_bSlideShowLoop)
	{
		iNextIndex = 0;
	}
	
	gdk_threads_enter();
	pViewerImpl->SetImageIndex(iNextIndex,true,true);
	gdk_threads_leave();

	if ( (!pViewerImpl->m_ImageList.HasNext() && !pViewerImpl->m_bSlideShowLoop)
		|| pViewerImpl->m_ImageList.GetSize() < 2)
	{
		pViewerImpl->m_pViewer->SlideShowStop();
		return FALSE;
	}
	
	return TRUE;
}


void Viewer::SlideShowStart()
{
	if (!m_ViewerImplPtr->m_iTimeoutSlideshowID && m_ViewerImplPtr->m_ImageList.GetSize() > 2 )
	{
		m_ViewerImplPtr->m_iTimeoutSlideshowID = g_timeout_add(m_ViewerImplPtr->m_iSlideShowDuration,timeout_advance_slideshow, m_ViewerImplPtr.get());
		EmitSlideShowStartedEvent();
	}
	else
	{
		SlideShowStop();
	}
}


void Viewer::SlideShowStop()
{
	if (0 != m_ViewerImplPtr->m_iTimeoutSlideshowID)
	{
		g_source_remove (m_ViewerImplPtr->m_iTimeoutSlideshowID);
		EmitSlideShowStoppedEvent();
	}
	// reset timout id
	m_ViewerImplPtr->m_iTimeoutSlideshowID = 0;
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
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	return pViewerImpl->m_ImageList.GetSize();
}

static GdkPixbuf* icon_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
	QuiverFile f = pViewerImpl->m_ImageList[cell];

	guint width, height;
	quiver_icon_view_get_icon_size(iconview,&width, &height);
	return f.GetIcon(width,height);
}

static GdkPixbuf* thumbnail_pixbuf_callback(QuiverIconView *iconview, guint cell,gpointer user_data)
{
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;
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
	Viewer::ViewerImpl* pViewerImpl = (Viewer::ViewerImpl*)user_data;

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


//=============================================================================
// private viewer implementation nested classes:
//=============================================================================
void Viewer::ViewerImpl::ImageListEventHandler::HandleContentsChanged(ImageListEventPtr event)
{
	parent->SetImageIndex(parent->m_ImageList.GetCurrentIndex(),true,true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleCurrentIndexChanged(ImageListEventPtr event) 
{
	parent->SetImageIndex(event->GetIndex(),true,true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemAdded(ImageListEventPtr event)
{
	parent->SetImageIndex(parent->m_ImageList.GetCurrentIndex(),true,true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemRemoved(ImageListEventPtr event)
{
	parent->SetImageIndex(parent->m_ImageList.GetCurrentIndex(),true,true);
}
void Viewer::ViewerImpl::ImageListEventHandler::HandleItemChanged(ImageListEventPtr event)
{
	if (parent->m_ImageList.GetCurrentIndex() == event->GetIndex())
	{ 
		ImageLoader::LoadParams params = {0};
	
		params.orientation = parent->GetCurrentOrientation();
		params.reload = true;
		params.fullsize = true;
		params.no_thumb_preview = true;
		params.state = ImageLoader::LOAD;
	
		printf("handle item changed... load image 1\n");
		parent->m_ImageLoader.LoadImage(parent->m_ImageList.GetCurrent(),params);
	}
}


void Viewer::ViewerImpl::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event)
{
	PreferencesPtr prefsPtr = Preferences::GetInstance();
	if (QUIVER_PREFS_APP == event->GetSection() )
	{
		if (QUIVER_PREFS_APP_USE_THEME_COLOR == event->GetKey() )
		{
			if (event->GetNewBoolean())
			{
				// use theme color
				gtk_widget_modify_bg (parent->m_pIconView, GTK_STATE_NORMAL, NULL );
				gtk_widget_modify_bg (parent->m_pImageView, GTK_STATE_NORMAL, NULL );
			}
			else
			{
				GdkColor color;
				
				string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);
						
				gdk_color_parse(strBGColorThumb.c_str(),&color);
				gtk_widget_modify_bg (parent->m_pIconView, GTK_STATE_NORMAL, &color );
				
				gdk_color_parse(strBGColorImg.c_str(),&color);
				gtk_widget_modify_bg (parent->m_pImageView, GTK_STATE_NORMAL, &color);
				
			}
		}
		else if (QUIVER_PREFS_APP_BG_IMAGEVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				GdkColor color;
				
				string strBGColorImg   = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_IMAGEVIEW);
				
				gdk_color_parse(strBGColorImg.c_str(),&color);
				gtk_widget_modify_bg (parent->m_pImageView, GTK_STATE_NORMAL, &color );
			}			
		}
		else if (QUIVER_PREFS_APP_BG_ICONVIEW == event->GetKey() )
		{
			if ( !prefsPtr->GetBoolean(QUIVER_PREFS_APP,QUIVER_PREFS_APP_USE_THEME_COLOR,true) )
			{
				GdkColor color;
				
				string strBGColorThumb = prefsPtr->GetString(QUIVER_PREFS_APP,QUIVER_PREFS_APP_BG_ICONVIEW);						

				gdk_color_parse(strBGColorThumb.c_str(),&color);
				gtk_widget_modify_bg (parent->m_pIconView, GTK_STATE_NORMAL, &color );
			}
		}
	}
	else if ( QUIVER_PREFS_VIEWER == event->GetSection () )
	{
		if (QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW == event->GetKey() )
		{
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION == event->GetKey() )
		{
			
			int iFilmstripPos = event->GetOldInteger();

			GtkContainer* container = NULL;
			
			switch (iFilmstripPos)
			{
				case FSTRIP_POS_TOP:
				case FSTRIP_POS_BOTTOM:
					container = GTK_CONTAINER(parent->m_pVBox);
					break;
				case FSTRIP_POS_LEFT:
				case FSTRIP_POS_RIGHT:
					container = GTK_CONTAINER(parent->m_pHBox);
					break;		
			}

			if (NULL != container)
			{
				g_object_ref(parent->m_pIconView);
				gtk_container_remove(container, parent->m_pIconView);
				parent->AddFilmstrip();
				g_object_unref(parent->m_pIconView);
			}
			
		}
		else if (QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE == event->GetKey() )
		{
			quiver_icon_view_set_icon_size(QUIVER_ICON_VIEW(parent->m_pIconView), event->GetNewInteger(), event->GetNewInteger());
		}
	}
	else if (QUIVER_PREFS_SLIDESHOW == event->GetSection() )
	{
		if (QUIVER_PREFS_SLIDESHOW_DURATION == event->GetKey() )
		{
			parent->m_iSlideShowDuration = event->GetNewInteger(); 
		}
		else if (QUIVER_PREFS_SLIDESHOW_LOOP == event->GetKey() )
		{
			parent->m_bSlideShowLoop = event->GetNewBoolean();
		}
	}
}


