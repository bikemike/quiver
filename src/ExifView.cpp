#include <gtk/gtk.h>
#include "ExifView.h"


#include <exif-ifd.h>
#include <exif-entry.h>
#include <exif-data.h>
#include <exif-loader.h>
#include <exif-tag.h>

/* prototypes */

static GtkTreeModel * create_numbers_model (void);

void exif_orientation_to_text (GtkTreeViewColumn *tree_column,
	                GtkCellRenderer   *cell, 
                    GtkTreeModel      *tree_model,
	                GtkTreeIter       *iter, 
                    gpointer           data);

void exif_content_foreach_entry_func (ExifEntry *entry, void *user_data);
void exif_tree_add_entry (GtkTreeStore *store,GtkTreeIter *parent, GtkTreeIter *new_child, ExifEntry *entry);
void exif_tree_update_iter_entry (GtkTreeStore *store, GtkTreeIter *iter, ExifEntry *entry);





/* private implementation */
class ExifView::ExifViewImpl
{

public:
	QuiverFile m_QuiverFile;
	GtkWidget *m_TreeView;
	GtkWidget *m_ScrolledWindow;
	guint m_iIdleLoadID;
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
	GtkTreeStore *store;
	GtkTreeIter *parent;
} ForEachEntryData;


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


ExifView::ExifView() : m_ExifViewImplPtr (new ExifViewImpl() )
{
	m_ExifViewImplPtr->m_iIdleLoadID = 0;
	m_ExifViewImplPtr->m_TreeView = NULL;

	GtkWidget *treeview;
	printf("quiver_exif_tree_new start\n");
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	//GtkTreeSelection *selection;

	/* Create a view */
	//treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	treeview = gtk_tree_view_new();//_with_model (GTK_TREE_MODEL (store));
	
	
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
	//g_signal_connect(renderer, "edited", (GCallback) exif_value_cell_edited_callback, store);
	//g_signal_connect(renderer, "edited", (GCallback) exif_value_cell_edited_callback, NULL);

	g_object_set (G_OBJECT (renderer),  "mode",GTK_CELL_RENDERER_MODE_EDITABLE,  NULL);
	g_object_set (G_OBJECT (renderer),  "cell-background-gdk", &highlight_color2,  NULL);
	g_object_set (G_OBJECT (renderer),  "background-gdk", &highlight_color1,  NULL);

	g_object_set (G_OBJECT (renderer),  "wrap-width",200,  NULL);
	g_object_set (G_OBJECT (renderer),  "wrap-mode",PANGO_WRAP_WORD_CHAR,  NULL);

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

	//g_signal_connect(renderer, "edited", (GCallback) exif_value_cell_edited_callback, store);
	//g_signal_connect(renderer, "edited", (GCallback) exif_value_cell_edited_callback, NULL);
	//g_signal_connect(renderer, "editing-started", (GCallback) exif_value_editing_started_callback, NULL);



	renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_end (column,renderer,FALSE);

	gtk_tree_view_column_add_attribute(column,renderer,"pixbuf",EXIF_TREE_COLUMN_VALUE_PIXBUF);
	gtk_tree_view_column_add_attribute(column,renderer,"visible",EXIF_TREE_COLUMN_IS_VISIBLE_PIXBUF);

	//gtk_tree_view_column_set_spacing(column,0);
	//gtk_tree_view_column_set_sizing (column,GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (column,TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);


	
//	gtk_tree_view_set_row_separator_func(GTK_TREE_VIEW (treeview),folder_tree_is_separator,NULL,NULL);
	/*GtkDestroyNotify destroy);*/
	
	/*
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW (treeview),FALSE);
	
	gtk_tree_view_set_search_column(GTK_TREE_VIEW (treeview),FILE_TREE_COLUMN_DISPLAY_NAME);
	
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW (treeview));
	gtk_tree_selection_set_mode(selection,GTK_SELECTION_MULTIPLE);
	
	g_signal_connect(G_OBJECT(selection),"changed",G_CALLBACK(folder_tree_selection_changed),NULL);
	g_signal_connect(treeview, "button-press-event", (GCallback) view_onButtonPressed, NULL);
	g_signal_connect(treeview, "key-press-event", (GCallback) view_on_key_press, NULL);
	g_signal_connect(treeview, "row-activated", (GCallback) view_onRowActivated, NULL);
	g_signal_connect(treeview, "row-expanded", (GCallback) signal_folder_tree_row_expanded,NULL);
	
	printf("quiver_folder_tree_new end\n");

	*/
	//g_signal_connect(treeview, "button-press-event", (GCallback) exif_tree_event_button_press, NULL);
	//g_signal_connect(treeview, "popup-menu", (GCallback) exif_tree_event_popup_menu, NULL);

	//g_signal_connect(treeview, "size-allocate", (GCallback) exif_tree_size_allocate, NULL);

	gtk_tree_view_expand_all (GTK_TREE_VIEW(treeview));

	GtkWidget * scrolled_window = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrolled_window),treeview);
	gtk_widget_show(scrolled_window);
	gtk_widget_show(treeview);

	m_ExifViewImplPtr->m_TreeView = treeview;
	m_ExifViewImplPtr->m_ScrolledWindow = scrolled_window;
}

ExifView::~ExifView()
{
}

GtkWidget *
ExifView::GetWidget()
{
	return m_ExifViewImplPtr->m_ScrolledWindow;
}

gboolean exif_view_idle_load_exif_tree_view(gpointer data)
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
	ExifData *exifData = NULL;
	exifData = pExifViewImpl->m_QuiverFile.GetExifData();
	if (NULL != exifData)
	{
		GdkPixbuf *pixbuf = pExifViewImpl->m_QuiverFile.GetExifThumbnail();
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
			if (exifData->ifd[i]->count)
			 {
				const char *exif_ifd = exif_ifd_get_name((ExifIfd)i);
				gtk_tree_store_append (store, &iter1, NULL);  /* Acquire a top-level iterator */
				gtk_tree_store_set (store, &iter1,
					EXIF_TREE_COLUMN_NAME, exif_ifd,
					EXIF_TREE_COLUMN_IS_GROUP, TRUE,
					EXIF_TREE_COLUMN_TAG_ID,i,
					EXIF_TREE_COLUMN_IS_VISIBLE_TEXT, TRUE,
					-1);

				ForEachEntryData entry_data;
				entry_data.store = store;
				entry_data.parent = &iter1;
				exif_content_foreach_entry (exifData->ifd[i],
							 exif_content_foreach_entry_func,&entry_data);
			}
		}
		// maker notes
		ExifMnoteData * mnote_data = exif_data_get_mnote_data (exifData);

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
	}

	gdk_threads_enter();
	
	gtk_tree_view_set_model(GTK_TREE_VIEW(pExifViewImpl->m_TreeView),GTK_TREE_MODEL (store));

	gtk_tree_view_expand_all (GTK_TREE_VIEW(pExifViewImpl->m_TreeView));

	pExifViewImpl->m_iIdleLoadID = 0;
	gdk_threads_leave();

	//exif_mnote_data_unref (mnote_data);
	exif_data_unref(exifData);

	g_object_unref (G_OBJECT (store));


	return FALSE;
}

void 
ExifView::SetQuiverFile(QuiverFile quiverFile)
{
	m_ExifViewImplPtr->m_QuiverFile = quiverFile;

	if (0 != m_ExifViewImplPtr->m_iIdleLoadID)
	{
		g_source_remove(m_ExifViewImplPtr->m_iIdleLoadID );
	}
	m_ExifViewImplPtr->m_iIdleLoadID = g_timeout_add(300,exif_view_idle_load_exif_tree_view,m_ExifViewImplPtr.get());

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




void 
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

void exif_content_foreach_entry_func (ExifEntry *entry, void *user_data)
{
	ForEachEntryData *entry_data = (ForEachEntryData*)user_data;
	GtkTreeIter new_child = {0};  /* Child iter  */
	exif_tree_add_entry(entry_data->store,entry_data->parent,&new_child,entry);
}

void exif_tree_add_entry (GtkTreeStore *store,GtkTreeIter *parent, GtkTreeIter *new_child, ExifEntry *entry)
{
	gtk_tree_store_append (store, new_child, parent);
	exif_tree_update_iter_entry(store,new_child,entry);

}


void exif_tree_update_iter_entry (GtkTreeStore *store, GtkTreeIter *iter, ExifEntry *entry)
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
		int orientation = 1;//FIXME:exif_data_get_orientation();

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

