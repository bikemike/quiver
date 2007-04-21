#include <gtk/gtk.h>
#include "ExifView.h"

#include "Preferences.h"
#include "IPreferencesEventHandler.h"

#include <libexif/exif-ifd.h>
#include <libexif/exif-entry.h>
#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>
#include <libexif/exif-tag.h>


#include "QuiverUtils.h"
//#define QUIVER_PREFS_EXIF                  "exif"
//#define QUIVER_PREFS_EXIF_EXPANDED_IFDS    "expanded_ifds"

/* prototypes */

static GtkTreeModel * create_numbers_model (void);

static void exif_orientation_to_text (GtkTreeViewColumn *tree_column,
	                GtkCellRenderer   *cell, 
                    GtkTreeModel      *tree_model,
	                GtkTreeIter       *iter, 
                    gpointer           data);

static void exif_content_foreach_entry_func (ExifEntry *entry, void *user_data);
static void exif_tree_store_add_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store,GtkTreeIter *parent, GtkTreeIter *new_child, ExifEntry *entry);
static void exif_tree_store_update_iter_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store, GtkTreeIter *iter, ExifEntry *entry);

static void exif_value_editing_started_callback (GtkCellRenderer *renderer,
                                            GtkCellEditable *editable,
                                            gchar *path,
                                            gpointer user_data);

static void exif_value_editing_canceled_callback (GtkCellRenderer *renderer,
                                            gpointer user_data);

static gboolean exif_tree_event_popup_menu (GtkWidget *treeview, gpointer userdata);
static gboolean exif_tree_event_button_press (GtkWidget *treeview, GdkEventButton *event, gpointer userdata);

static void exif_tree_show_popup_menu (ExifView::ExifViewImpl *pExifViewImpl, GtkWidget *treeview, guint button, guint32 activate_time);
static void exif_value_cell_edited_callback (GtkCellRendererText *cell,
                                  gchar               *path_string,
                                  gchar               *new_text,
                                  gpointer             user_data);

static void exif_convert_arg_to_entry (const char*set_value, ExifEntry *e, ExifByteOrder o);

static int exif_data_get_orientation(ExifData *pExifData);
static gboolean exif_update_orientation(ExifData *pExifData, int value);
static void exif_update_entry(ExifData *pExifData, ExifIfd ifd,ExifTag tag,const char *value);
static gboolean exif_date_format_is_valid(const char *date);

static void exif_tree_event_remove_tag(GtkMenuItem *menuitem, gpointer user_data);
static void exif_tree_event_add_tag(GtkMenuItem *menuitem, gpointer user_data);
static void exif_tree_update_thumbnail(ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store);

static void exif_tree_update_iter_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeIter *iter, ExifEntry *entry);
static void exif_tree_update_entry (ExifView::ExifViewImpl *pExifViewImpl, ExifIfd ifd, ExifTag tag);	

static void exif_view_map(GtkWidget *widget, gpointer user_data);
static gboolean exif_view_idle_load_exif_tree_view(gpointer data);

/* private implementation */
class ExifView::ExifViewImpl
{
public:
//	methods
	ExifViewImpl();
	~ExifViewImpl();


// variables
	QuiverFile    m_QuiverFile;
	ExifData*     m_pExifData;
	GtkWidget*    m_pTreeView;
	GtkWidget*    m_pScrolledWindow;
	GtkUIManager* m_pUIManager;
	guint         m_iIdleLoadID;
	
	gboolean      m_bLoaded;

// nested classes
	class PreferencesEventHandler : public IPreferencesEventHandler
	{
	public:
		PreferencesEventHandler(ExifViewImpl* parent) {this->parent = parent;};
		virtual void HandlePreferenceChanged(PreferencesEventPtr event);
	private:
		ExifViewImpl* parent;
	};
	
	IPreferencesEventHandlerPtr  m_PreferencesEventHandlerPtr;
};

enum
{
	EXIF_TREE_COLUMN_TAG_ID,
	EXIF_TREE_COLUMN_NAME,
	EXIF_TREE_COLUMN_VALUE_TEXT,
	EXIF_TREE_COLUMN_VALUE_ORIENTATION,
	EXIF_TREE_COLUMN_VALUE_PIXBUF,
	EXIF_TREE_COLUMN_IS_VISIBLE_TEXT,
	EXIF_TREE_COLUMN_IS_VISIBLE_PIXBUF,
	EXIF_TREE_COLUMN_IS_VISIBLE_ORIENTATION,
	EXIF_TREE_COLUMN_IS_GROUP,
	EXIF_TREE_COLUMN_IS_EDITABLE,
	EXIF_TREE_COLUMN_COUNT,

};


enum
{
  ORIENTATION_COLUMN_TEXT_VALUE,
  ORIENTATION_COLUMN_COUNT
};


const char *orientation_options[] = {
        "top - left",
        "top - right",
        "bottom - right",
        "bottom - left",
        "left - top",
        "right - top",
        "right - bottom",
        "left - bottom",
};

typedef struct _ForEachEntryData
{
	ExifView::ExifViewImpl *pExifViewImpl;
	GtkTreeStore *store;
	GtkTreeIter *parent;
} ForEachEntryData;


typedef struct _ExifTagAddRemoveStruct
{
	ExifView::ExifViewImpl *pExifViewImpl;
	ExifIfd ifd;
	ExifTag tag;
	
} ExifTagAddRemoveStruct;




const int ifd_editable_tags[][10] = {
	/* ifd0 */
	{
		EXIF_TAG_ARTIST,
		//EXIF_TAG_COPYRIGHT, - a little difficult to implement right now
		EXIF_TAG_DATE_TIME,
		EXIF_TAG_IMAGE_DESCRIPTION,
		EXIF_TAG_IMAGE_WIDTH,
		EXIF_TAG_IMAGE_LENGTH,
		EXIF_TAG_ORIENTATION,
		EXIF_TAG_SOFTWARE,
		0
	},

	/* ifd1 */
	{ 0 },
	
	/* ifd exif */
	{
		EXIF_TAG_DATE_TIME_ORIGINAL,
		EXIF_TAG_DATE_TIME_DIGITIZED,
		EXIF_TAG_PIXEL_X_DIMENSION,
		EXIF_TAG_PIXEL_Y_DIMENSION,
		EXIF_TAG_USER_COMMENT,
		0
	},
	
	/* ifd gps */
	{ 0 },
	
	/* ifd interop */
	{
		EXIF_TAG_RELATED_IMAGE_WIDTH,
		EXIF_TAG_RELATED_IMAGE_LENGTH,
		0
	},
};


ExifView::ExifViewImpl::ExifViewImpl() :
	m_PreferencesEventHandlerPtr ( new PreferencesEventHandler(this) )
{
	PreferencesPtr prefPtr = Preferences::GetInstance();
	prefPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
}


ExifView::ExifViewImpl::~ExifViewImpl()
{
	PreferencesPtr prefPtr = Preferences::GetInstance();
	prefPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );
	
	if (NULL != m_pExifData)
	{
		exif_data_unref(m_pExifData);
	}
	
	if (NULL != m_pUIManager)
	{
		g_object_unref(m_pUIManager);
		m_pUIManager =  NULL;
	}
}

static void exif_view_map(GtkWidget *widget, gpointer user_data)
{
	ExifView::ExifViewImpl *pExifViewImpl = static_cast<ExifView::ExifViewImpl*>(user_data);
	if (!pExifViewImpl->m_bLoaded)
	{
		if (0 != pExifViewImpl->m_iIdleLoadID)
		{
			g_source_remove(pExifViewImpl->m_iIdleLoadID );
			pExifViewImpl->m_iIdleLoadID = 0;
		}
		
		pExifViewImpl->m_iIdleLoadID = g_timeout_add(10,exif_view_idle_load_exif_tree_view,pExifViewImpl);
		pExifViewImpl->m_bLoaded = TRUE;
	}
}

ExifView::ExifView() : m_ExifViewImplPtr (new ExifViewImpl() )
{
	m_ExifViewImplPtr->m_iIdleLoadID = 0;
	m_ExifViewImplPtr->m_bLoaded     = FALSE;
	m_ExifViewImplPtr->m_pTreeView   = NULL;
	m_ExifViewImplPtr->m_pExifData   = NULL;
	m_ExifViewImplPtr->m_pUIManager  = NULL;

	GtkWidget *treeview;

	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	//GtkTreeSelection *selection;

	/* Create a view */
	//treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	treeview = gtk_tree_view_new();//_with_model (GTK_TREE_MODEL (store));

	//g_object_set(G_OBJECT(treeview),"show-expanders",FALSE,NULL);
	
	
	GdkColor highlight_color1;
	GdkColor highlight_color2;
	GdkColor editable_color;
	gdk_color_parse("#00f",&editable_color);
	gdk_color_parse("#fa1",&highlight_color1);
	gdk_color_parse("#f90",&highlight_color2);
	/* = treeview->style->bg[GTK_STATE_SELECTED];*/

/*
	highlight_color.red =  (treeview->style->bg[GTK_STATE_SELECTED].red + treeview->style->bg[GTK_STATE_NORMAL].red) /2;
	highlight_color.green =  (treeview->style->bg[GTK_STATE_SELECTED].green + treeview->style->bg[GTK_STATE_NORMAL].green)/2;
	highlight_color.blue =  (treeview->style->bg[GTK_STATE_SELECTED].blue + treeview->style->bg[GTK_STATE_NORMAL].blue)/2;
*/
	
	//gtk_tree_view_set_expander_column(GTK_TREE_VIEW (treeview),column);
	

	// first column of exif treeview
	// Name
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (G_OBJECT (renderer),  "mode", GTK_CELL_RENDERER_MODE_INERT,  NULL);
	g_object_set (G_OBJECT (renderer),  "cell-background-gdk", &highlight_color2,  NULL);
	g_object_set (G_OBJECT (renderer),  "background-gdk", &highlight_color1,  NULL);
	g_object_set (G_OBJECT (renderer),  "foreground-gdk", &editable_color,  NULL);
	g_object_set (G_OBJECT (renderer),  "weight", PANGO_WEIGHT_BOLD,  NULL);
	//g_object_set (G_OBJECT (renderer),  "scale", 1.1,  NULL);
	g_object_set (G_OBJECT (renderer),  "yalign", 0.0,  NULL);

	/*
	g_object_set (G_OBJECT (renderer),  "wrap-width",85,  NULL);
	g_object_set (G_OBJECT (renderer),  "wrap-mode",PANGO_WRAP_WORD_CHAR,  NULL);
	*/
	column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
	  "text", EXIF_TREE_COLUMN_NAME,
	  "cell-background-set",EXIF_TREE_COLUMN_IS_GROUP,
	  "background-set",EXIF_TREE_COLUMN_IS_GROUP,
	  "foreground-set",EXIF_TREE_COLUMN_IS_EDITABLE,
	  //"sensitive",EXIF_TREE_COLUMN_IS_EDITABLE,
	  "weight-set",EXIF_TREE_COLUMN_IS_GROUP,
	//  "scale-set",EXIF_TREE_COLUMN_IS_GROUP,
	NULL);
	gtk_tree_view_column_set_resizable (column,TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	// second column of exif treeview
	// Value
	// we're going to try adding multiple cell renderers to this column so we can
	// put in a combo box if needed, or a checkbox, or a spin-control
	/*
	 *
  GtkCellRenderer *renderer_text;
  GtkCellRenderer *renderer_image;
  GtkTreeViewColumn *column;

  column = gtk_tree_view_column_new();

  * COL_IMAGE 
  renderer_image = gtk_cell_renderer_pixbuf_new();
  gtk_tree_view_column_pack_start(column,renderer_image,FALSE);
  gtk_tree_view_column_add_attribute(column,
				     renderer_image,
				     "pixbuf",COL_IMAGE);  
  
  * COL_NAME 
  renderer_text = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_end(column,renderer_text,TRUE);
  gtk_tree_view_column_add_attribute(column,
				     renderer_text,
				     "text",COL_NAME);

  * insert column 
  gtk_tree_view_insert_column(GTK_TREE_VIEW(treeview),column,-1);



  */

	renderer = gtk_cell_renderer_text_new ();
	g_signal_connect(renderer, "edited", (GCallback) exif_value_cell_edited_callback, m_ExifViewImplPtr.get());
	g_signal_connect(renderer, "editing-started", (GCallback) exif_value_editing_started_callback, m_ExifViewImplPtr.get());
	g_signal_connect(renderer, "editing-canceled", (GCallback) exif_value_editing_canceled_callback, m_ExifViewImplPtr.get());

	g_object_set (G_OBJECT (renderer),  "mode",GTK_CELL_RENDERER_MODE_EDITABLE,  NULL);
	g_object_set (G_OBJECT (renderer),  "cell-background-gdk", &highlight_color2,  NULL);
	g_object_set (G_OBJECT (renderer),  "background-gdk", &highlight_color1,  NULL);


#if GTK_MAJOR_VERSION > 2 || GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION >= 8
	g_object_set (G_OBJECT (renderer),  "wrap-width",200,  NULL);
	g_object_set (G_OBJECT (renderer),  "wrap-mode",PANGO_WRAP_WORD_CHAR,  NULL);
#endif

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column,"Value");

	gtk_tree_view_column_pack_start (column,renderer,TRUE);
	gtk_tree_view_column_set_attributes(column,renderer,
	  "text", EXIF_TREE_COLUMN_VALUE_TEXT,
	  "cell-background-set",EXIF_TREE_COLUMN_IS_GROUP,
	  "background-set",EXIF_TREE_COLUMN_IS_GROUP,
	  "editable",EXIF_TREE_COLUMN_IS_EDITABLE,
	  "visible",EXIF_TREE_COLUMN_IS_VISIBLE_TEXT,
	NULL);

	renderer = gtk_cell_renderer_combo_new();

	gtk_tree_view_column_pack_end (column,renderer,FALSE);

	g_object_set (renderer,
                "model", create_numbers_model(),
                "text-column", ORIENTATION_COLUMN_TEXT_VALUE,
                "has-entry", FALSE,
	  			"editable",TRUE,
                NULL);

	gtk_tree_view_column_add_attribute(column,renderer,"text",EXIF_TREE_COLUMN_VALUE_ORIENTATION);
	gtk_tree_view_column_add_attribute(column,renderer,"visible",EXIF_TREE_COLUMN_IS_VISIBLE_ORIENTATION);
	gtk_tree_view_column_set_cell_data_func(column,renderer,exif_orientation_to_text,NULL,NULL);

	g_signal_connect(renderer, "edited", (GCallback) exif_value_cell_edited_callback, m_ExifViewImplPtr.get());
	//g_signal_connect(renderer, "edited", (GCallback) exif_value_cell_edited_callback, NULL);
	g_signal_connect(renderer, "editing-started", (GCallback) exif_value_editing_started_callback, m_ExifViewImplPtr.get());
	g_signal_connect(renderer, "editing-canceled", (GCallback) exif_value_editing_canceled_callback, m_ExifViewImplPtr.get());



	renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_end (column,renderer,FALSE);

	gtk_tree_view_column_add_attribute(column,renderer,"pixbuf",EXIF_TREE_COLUMN_VALUE_PIXBUF);
	gtk_tree_view_column_add_attribute(column,renderer,"visible",EXIF_TREE_COLUMN_IS_VISIBLE_PIXBUF);

	//gtk_tree_view_column_set_spacing(column,0);
	//gtk_tree_view_column_set_sizing (column,GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (column,TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);


	g_signal_connect(treeview, "button-press-event", (GCallback) exif_tree_event_button_press, m_ExifViewImplPtr.get());
	g_signal_connect(treeview, "popup-menu", (GCallback) exif_tree_event_popup_menu, m_ExifViewImplPtr.get());

	//g_signal_connect(treeview, "size-allocate", (GCallback) exif_tree_size_allocate, NULL);

	gtk_tree_view_expand_all (GTK_TREE_VIEW(treeview));

	GtkWidget * scrolled_window = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrolled_window),treeview);
	gtk_widget_show(scrolled_window);
	gtk_widget_show(treeview);

	m_ExifViewImplPtr->m_pTreeView = treeview;
	m_ExifViewImplPtr->m_pScrolledWindow = scrolled_window;	
	
	g_signal_connect(scrolled_window, "map", (GCallback) exif_view_map, m_ExifViewImplPtr.get());
}

ExifView::~ExifView()
{
}

GtkWidget *
ExifView::GetWidget()
{
	return m_ExifViewImplPtr->m_pScrolledWindow;
}

static gboolean exif_view_idle_load_exif_tree_view(gpointer data)
{
	ExifView::ExifViewImpl *pExifViewImpl = static_cast<ExifView::ExifViewImpl*>(data);
	
	
	GtkTreeStore *store;

	GtkTreeIter iter1 = {0};  /* Parent iter */
	GtkTreeIter iter2 = {0};  /* Child iter  */

	/* Create a model.  We are using the store model for now, though we
	* could use any other GtkTreeModel */
	store = gtk_tree_store_new (EXIF_TREE_COLUMN_COUNT,
	 G_TYPE_INT,
	 G_TYPE_STRING,
	 G_TYPE_STRING,
	 G_TYPE_INT,
	 GDK_TYPE_PIXBUF,
	 G_TYPE_BOOLEAN,
	 G_TYPE_BOOLEAN,
	 G_TYPE_BOOLEAN,
	 G_TYPE_BOOLEAN,
	 G_TYPE_BOOLEAN
	 );

	int i;
	
	//loader = exif_loader_new();
	
	//exif_loader_write_file(loader,"ginger.jpg");
	//exifData = exif_data_new_from_file("IMGP0217_modified.JPG"); //exif_loader_get_data(loader);

	if (NULL != pExifViewImpl->m_pExifData)
	{
		exif_data_unref(pExifViewImpl->m_pExifData);
	}
	
	pExifViewImpl->m_pExifData = pExifViewImpl->m_QuiverFile.GetExifData();
	
	if (NULL != pExifViewImpl->m_pExifData)
	{
		exif_data_ref(pExifViewImpl->m_pExifData);
	}

	ExifData *pExifData = pExifViewImpl->m_pExifData;
	
	if (NULL != pExifData)
	{
		exif_data_ref(pExifData);
		
		GdkPixbuf *pixbuf = pExifViewImpl->m_QuiverFile.GetExifThumbnail();
		if (NULL != pixbuf)
		{
			GdkPixbuf *new_pixbuf = QuiverUtils::GdkPixbufExifReorientate(pixbuf,pExifViewImpl->m_QuiverFile.GetOrientation());
			if (NULL != new_pixbuf)
			{
				g_object_unref(pixbuf);
				pixbuf = new_pixbuf;
			}
		}
		
		if (NULL != pixbuf)
		{
			gtk_tree_store_append (store, &iter1, NULL);  /* Acquire a top-level iterator */
			gtk_tree_store_set (store, &iter1,
					EXIF_TREE_COLUMN_NAME, "Exif Thumbnail",
					EXIF_TREE_COLUMN_IS_GROUP, TRUE,
					EXIF_TREE_COLUMN_IS_VISIBLE_TEXT, TRUE,
					EXIF_TREE_COLUMN_TAG_ID,-1,
					-1);

					gtk_tree_store_append (store, &iter2, &iter1);
					gtk_tree_store_set (store, &iter2,
						EXIF_TREE_COLUMN_NAME, "Thumbnail",
						EXIF_TREE_COLUMN_VALUE_PIXBUF,pixbuf,
						EXIF_TREE_COLUMN_IS_VISIBLE_PIXBUF, TRUE,
						-1);
			g_object_unref(pixbuf);

		}

		// go through exif fields to see if any are set (if not, no exif)
		for (i = 0; i < EXIF_IFD_COUNT; i++)
		{
			if (pExifData->ifd[i]->count)
			 {
				const char *exif_ifd = exif_ifd_get_name((ExifIfd)i);
				gtk_tree_store_append (store, &iter1, NULL);  /* Acquire a top-level iterator */
				gtk_tree_store_set (store, &iter1,
					EXIF_TREE_COLUMN_NAME, exif_ifd,
					EXIF_TREE_COLUMN_IS_GROUP, TRUE,
					EXIF_TREE_COLUMN_TAG_ID,i,
					EXIF_TREE_COLUMN_IS_VISIBLE_TEXT, TRUE,
					-1);

				ForEachEntryData entry_data = {0};
				entry_data.pExifViewImpl = pExifViewImpl;
				entry_data.store = store;
				entry_data.parent = &iter1;
				exif_content_foreach_entry (pExifData->ifd[i],
							 exif_content_foreach_entry_func,&entry_data);
			}
		}
		// maker notes
		ExifMnoteData * mnote_data = exif_data_get_mnote_data (pExifData);

		int mnote_count = exif_mnote_data_count (mnote_data);

		//ExifMnoteData *n;

		if (!mnote_data) {
			//printf ("Unknown MakerNote format.\n");
		}
		else
		{

			mnote_count = exif_mnote_data_count (mnote_data);

			if (0 < mnote_count)
			{
				gtk_tree_store_append (store, &iter1, NULL);  /* Acquire a top-level iterator */
				gtk_tree_store_set (store, &iter1,
						EXIF_TREE_COLUMN_NAME, "Maker Notes",
						EXIF_TREE_COLUMN_IS_GROUP, TRUE,
						EXIF_TREE_COLUMN_IS_VISIBLE_TEXT, TRUE,
						EXIF_TREE_COLUMN_TAG_ID,-1,
						-1);

				for (i = 0; i < mnote_count; i++)
				{
					char val[1024];
					const char * name = exif_mnote_data_get_name (mnote_data,i);
					//const char * title = exif_mnote_data_get_title (mnote_data,i);
					//const char * description = exif_mnote_data_get_description (mnote_data,i);
					const char * value = exif_mnote_data_get_value (mnote_data,i,val,1024);
					if (name && value)
					{
						//printf("title: %s\n",title);
						//printf("Descr: %s\n",description);
						//printf("Entry: %s - %s\n",name,value);

						gtk_tree_store_append (store, &iter2, &iter1);
						gtk_tree_store_set (store, &iter2,
							EXIF_TREE_COLUMN_NAME, name,
							EXIF_TREE_COLUMN_VALUE_TEXT,value,
							EXIF_TREE_COLUMN_IS_VISIBLE_TEXT, TRUE,
							-1);
					}
				}

			}
		}
		exif_data_unref(pExifData);
	}

	gdk_threads_enter();

	/*
	GtkAdjustment *h = gtk_tree_view_get_hadjustment(GTK_TREE_VIEW(pExifViewImpl->m_pTreeView));
	GtkAdjustment *v = gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(pExifViewImpl->m_pTreeView));

	gdouble hval = gtk_adjustment_get_value(h);
	gdouble vval = gtk_adjustment_get_value(v);
	*/

	gtk_tree_view_set_model(GTK_TREE_VIEW(pExifViewImpl->m_pTreeView),GTK_TREE_MODEL (store));

	gtk_tree_view_expand_all (GTK_TREE_VIEW(pExifViewImpl->m_pTreeView));

	/*
	printf("setting : %f %f \n",hval,vval);
	gtk_adjustment_set_value(h,hval);
	gtk_adjustment_set_value(v,vval);
	*/

	pExifViewImpl->m_iIdleLoadID = 0;
	g_object_unref (G_OBJECT (store));

	gdk_threads_leave();



	return FALSE;
}

void 
ExifView::SetQuiverFile(QuiverFile quiverFile)
{
	m_ExifViewImplPtr->m_QuiverFile = quiverFile;
	if (NULL != m_ExifViewImplPtr->m_pExifData)
	{
		exif_data_unref(m_ExifViewImplPtr->m_pExifData);
		m_ExifViewImplPtr->m_pExifData = NULL;
		
	}

	if (GTK_WIDGET_MAPPED(m_ExifViewImplPtr->m_pScrolledWindow))
	{
		if (0 != m_ExifViewImplPtr->m_iIdleLoadID)
		{
			g_source_remove(m_ExifViewImplPtr->m_iIdleLoadID );
			m_ExifViewImplPtr->m_iIdleLoadID = 0;
		}
		
		m_ExifViewImplPtr->m_iIdleLoadID = g_timeout_add(300,exif_view_idle_load_exif_tree_view,m_ExifViewImplPtr.get());
		m_ExifViewImplPtr->m_bLoaded = TRUE;
	}
	else
	{
		m_ExifViewImplPtr->m_bLoaded = FALSE;
	}

}

void 
ExifView::SetUIManager(GtkUIManager *ui_manager)
{
	GError *tmp_error;
	tmp_error = NULL;
	
	if (NULL != m_ExifViewImplPtr->m_pUIManager)
	{
		g_object_unref(m_ExifViewImplPtr->m_pUIManager);
	}

	m_ExifViewImplPtr->m_pUIManager = ui_manager;
	
	g_object_ref(m_ExifViewImplPtr->m_pUIManager);

/*
	guint n_entries = G_N_ELEMENTS (action_entries);
	
	GtkActionGroup* actions = gtk_action_group_new ("BrowserActions");
	
	gtk_action_group_add_actions(actions, action_entries, n_entries, m_ViewerImplPtr.get());
                                 
	gtk_action_group_add_toggle_actions(actions,
										action_entries_toggle, 
										G_N_ELEMENTS (action_entries_toggle),
										m_ViewerImplPtr.get());
	gtk_ui_manager_insert_action_group (m_ViewerImplPtr->m_pUIManager,actions,0);
*/	
}

/* misc functions */

static GtkTreeModel *
create_numbers_model (void)
{
	gint i = 0;
	GtkListStore *model;
	GtkTreeIter iter;

	/* create list store */
	model = gtk_list_store_new (ORIENTATION_COLUMN_COUNT, G_TYPE_STRING);

	/* add numbers */
	for (i = 0; i < 8; i++)
	{
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
                          ORIENTATION_COLUMN_TEXT_VALUE, orientation_options[i],
                          -1);
	}

	return GTK_TREE_MODEL (model);
}




static void 
exif_orientation_to_text (GtkTreeViewColumn *tree_column,
	                GtkCellRenderer   *cell, 
                    GtkTreeModel      *tree_model,
	                GtkTreeIter       *iter, 
                    gpointer           data)
{
	//GtkCellRendererText *cell_text = (GtkCellRendererText *)cell;
	gint value;

	/* Get the double value from the model. */
	gtk_tree_model_get (tree_model,iter,EXIF_TREE_COLUMN_VALUE_ORIENTATION,&value,-1);
	/* Now we can format the value ourselves. */
	if (value <= 8 && 0 < value)
		g_object_set (cell, "text", orientation_options[value-1], NULL);
}

static void exif_content_foreach_entry_func (ExifEntry *entry, void *user_data)
{
	ForEachEntryData *entry_data = (ForEachEntryData*)user_data;
	
	GtkTreeIter new_child = {0};  /* Child iter  */
	exif_tree_store_add_entry(entry_data->pExifViewImpl,entry_data->store,entry_data->parent,&new_child,entry);
}

static void exif_tree_store_add_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store,GtkTreeIter *parent, GtkTreeIter *new_child, ExifEntry *entry)
{
	gtk_tree_store_append (store, new_child, parent);
	exif_tree_store_update_iter_entry(pExifViewImpl,store,new_child,entry);
}


static void exif_tree_store_update_iter_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store, GtkTreeIter *iter, ExifEntry *entry)
{
	gboolean editable = FALSE;

	int i,j;
	const char *name;
	const char *title;
	const char *description;
	const char *value;
	const guint size_max = 2048;
	char val[size_max];
	value = exif_entry_get_value (entry, val,size_max);
	
	name = exif_tag_get_name (entry->tag);
	title = exif_tag_get_title(entry->tag);
	description = exif_tag_get_description (entry->tag);

	
	for (i = 0;i< EXIF_IFD_COUNT && !editable;i++)
	{
		for (j = 0; 0 != ifd_editable_tags[i][j] ;j++)
		{
			if (entry->tag == ifd_editable_tags[i][j])
			{
				editable = TRUE;
				break;
			}
		}
	}
	if (EXIF_TAG_USER_COMMENT == entry->tag)
	{
		const guint character_code_size = 8;
		if (character_code_size <= entry->size && entry->size - character_code_size < size_max )
		{
			char character_code[8];
			
			memcpy(character_code,entry->data,character_code_size);

			unsigned char *start = entry->data+character_code_size;
			memcpy(val,start,entry->size - character_code_size);
			val[entry->size - character_code_size] = '\0';

			//printf("Size of the user comment filed is %d\n",entry->size);
		}
		else
		{
			val[0] = '\0';
		}
	}
	
	if (EXIF_TAG_ORIENTATION == entry->tag)
	{
		ExifByteOrder o;
		o = exif_data_get_byte_order (entry->parent->parent);
		int orientation = exif_get_short (entry->data, o);

		gtk_tree_store_set (store, iter,
			EXIF_TREE_COLUMN_TAG_ID,entry->tag,
            EXIF_TREE_COLUMN_NAME, title,
            EXIF_TREE_COLUMN_VALUE_ORIENTATION,orientation,
            EXIF_TREE_COLUMN_IS_VISIBLE_ORIENTATION, TRUE,
            EXIF_TREE_COLUMN_IS_EDITABLE, editable,
            -1);
	}
	else
	{
		gtk_tree_store_set (store, iter,
			EXIF_TREE_COLUMN_TAG_ID,entry->tag,
            EXIF_TREE_COLUMN_NAME, title,
            EXIF_TREE_COLUMN_VALUE_TEXT,value,
            EXIF_TREE_COLUMN_IS_VISIBLE_TEXT, TRUE,
            EXIF_TREE_COLUMN_IS_EDITABLE, editable,
            -1);
	}


}



static gboolean entry_focus_out ( GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{
	//GtkCellRenderer* renderer = (GtkCellRenderer*)user_data;
	
	//printf("losing focus\n");
	
	//gtk_cell_renderer_stop_editing(renderer,TRUE);
	gtk_cell_editable_remove_widget(GTK_CELL_EDITABLE(widget));
	
	//gtk_widget_hide(widget);
	
	return FALSE; // false to propagate
}

static void exif_value_editing_started_callback (GtkCellRenderer *renderer,
                                            GtkCellEditable *editable,
                                            gchar *path,
                                            gpointer user_data)
{
	ExifView::ExifViewImpl* pExifViewImpl = (ExifView::ExifViewImpl*)user_data;
	
	//printf("Editing started\n");
	
	if (NULL != pExifViewImpl->m_pUIManager)
	{
		QuiverUtils::DisconnectUnmodifiedAccelerators(pExifViewImpl->m_pUIManager);
	}
	
	g_signal_connect (G_OBJECT (editable), "focus-out-event",
    			G_CALLBACK (entry_focus_out), renderer);
	
	if (GTK_IS_COMBO_BOX(editable))
	{

		GtkCellLayout *layout = GTK_CELL_LAYOUT(editable);
		gtk_cell_layout_clear(layout);

		GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

		gtk_cell_layout_pack_start (layout,renderer,TRUE);
		gtk_cell_layout_set_attributes(layout,renderer,
		  "text", ORIENTATION_COLUMN_TEXT_VALUE,
		NULL);
	}

}

static void exif_value_editing_canceled_callback (GtkCellRenderer *renderer,
                                            gpointer user_data)
{
	ExifView::ExifViewImpl* pExifViewImpl = (ExifView::ExifViewImpl*)user_data;
	
	//printf("Editing canceled\n");
	
	if (NULL != pExifViewImpl->m_pUIManager)
	{
		QuiverUtils::ConnectUnmodifiedAccelerators(pExifViewImpl->m_pUIManager);
	}
}

static gboolean exif_tree_event_popup_menu (GtkWidget *treeview, gpointer userdata)
{
	ExifView::ExifViewImpl *pExifViewImpl = (ExifView::ExifViewImpl*)userdata;
	exif_tree_show_popup_menu(pExifViewImpl,treeview, 0, gtk_get_current_event_time());
	return TRUE; /* we handled this */
}

static gboolean
exif_tree_event_button_press (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
	ExifView::ExifViewImpl *pExifViewImpl = (ExifView::ExifViewImpl*)userdata;
	
	/* single click with the right mouse button? */
	if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3)
	{
		g_print ("Single right click on the tree view.\n");
	
		/* optional: select row if no row is selected or only
		 *  one other row is selected (will only do something
		 *  if you set a tree selection mode as described later
		 *  in the tutorial) */
		GtkTreeSelection *selection;
		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
		
		if (NULL != selection)
		{
			/* Note: gtk_tree_selection_count_selected_rows() does not
			 *   exist in gtk+-2.0, only in gtk+ >= v2.2 ! */
			if (gtk_tree_selection_count_selected_rows(selection)  <= 1)
			{
				GtkTreePath *path;
				/* Get tree path for row that was clicked */
				GtkTreeViewColumn *column;
				if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
										 (gint) event->x, 
										 (gint) event->y,
										 &path, &column, NULL, NULL))
				{
					gtk_tree_view_set_cursor(GTK_TREE_VIEW(treeview),path,NULL,FALSE);
					gtk_tree_selection_unselect_all(selection);
					gtk_tree_selection_select_path(selection, path);
					gtk_tree_path_free(path);
				}
			}
		}			
		exif_tree_show_popup_menu(pExifViewImpl,treeview, event->button, gdk_event_get_time((GdkEvent*)event));
		return TRUE;
	}
	return FALSE;
}

static void exif_tree_show_popup_menu (ExifView::ExifViewImpl *pExifViewImpl, GtkWidget *treeview, guint button, guint32 activate_time)
{
	int j;
	GtkWidget *menu, *submenu=NULL, *menuitem;
	
	ExifData* pExifData = pExifViewImpl->m_pExifData;
	
	

	/* get the group that has been clicked on or is selected */
	
	GtkTreePath *path;
	GtkTreeViewColumn *column;
	GtkTreeIter iter,parent;
	ExifIfd content_id;
	ExifTag tag_id; 
	gboolean has_parent = FALSE;
	gboolean show_menu = FALSE;

	gtk_tree_view_get_cursor ( GTK_TREE_VIEW(treeview),&path,&column );
	GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW(treeview));

	if (NULL != path)
	{
		menu = gtk_menu_new();
		gtk_tree_model_get_iter(GTK_TREE_MODEL(model),&iter,path);
		gtk_tree_model_get (GTK_TREE_MODEL(model),&iter,EXIF_TREE_COLUMN_TAG_ID,&tag_id,-1);
		while ( gtk_tree_model_iter_parent(GTK_TREE_MODEL(model),&parent,&iter ) )
		{
			has_parent = TRUE;
			iter = parent;
		}
	
		gtk_tree_model_get (GTK_TREE_MODEL(model),&iter,EXIF_TREE_COLUMN_TAG_ID,&content_id,-1);
	
		GtkWidget *image;
	
		if (0 <= content_id && content_id < EXIF_IFD_COUNT)
		{
			/*
			menuitem = gtk_menu_item_new_with_label( exif_ifd_get_name(i) );
			gtk_menu_shell_append(GTK_MENU_SHELL(submenu), menuitem);
			*/
			if (0 != ifd_editable_tags[content_id][0])
			{
				for (j=0 ; 0 != ifd_editable_tags[content_id][j];j++)
				{
					/* only add the tag if it doesnt already exist in the exif data */
					ExifEntry *e = exif_content_get_entry (pExifData->ifd[content_id],(ExifTag)ifd_editable_tags[content_id][j] );
					if (!e) 
					{
						if (!show_menu)
						{
							show_menu = TRUE;
							image = gtk_image_new_from_stock (GTK_STOCK_ADD,GTK_ICON_SIZE_MENU);
	
							menuitem = gtk_image_menu_item_new_with_label("Add Tag");
							gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menuitem),image);
	
							gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
							submenu = gtk_menu_new();
							gtk_menu_item_set_submenu       (GTK_MENU_ITEM(menuitem),submenu);
						}
						menuitem = gtk_menu_item_new_with_label( exif_tag_get_title((ExifTag)ifd_editable_tags[content_id][j]) );
						gtk_menu_shell_append(GTK_MENU_SHELL(submenu), menuitem);
	
						ExifTagAddRemoveStruct *data = g_new(ExifTagAddRemoveStruct,1);
						data->pExifViewImpl = pExifViewImpl;
						data->ifd = content_id;
						data->tag = (ExifTag)ifd_editable_tags[content_id][j];
						g_signal_connect(menuitem, "activate",
							 (GCallback) exif_tree_event_add_tag, data);//ifd_editable_tags[content_id][j]);
					}
				}
			}
	
	
			if (has_parent)
			{
				show_menu = TRUE;
				image = gtk_image_new_from_stock (GTK_STOCK_REMOVE,GTK_ICON_SIZE_MENU);
				menuitem = gtk_image_menu_item_new_with_label("Remove Tag");
	
				gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menuitem),image);
	
				ExifTagAddRemoveStruct *data = g_new(ExifTagAddRemoveStruct,1);
				data->pExifViewImpl = pExifViewImpl;
				data->tag = tag_id;
				data->ifd = content_id;
				g_signal_connect(menuitem, "activate",
								 (GCallback) exif_tree_event_remove_tag, data);//ifd_editable_tags[content_id][j]);
				/*
				g_signal_connect(menuitem, "activate",
								 (GCallback) signal_uncheck_selected, treeview);
								 */
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
			}
		
		}
		if (show_menu)
		{
	
			gtk_widget_show_all(menu);
			
			gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
							  button, activate_time);
		}
	}
}

static void exif_value_cell_edited_callback (GtkCellRendererText *cell,
                                  gchar               *path_string,
                                  gchar               *new_text,
                                  gpointer             user_data)
{
	GtkTreePath *path;
	GtkTreeIter child = {0};
	GtkTreeIter parent = {0};
	gboolean updated = FALSE;
	ExifTag tag_id;
	ExifIfd ifd_id;

	char * value;

	ExifView::ExifViewImpl *pExifViewImpl = (ExifView::ExifViewImpl*)user_data;

	if (NULL != pExifViewImpl->m_pUIManager)
	{
		QuiverUtils::ConnectUnmodifiedAccelerators(pExifViewImpl->m_pUIManager);
	}

	GtkTreeModel *pTreeModel = gtk_tree_view_get_model(GTK_TREE_VIEW(pExifViewImpl->m_pTreeView));

	ExifData* pExifData = pExifViewImpl->m_pExifData;
	
	path = gtk_tree_path_new_from_string(path_string);
	gtk_tree_model_get_iter(pTreeModel,&child,path);
	

	gtk_tree_model_iter_parent(pTreeModel,&parent, &child);
	
	gtk_tree_model_get (pTreeModel,&parent,EXIF_TREE_COLUMN_TAG_ID,&ifd_id,-1);
	gtk_tree_model_get (pTreeModel,&child,EXIF_TREE_COLUMN_TAG_ID,&tag_id,-1);
	gtk_tree_model_get (pTreeModel,&child,EXIF_TREE_COLUMN_VALUE_TEXT,&value,-1);



	if (EXIF_TAG_ORIENTATION != tag_id && !strcmp(value,new_text) )
	{
		return;
	}
	
	int i;
	
	switch (tag_id)
	{
		case EXIF_TAG_ORIENTATION:
			for (i=0;i<8;i++)
			{
				if ( !strcmp(new_text,orientation_options[i]) )
				{
					updated = exif_update_orientation(pExifData, i+1);	
					
					if (updated)
					{
						/* let us update the thumbnail as well */
						exif_tree_update_thumbnail(pExifViewImpl, GTK_TREE_STORE(pTreeModel));
					}
					
					break;
				}
			}
			break;

		case EXIF_TAG_IMAGE_WIDTH:
		case EXIF_TAG_IMAGE_LENGTH:
		case EXIF_TAG_PIXEL_X_DIMENSION:
		case EXIF_TAG_PIXEL_Y_DIMENSION:
		case EXIF_TAG_RELATED_IMAGE_WIDTH:
		case EXIF_TAG_RELATED_IMAGE_LENGTH:
			exif_update_entry(pExifData, ifd_id,tag_id,new_text);
			updated = TRUE;
			break;

		case EXIF_TAG_DATE_TIME:
		case EXIF_TAG_DATE_TIME_ORIGINAL:
		case EXIF_TAG_DATE_TIME_DIGITIZED:
			//"YYYY:MM:DD HH:MM:SS" 
			if ( exif_date_format_is_valid(new_text) )
			{
				exif_update_entry(pExifData, ifd_id,tag_id,new_text);
				updated = TRUE;
				break;
			} 
			break;

		case EXIF_TAG_SOFTWARE:
		case EXIF_TAG_ARTIST:
		case EXIF_TAG_IMAGE_DESCRIPTION:
		case EXIF_TAG_COPYRIGHT:
			exif_update_entry(pExifData, ifd_id,tag_id,new_text);
			updated = TRUE;
			break;


		case EXIF_TAG_USER_COMMENT:
			break;

		default:
			break;

	}
	if (updated)
	{
		if (pExifViewImpl->m_QuiverFile.Modified())
			
		exif_tree_update_entry(pExifViewImpl, ifd_id, tag_id);
		
		pExifViewImpl->m_QuiverFile.SetExifData(pExifData);
		if (pExifViewImpl->m_QuiverFile.Modified())
		{
		}
		//save_image();
	}
}




static void
exif_convert_arg_to_entry (const char *set_value, ExifEntry *e, ExifByteOrder o)
{
	unsigned int i;
	char *value_p;

	/*
	 * ASCII strings are handled separately,
	 * since they don't require any conversion.
	 */
	if (e->format == EXIF_FORMAT_ASCII) {
		if (e->data) free (e->data);

		e->components = strlen (set_value) + 1;
		e->size = sizeof (char) * e->components;
		e->data = (unsigned char*)malloc (e->size);
		if (!e->data) 
		{
			//fprintf (stderr, ("Not enough memory."));
			//fputc ('\n', stderr);
			//exit (1);
		}
		else
		{
			strcpy ((char*)e->data, set_value);
		}
		return;
	}

	value_p = (char*) set_value;
	for (i = 0; i < e->components; i++) 
	{
		const char *begin, *end;
		unsigned char *buf, s;
		const char comp_separ = ' ';

		begin = value_p;
		value_p = index (begin, comp_separ);
		if (!value_p) 
		{
			if (i != e->components - 1)
			{
				break;
			}
			else
			{
				end = begin + strlen (begin);
			}
		} 
		else
		{
			end = value_p++;
		}

		buf = (unsigned char*)malloc ((end - begin + 1) * sizeof (char));
		strncpy ((char*)buf, begin, end - begin);
		buf[end - begin] = '\0';

		s = exif_format_get_size (e->format);
		switch (e->format)
		{
			case EXIF_FORMAT_ASCII:
				//internal_error (); /* Previously handled */
				break;
			case EXIF_FORMAT_SHORT:
				exif_set_short (e->data + (s * i), o, atoi ((char*)buf));
				break;
			case EXIF_FORMAT_LONG:
				exif_set_long (e->data + (s * i), o, atol ((char*)buf));
				break;
			case EXIF_FORMAT_SLONG:
				exif_set_slong (e->data + (s * i), o, atol ((char*)buf));
				break;
			case EXIF_FORMAT_RATIONAL:
			case EXIF_FORMAT_SRATIONAL:
			case EXIF_FORMAT_BYTE:
			default:
				break;
		}

		free (buf);
	}
}

static int exif_data_get_orientation(ExifData *pExifData)
{
	ExifEntry *e;
	int orientation = 1;
	
	if (pExifData)
	{
		e = exif_content_get_entry (pExifData->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
		if (e)
		{ 
			ExifByteOrder o;
			o = exif_data_get_byte_order (e->parent->parent);
			orientation = exif_get_short (e->data, o);
		}
	}
	return orientation;
}

static gboolean exif_update_orientation(ExifData *pExifData, int value)
{
	gboolean update;
	ExifEntry *e;

	update = FALSE;
	
	/* If the entry doesn't exist, create it. */
	e = exif_content_get_entry (pExifData->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
	if (!e) 
	{
		e = exif_entry_new ();
		exif_content_add_entry (pExifData->ifd[EXIF_IFD_0], e);
		exif_entry_initialize (e, EXIF_TAG_ORIENTATION);
		update = TRUE;
	}
	else
	{
		ExifByteOrder o;
		o = exif_data_get_byte_order (e->parent->parent);
		int orientation = exif_get_short (e->data, o);
		if (orientation != value)
		{
			update = TRUE;
		}
	}

	if (update)
	{
		/* Now set the value and save the data. */
		exif_set_short (e->data , exif_data_get_byte_order (pExifData), value);
	}
	return update;
}

static void exif_update_entry(ExifData *pExifData, ExifIfd ifd,ExifTag tag,const char *value)
{
	ExifEntry *e;

	/* If the entry doesn't exist, create it. */
	e = exif_content_get_entry (pExifData->ifd[ifd], tag);
	if (!e) 
	{
		e = exif_entry_new ();
		exif_content_add_entry (pExifData->ifd[ifd], e);
		exif_entry_initialize (e, tag);
	}

	/* Now set the value and save the data. */
	exif_convert_arg_to_entry (value, e, exif_data_get_byte_order (pExifData));
	
	//save_exif_data_to_file (exifData, *args, fname);
}


/*
 * this routine parses a date in exif date format and checks that it is valid
 * format: YYYY:MM:DD HH:MM:SS
 */

static gboolean exif_date_format_is_valid(const char *date)
{
	gboolean retval = FALSE;
	
	if (19 == strlen(date))
	{
		int year, month, day, hour, min, sec;
		sscanf(date,"%d:%d:%d %d:%d:%d",&year, &month, &day, &hour, &min, &sec);
		struct tm tm_date;
		tm_date.tm_sec = sec;
		tm_date.tm_min = min;
		tm_date.tm_hour = hour;
		tm_date.tm_mday = day;
		tm_date.tm_mon = month -1;
		tm_date.tm_year = year - 1900;


		if ( tm_date.tm_sec == sec &&
			tm_date.tm_min == min &&
			tm_date.tm_hour == hour &&
			tm_date.tm_mday == day &&
			tm_date.tm_mon == month -1 &&
			tm_date.tm_year == year - 1900 )
		{
			retval = TRUE;
		}
		else
		{
		}
		
	}
	else
	{
	}

	return retval;
}

static void
exif_tree_event_add_tag(GtkMenuItem *menuitem, gpointer user_data)
{
	int i,j;
	ExifTagAddRemoveStruct *data = (ExifTagAddRemoveStruct*)user_data;
	ExifData *pExifData = data->pExifViewImpl->m_pExifData;

	for (i = 0;i< EXIF_IFD_COUNT;i++)
	{
		for (j = 0;0 != ifd_editable_tags[i][j];j++)
		{
			if (data->tag == ifd_editable_tags[i][j])
			{
				ExifEntry *entry = exif_content_get_entry(pExifData->ifd[i],data->tag);
				if (NULL == entry)
				{
					// entry does not exist, add it
					entry = exif_entry_new();
					exif_content_add_entry (pExifData->ifd[i], entry);
					if (data->tag == EXIF_TAG_RELATED_IMAGE_WIDTH || data->tag == EXIF_TAG_RELATED_IMAGE_LENGTH)
					{

						ExifByteOrder o = exif_data_get_byte_order (entry->parent->parent);
						entry->tag = data->tag;
						entry->components = 1;
						entry->format = EXIF_FORMAT_SHORT;
						entry->size = exif_format_get_size (entry->format) * entry->components;
						entry->data = (unsigned char *)malloc (entry->size);
						exif_set_short (entry->data, o, 0);
					}
					else
					{
						exif_entry_initialize (entry, data->tag);
					}
					
					/* now add an entry to the tree */
					int tag_id;
					GtkTreeIter iter;
					GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(data->pExifViewImpl->m_pTreeView));
					if ( gtk_tree_model_get_iter_first(model,&iter) )
					{
						do 
						{
							gtk_tree_model_get (model,&iter,EXIF_TREE_COLUMN_TAG_ID,&tag_id,-1);
							if (tag_id == i)
							{
								/* add the entry */
								GtkTreeIter new_child = {0};
								exif_tree_store_add_entry(data->pExifViewImpl,GTK_TREE_STORE(model), &iter, &new_child, entry);

								/* update the selection */
								GtkTreePath *path;
								path = gtk_tree_model_get_path (model,&new_child);
								gtk_tree_view_set_cursor(GTK_TREE_VIEW(data->pExifViewImpl->m_pTreeView),path,NULL,FALSE);

								GtkTreeSelection *selection;
								selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data->pExifViewImpl->m_pTreeView));
								gtk_tree_selection_unselect_all(selection);
								gtk_tree_selection_select_path(selection, path);

								break;
							}
						} while ( gtk_tree_model_iter_next (model,&iter) );
					}
					
					
				}
				break;
			}
		}
	}
	// free the user_data
	g_free(data);
}

static void
exif_tree_event_remove_tag(GtkMenuItem *menuitem, gpointer user_data)
{
	ExifTagAddRemoveStruct *data = (ExifTagAddRemoveStruct*)user_data;
	ExifData *pExifData = data->pExifViewImpl->m_pExifData;
	ExifEntry *entry = exif_content_get_entry(pExifData->ifd[data->ifd],data->tag);
	
	if (NULL != entry)
	{
		exif_content_remove_entry(pExifData->ifd[data->ifd],entry);

		/* now remove the entry from the tree */
		int tag_id;
		GtkTreeIter iter = {0};
		GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(data->pExifViewImpl->m_pTreeView));
		if ( gtk_tree_model_get_iter_first(model,&iter) )
		{
			do 
			{
				gtk_tree_model_get (model,&iter,EXIF_TREE_COLUMN_TAG_ID,&tag_id,-1);
				if (tag_id == data->ifd)
				{
					GtkTreeIter child = {0};
					if ( gtk_tree_model_iter_children(model,&child,&iter) )
					{
						do 
						{
							gtk_tree_model_get (model,&child,EXIF_TREE_COLUMN_TAG_ID,&tag_id,-1);
							if ( tag_id == data->tag )
							{
								gtk_tree_store_remove(GTK_TREE_STORE(model),&child);
								break;
							}
						} while ( gtk_tree_model_iter_next (model,&child) );
					}
					break;
				}
			} while ( gtk_tree_model_iter_next (model,&iter) );

		}
	}
	g_free(data);
}

static void
exif_tree_update_thumbnail(ExifView::ExifViewImpl *pExifViewImpl, GtkTreeStore *store)
{
	GtkTreeIter iter = {0};
	GtkTreeModel *model = GTK_TREE_MODEL(store);
	GdkPixbuf *pixbuf;
	if ( gtk_tree_model_get_iter_first(model,&iter) )
	{
		do 
		{
			GtkTreeIter child = {0};
			if ( gtk_tree_model_iter_children(model,&child,&iter) )
			{
				do 
				{
					char *name;
					gtk_tree_model_get (model,&child,EXIF_TREE_COLUMN_VALUE_PIXBUF,&pixbuf,-1);
					gtk_tree_model_get (model,&child,EXIF_TREE_COLUMN_NAME,&name,-1);
					if ( NULL != pixbuf )
					{
						
						pixbuf = pExifViewImpl->m_QuiverFile.GetExifThumbnail();
						GdkPixbuf *new_pixbuf = QuiverUtils::GdkPixbufExifReorientate(pixbuf,exif_data_get_orientation(pExifViewImpl->m_pExifData));
						if (NULL != new_pixbuf)
						{
							g_object_unref(pixbuf);
							pixbuf = new_pixbuf;
						}
						gtk_tree_store_set (store,&child,EXIF_TREE_COLUMN_VALUE_PIXBUF,pixbuf,-1);
						g_object_unref(pixbuf);
						break;
					}
				} while ( gtk_tree_model_iter_next (model,&child) );
			}
		} while ( gtk_tree_model_iter_next (model,&iter) );

	}
}


static void exif_tree_update_iter_entry (ExifView::ExifViewImpl *pExifViewImpl, GtkTreeIter *iter, ExifEntry *entry)
{
	gboolean editable = FALSE;

	int i,j;
	const char *name;
	const char *title;
	const char *description;
	const char *value;
	const unsigned int size_max = 2048;
	char val[size_max];
	value = exif_entry_get_value (entry, val,size_max);
	
	name = exif_tag_get_name (entry->tag);
	title = exif_tag_get_title(entry->tag);
	description = exif_tag_get_description (entry->tag);

	
	for (i = 0;i< EXIF_IFD_COUNT && !editable;i++)
	{
		for (j = 0; 0 != ifd_editable_tags[i][j] ;j++)
		{
			if (entry->tag == ifd_editable_tags[i][j])
			{
				editable = TRUE;
				break;
			}
		}
	}
	if (EXIF_TAG_USER_COMMENT == entry->tag)
	{
		const unsigned int character_code_size = 8;
		if (character_code_size <= entry->size && entry->size - character_code_size < size_max )
		{
			char character_code[8];
			
			memcpy(character_code,entry->data,character_code_size);

			unsigned char *start = entry->data+character_code_size;
			memcpy(val,start,entry->size - character_code_size);
			val[entry->size - character_code_size] = '\0';

		}
		else
		{
			val[0] = '\0';
		}
	}
	
	GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW(pExifViewImpl->m_pTreeView));
	GtkTreeStore *store = GTK_TREE_STORE(model);
	
	if (EXIF_TAG_ORIENTATION == entry->tag)
	{
		ExifByteOrder o;
		o = exif_data_get_byte_order (entry->parent->parent);
		int orientation = exif_get_short (entry->data, o);

		gtk_tree_store_set (store, iter,
			EXIF_TREE_COLUMN_TAG_ID,entry->tag,
            EXIF_TREE_COLUMN_NAME, title,
            EXIF_TREE_COLUMN_VALUE_ORIENTATION,orientation,
            EXIF_TREE_COLUMN_IS_VISIBLE_ORIENTATION, TRUE,
            EXIF_TREE_COLUMN_IS_EDITABLE, editable,
            -1);
	}
	else
	{
		gtk_tree_store_set (store, iter,
			EXIF_TREE_COLUMN_TAG_ID,entry->tag,
            EXIF_TREE_COLUMN_NAME, title,
            EXIF_TREE_COLUMN_VALUE_TEXT,value,
            EXIF_TREE_COLUMN_IS_VISIBLE_TEXT, TRUE,
            EXIF_TREE_COLUMN_IS_EDITABLE, editable,
            -1);
	}


}

static void exif_tree_update_entry (ExifView::ExifViewImpl *pExifViewImpl, ExifIfd ifd, ExifTag tag)
{
	ExifData *pExifData = pExifViewImpl->m_pExifData;
	ExifEntry *entry = exif_content_get_entry(pExifData->ifd[ifd],tag);
	
	if (NULL != entry)
	{
		int tag_id;
		GtkTreeIter iter = {0};
		GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW(pExifViewImpl->m_pTreeView));
		if ( gtk_tree_model_get_iter_first(model,&iter) )
		{
			do 
			{
				gtk_tree_model_get (model,&iter,EXIF_TREE_COLUMN_TAG_ID,&tag_id,-1);
				if (tag_id == ifd)
				{
					GtkTreeIter child = {0};
					if ( gtk_tree_model_iter_children(model,&child,&iter) )
					{
						do 
						{
							gtk_tree_model_get (model,&child,EXIF_TREE_COLUMN_TAG_ID,&tag_id,-1);
							if ( tag_id == tag )
							{
								exif_tree_update_iter_entry(pExifViewImpl,&child,entry);
								break;
							}
						} while ( gtk_tree_model_iter_next (model,&child) );
					}
					break;
				}
			} while ( gtk_tree_model_iter_next (model,&iter) );

		}
	}

}

// nested class methods

void ExifView::ExifViewImpl::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event)
{

}


