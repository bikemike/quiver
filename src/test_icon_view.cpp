#include <config.h>
#include <libgnomevfs/gnome-vfs.h>
//#include <gtk/gtkfilesystem.h>

#include <libgnomeui/gnome-icon-lookup.h>
#include <pthread.h>


#include "ImageList.h"
#include "QuiverFile.h"
#include "QuiverUtils.h"
#include "Timer.h"

using namespace std;

GtkWidget * window;
GtkWidget * icon_view;
GtkListStore *store;

int icon_width = 140;
int icon_height = 140;
int thumb_size = 128;

GdkPixbuf* GetFancyIcon(QuiverFile f);

// signal functions
gboolean signal_iconview_activate_cursor_item(GtkIconView *iconview,gpointer user_data)
{
	printf("signal_iconview_activate_cursor_item\n");
	return TRUE;
}

gboolean signal_iconview_item_activated(GtkIconView *iconview,GtkTreePath *arg1,gpointer user_data)
{
	printf("signal_iconview_item_activated\n");
	return TRUE;
}
gboolean signal_iconview_move_cursor(GtkIconView *iconview,GtkMovementStep arg1,gint arg2,gpointer user_data)
{
	printf("signal_iconview_move_cursor\n");
	return TRUE;
}
gboolean signal_iconview_select_all(GtkIconView *iconview,gpointer user_data)
{
	printf("signal_iconview_select_all\n");
	return TRUE;
}
gboolean signal_iconview_select_cursor_item(GtkIconView *iconview,gpointer user_data)
{
	printf("signal_iconview_select_cursor_item\n");
	return TRUE;
}
gboolean signal_iconview_selection_changed(GtkIconView *iconview,gpointer user_data)
{
	printf("signal_iconview_selection_changed\n");
	return TRUE;
}
gboolean signal_iconview_set_scroll_adjustments(GtkIconView *iconview,GtkAdjustment arg1,GtkAdjustment arg2,gpointer user_data)
{
	printf("signal_iconview_set_scroll_adjustments\n");
	return TRUE;
}
gboolean signal_iconview_toggle_cursor_item(GtkIconView *iconview,gpointer user_data)
{
	printf("signal_iconview_toggle_cursor_item\n");
	return TRUE;
}
gboolean signal_iconview_unselect_all(GtkIconView *iconview,gpointer user_data)
{
	printf("signal_iconview_unselect_all\n");
	return TRUE;
}

gboolean event_iconview_motion_notify (GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	printf("widget allocation.height: %d\n",widget->allocation.height);
	int x=0, y=0;
	
	GdkModifierType state;
	
	if (event->is_hint)
	{
		//printf("hint\n");
		gdk_window_get_pointer (event->window, &x, &y, &state);
	}		
	else
	{
		//printf("event\n");
		x = (int)event->x;
		y = (int)event->y;
		state = (GdkModifierType)event->state;
		
	}
	
	GtkTreePath *tree_path;
	GtkCellRenderer * cell;
	GtkIconView *icon_view = GTK_ICON_VIEW(widget);
	
	tree_path = gtk_icon_view_get_path_at_pos(icon_view,x,y);
/*
	gboolean rval = gtk_icon_view_get_item_at_pos   (icon_view,
                                             x,
                                             y,
                                             tree_path,
                                             cell);
*/
	if (NULL != tree_path)
	{
		gchar *uri = NULL;
		GtkTreeModel* model = gtk_icon_view_get_model(icon_view);
		GtkTreeIter iter;
		gtk_tree_model_get_iter (model, &iter, tree_path);
		gtk_tree_model_get(model,&iter, 2, &uri, -1);

		printf("tree path %s: %s\n",gtk_tree_path_to_string(tree_path),uri);
	}
	printf("%dx%d\n",x,y);
	
	gtk_tree_path_free(tree_path);
	
	return FALSE;
}


void* Run(void * data)
{
	Timer t(false);
	printf("sleeping for .1s\n");
	gdk_threads_enter ();
	pthread_yield();
	gdk_threads_leave ();
	usleep(100000);
	
	printf("slept for .1s\n");
	
	GtkTreeIter   iter;
	gdk_threads_enter ();
	
	gboolean rval; 
	
	gint size = gtk_tree_model_iter_n_children (GTK_TREE_MODEL(store),NULL);

	/*
	rval = gtk_tree_model_get_iter_first   (GTK_TREE_MODEL(store),
                                             &iter);
	*/
	
	GtkTreePath *start_path;
	GtkTreePath *end_path;
	rval = gtk_icon_view_get_visible_range (GTK_ICON_VIEW(icon_view),
                                             &start_path,
                                             &end_path);
											 
  	gtk_tree_model_get_iter (GTK_TREE_MODEL(store), &iter, start_path);
  	gtk_tree_path_free (start_path);
	gtk_tree_path_free (end_path);
	
	gdk_threads_leave ();
	ImageCache iconCache(size);
	
	int i = 1;
	int n = 10;
	//gdk_threads_enter ();
	while ( rval )
	{
		gdk_threads_enter ();
		GdkPixbuf * pixbuf = NULL;
		gchar *uri = NULL;
		
		gtk_tree_model_get(GTK_TREE_MODEL(store),&iter, 2, &uri, -1);

				
		// hmm
		if (NULL == uri)
		{
			continue;
		}
		QuiverFile f(uri);
		g_free(uri);
		pixbuf = GetFancyIcon(f);//GetFancyIcon(f);
		
		//printf("adding pixbuf to cache: %s\n", f.GetURI());
		if (NULL != pixbuf)
		{
			iconCache.AddPixbuf(f.GetURI(),pixbuf);
			g_object_unref(pixbuf);
		}
		
		if (NULL != pixbuf)
		{
/*
			g_signal_handler_block ()
			g_signal_stop_emission_by_name  (store,"row_changed");
			g_signal_stop_emission_by_name  (store,"row_inserted");
			g_signal_stop_emission_by_name  (store,"row_deleted");
			g_signal_stop_emission_by_name  (store,"rows_reordered");
*/
/*			GdkPixbuf * store_buff = NULL;
			gtk_tree_model_get (GTK_TREE_MODEL(store_, &iter,1, &store_buff,-1);
			store_buff = pixbuf;
*/			
			gtk_list_store_set (store, &iter,1, pixbuf,-1);
		}
		
		rval = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		
		gdk_threads_leave ();
		if (0 == i%n)
		{
			//pthread_yield();
			usleep(10);
		}
		i++;
	}
	//gdk_threads_leave ();
	
	rval = gtk_tree_model_get_iter_first   (GTK_TREE_MODEL(store),
                                             &iter);
	return NULL;
}

void StartThread()
{
	pthread_t m_pthread_id;
	pthread_create(&m_pthread_id, NULL, Run, NULL);
	pthread_detach(m_pthread_id);
}
void Init()
{
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_move(GTK_WINDOW(window),100,100);
	gtk_window_set_default_size (GTK_WINDOW(window),800,600);


	
	/* Set up our GUI elements */
	GtkWidget *menubar;
	GtkWidget *menuitem;

	menubar =  gtk_menu_bar_new();
	
	menuitem = gtk_menu_item_new_with_mnemonic ("_File");
	gtk_menu_bar_append(menubar,menuitem);
	
	menuitem = gtk_menu_item_new_with_mnemonic ("_Edit");
	gtk_menu_bar_append(menubar,menuitem);

	menuitem = gtk_menu_item_new_with_mnemonic ("_View");
	gtk_menu_bar_append(menubar,menuitem);

	menuitem = gtk_menu_item_new_with_mnemonic ("_Tools");
	gtk_menu_bar_append(menubar,menuitem);

	menuitem = gtk_menu_item_new_with_mnemonic ("_Window");
	gtk_menu_bar_append(menubar,menuitem);



	menuitem = gtk_menu_item_new_with_mnemonic ("_Help");

	//menu_path = gtk_menu_factory_find (factory,  "<MyApp>/Help");
	//gtk_menu_item_right_justify(menu_path->widget);

	//If you do not use the MenuFactory, you should simply use:
	gtk_menu_item_right_justify(GTK_MENU_ITEM(menuitem));

	gtk_menu_bar_append(menubar,menuitem);


	GtkWidget* statusbar;
	statusbar = gtk_statusbar_new ();
	
	GtkWidget* vbox;
	
	vbox = gtk_vbox_new(FALSE,0);
	
	icon_view =   gtk_icon_view_new ();
	
	g_signal_connect (G_OBJECT (icon_view), "activate_cursor_item",
	   			G_CALLBACK (signal_iconview_activate_cursor_item), NULL);
	   			
	g_signal_connect (G_OBJECT (icon_view), "item_activated",
	   			G_CALLBACK (signal_iconview_item_activated), NULL);
	   			
	g_signal_connect (G_OBJECT (icon_view), "move_cursor",
	   			G_CALLBACK (signal_iconview_move_cursor), NULL);
	   			
	g_signal_connect (G_OBJECT (icon_view), "select_all",
	   			G_CALLBACK (signal_iconview_select_all), NULL);

	g_signal_connect (G_OBJECT (icon_view), "select_cursor_item",
	   			G_CALLBACK (signal_iconview_select_cursor_item), NULL);
	   				   			
	g_signal_connect (G_OBJECT (icon_view), "selection_changed",
	   			G_CALLBACK (signal_iconview_selection_changed), NULL);
	   			
	g_signal_connect (G_OBJECT (icon_view), "set_scroll_adjustments",
   			G_CALLBACK (signal_iconview_set_scroll_adjustments), NULL);
	g_signal_connect (G_OBJECT (icon_view), "toggle_cursor_item)",
   			G_CALLBACK (signal_iconview_toggle_cursor_item), NULL);
   			
	g_signal_connect (G_OBJECT (icon_view), "unselect_all",
   			G_CALLBACK (signal_iconview_unselect_all), NULL);
   			
 	gtk_widget_add_events (icon_view, GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);   			
 	
	g_signal_connect (G_OBJECT (icon_view), "motion_notify_event",
				G_CALLBACK (event_iconview_motion_notify), NULL);   			

	GtkWidget * scrolled_window = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	//gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window),icon_view);
	gtk_container_add(GTK_CONTAINER(scrolled_window),icon_view);
	
	gtk_container_add (GTK_CONTAINER (window),scrolled_window);
	
	/* Show the application window */
	
	gtk_container_set_border_width (GTK_CONTAINER(window),0);
	//gtk_window_fullscreen(GTK_WINDOW(window));

	gtk_widget_show_all (window);
	/* Enter the main event loop, and wait for user interaction */

	//{ "application/x-rootwin-drop", 0, TARGET_ROOTWIN }
	StartThread();
}

int Show()
{
	gtk_main ();
	
	/*
	start a thread to change the rows!
	void        gtk_tree_model_row_changed      (GtkTreeModel *tree_model,
                                             GtkTreePath *path,
                                             GtkTreeIter *iter);

	*/
	/* The user lost interest */
	return 0;
	
}

GdkPixbuf* GetFancyIcon(QuiverFile f)
{
	//Timer t(false);
	// bgcolor 
	GtkStyle* style =   gtk_widget_get_style(icon_view);
	GdkColor bgcolor = style->base[GTK_STATE_NORMAL];

	GdkPixbuf *pixbuf = f.GetThumbnail();
	if (NULL == pixbuf)
		return NULL;

	guchar *buffer;
	
	int w,h,src_rowstride;
	w = gdk_pixbuf_get_width(pixbuf);
	h = gdk_pixbuf_get_height(pixbuf);
	src_rowstride = gdk_pixbuf_get_rowstride(pixbuf);
	
	if (w > thumb_size || h > thumb_size)
	{
		int nw,nh;
		if (w > thumb_size && w > h)
		{
			nw = thumb_size;
			nh = (int) (thumb_size * (double)h/(double)w);
		}
		else
		{
			nh = thumb_size;
			nw = (int)(thumb_size * (double)w / (double)h);
		}
		//printf("new size %d %d\n",nw,nh);
		GdkPixbuf* newpixbuf = gdk_pixbuf_scale_simple (
							pixbuf,
							nw,
							nh,
							GDK_INTERP_BILINEAR);
							//GDK_INTERP_NEAREST);
		g_object_unref(pixbuf);
		//printf("resized\n");
		pixbuf = newpixbuf;

	}
	w = gdk_pixbuf_get_width(pixbuf);
	h = gdk_pixbuf_get_height(pixbuf);
	
	GdkPixbuf* draw_buf = gdk_pixbuf_new (
							gdk_pixbuf_get_colorspace(pixbuf),
							TRUE,//gdk_pixbuf_get_has_alpha(pixbuf),
							gdk_pixbuf_get_bits_per_sample(pixbuf),
							icon_width,
							icon_height);
                                             
	gdk_pixbuf_fill (draw_buf,bgcolor.red<<24 | bgcolor.green<<16 | bgcolor.blue<<8 | 0xff);
	
	//calculate center
	double top_x,top_y;
	top_x = (int)(icon_width/2 - w/2) - .5;
	top_y = (int)(icon_height/2 - h/2) - .5;

	buffer = gdk_pixbuf_get_pixels(draw_buf);
	int rowstride = gdk_pixbuf_get_rowstride(draw_buf);
	
	cairo_surface_t *surface = cairo_image_surface_create_for_data (
							buffer,
							CAIRO_FORMAT_ARGB32,
							icon_width,icon_height,
							rowstride);
	
	cairo_t * cr = cairo_create(surface);

	// set the color for the drop shadow
	// for gdkpixbuf rgba data this is going to be bgra
	cairo_set_source_rgba (cr, 0, 0, 0, .1);
	cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
	for (int i = 8;i >=1; i-=2)
	{
		cairo_set_line_width(cr,i);
		cairo_rectangle (cr, top_x+1, top_y+1, w, h);
		cairo_stroke(cr);
	}

	cairo_rectangle (cr, top_x, top_y, w, h);

	gdk_pixbuf_composite(pixbuf,
							draw_buf,
							(int)top_x,
							(int)top_y,
							w,
							h,
							(int)top_x,
							(int)top_y,
							1,
							1,
							GDK_INTERP_NEAREST,
							255);

	cairo_set_line_width(cr,1);
	cairo_set_source_rgba (cr, 0, 0, 0, 1);

	cairo_stroke(cr);	
	
	cairo_set_source_rgba (cr, 0, 0, 0, .15);
	double dashes[] = {2.0,2.0};
	
	// set the offset to .5 so 1 pixel dashes don't look like a solid line
	cairo_set_dash(cr,dashes,2,.5);
	cairo_rectangle (cr, .5, .5, icon_width-1, icon_height -1);
	cairo_stroke(cr);
	
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	
	g_object_unref(pixbuf);
	return draw_buf;
}

GdkPixbuf * GetStockIcon(QuiverFile f)
{
	
	GError *error = NULL;

	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();
	GnomeIconLookupResultFlags result;
	/*
	GNOME_ICON_LOOKUP_RESULT_FLAGS_NONE
	GNOME_ICON_LOOKUP_FLAGS_NONE
	*/
	GnomeVFSFileInfo * file_info = f.GetFileInfo();
	
	char* icon_name = gnome_icon_lookup (icon_theme,
										 NULL,
										 f.GetURI(),
										 NULL,
										 file_info,
										 file_info->mime_type,
										 GNOME_ICON_LOOKUP_FLAGS_NONE,
										 &result);
	//printf("this is the icon path: %s\n",icon_name);
	GdkPixbuf* pixbuf = gtk_icon_theme_load_icon (icon_theme,
                                             icon_name,
                                             48,
                                             GTK_ICON_LOOKUP_USE_BUILTIN,
                                             &error);
	
	GdkPixbuf* draw_buf = gdk_pixbuf_new (
								gdk_pixbuf_get_colorspace(pixbuf),
											TRUE,//gdk_pixbuf_get_has_alpha(pixbuf),
                                             gdk_pixbuf_get_bits_per_sample(pixbuf),
                                             icon_width,
                                             icon_height);
	int w = gdk_pixbuf_get_width(pixbuf);
	int h = gdk_pixbuf_get_height(pixbuf);
	
	int top_x,top_y;
	top_x = (int)(icon_width/2 - w/2);
	top_y = (int)(icon_height/2 - h/2);
	
	GtkStyle* style =   gtk_widget_get_style(icon_view);
	GdkColor bgcolor = style->base[GTK_STATE_NORMAL];
	
	gdk_pixbuf_fill (draw_buf,bgcolor.red<<24 | bgcolor.green<<16 | bgcolor.blue<<8 | 0xff);

	
	gdk_pixbuf_composite            (pixbuf,
                                             draw_buf,
                                             (int)top_x,
                                             (int)top_y,
                                             w,
                                             h,
                                             (int)top_x,
                                             (int)top_y,
                                             1,
                                             1,
                                             GDK_INTERP_NEAREST,
                                             255);
    g_object_unref(pixbuf);

	int rowstride = gdk_pixbuf_get_rowstride(draw_buf);
	
	cairo_surface_t *surface = cairo_image_surface_create_for_data (
							gdk_pixbuf_get_pixels(draw_buf),
							CAIRO_FORMAT_ARGB32,
							icon_width,icon_height,
							rowstride);
	
	cairo_t * cr = cairo_create(surface);

	cairo_set_line_width(cr,1);
	cairo_set_source_rgba (cr, 0, 0, 0, .15);
	double dashes[] = {2.0,2.0};
	
	// set the offset to .5 so 1 pixel dashes don't look like a solid line
	cairo_set_dash(cr,dashes,2,.5);
	cairo_rectangle (cr, .5, .5, icon_width-1, icon_height -1);
	cairo_stroke(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	
	gnome_vfs_file_info_unref(file_info);
	return draw_buf;
}

void PopulateIconView(list<string> &files)
{
	ImageList image_list;
	image_list.SetImageList(&files);

	store = gtk_list_store_new (3, G_TYPE_STRING, GDK_TYPE_PIXBUF,G_TYPE_STRING);
	GtkTreeIter   iter;

	if (image_list.GetSize())
	{
		
		QuiverFile f = *image_list.GetCurrent();

		

		//GdkPixbuf * pixbuf = f.GetThumbnail();

		GdkPixbuf *pixbuf = GetStockIcon(f);//GetFancyIcon(f);
		if (NULL != pixbuf)
		{
			gtk_list_store_append  (store,&iter);
			
			GnomeVFSFileInfo * info = f.GetFileInfo();
			gtk_list_store_set (store, &iter,0,info->name,1, pixbuf,2,f.GetURI(),-1);
			gnome_vfs_file_info_unref(info);
			
		}
	}
	while (image_list.HasNext() )
	{
		


		QuiverFile f = *image_list.GetNext();

		GdkPixbuf *pixbuf = GetStockIcon(f);//GetFancyIcon(f);
		if (NULL != pixbuf)
		{
			
			gtk_list_store_append  (store,&iter);
			GnomeVFSFileInfo * info = f.GetFileInfo();
			gtk_list_store_set (store, &iter,0,info->name,1, pixbuf,2,f.GetURI(),-1);
			gnome_vfs_file_info_unref(info);
			//printf("appended\n");
		}
	}
	
	gtk_icon_view_set_model (GTK_ICON_VIEW(icon_view), (GTK_TREE_MODEL (store)));
	//gtk_icon_view_set_text_column   (GTK_ICON_VIEW(icon_view),0);
	gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW(icon_view),1);
	gtk_icon_view_set_selection_mode (GTK_ICON_VIEW(icon_view),GTK_SELECTION_MULTIPLE);
	//gtk_icon_view_set_item_width(GTK_ICON_VIEW(icon_view),50);
}

int main (int argc, char **argv)
{
	/* Initialize i18n support */


  /* init threads */
	g_thread_init (NULL);
	gdk_threads_init ();
	gdk_threads_enter ();


	/* Initialize the widget set */
	gtk_init (&argc, &argv);
	
	list<string> files;
	for (int i =1;i<argc;i++)
	{	
		printf ("command line %d : %s\n",i,argv[i]);
		files.push_back(argv[i]);
	}
	if (argc == 1)
	{	
		char buf[1024];
		getcwd(buf,sizeof(buf));
		files.push_back(buf);
	}
	//init gnome-vfs
	if (!gnome_vfs_init ()) {
		printf ("Could not initialize GnomeVFS\n");
		return 1;
	}
	
	Init();
	PopulateIconView(files);
	int rval = Show();	
	gdk_threads_leave();
	
	return rval;
}

