#include <config.h>
#include <libgnomevfs/gnome-vfs.h>
//#include <gtk/gtkfilesystem.h>
#include <libart_lgpl/libart.h>
#include <libgnomeui/gnome-icon-lookup.h>
#include <pthread.h>

#include "art_rgba_svp.h"
#include "art_rgba_rgba_affine.h"

#include "ImageList.h"
#include "QuiverFile.h"

using namespace std;

GtkWidget * window;
GtkWidget * icon_view;
GtkListStore *store;

GdkPixbuf* GetFancyIcon(QuiverFile f);

//void Run(void);
void* Run(void * data)
{
	

	printf("sleeping for 2s\n");
	gdk_threads_enter ();
	pthread_yield();
	gdk_threads_leave ();
	usleep(1500000);
	
	printf("slept for 2s\n");
	
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
	
	

	while ( rval )
	{
			
		
		//printf("hello why sleep no work?\n");
		//store
		GdkPixbuf * pixbuf = NULL;
		gchar *uri = NULL;
		/*
		gtk_tree_model_get_value(GTK_TREE_MODEL(store),&iter,  0,  value);
		*/
		gdk_threads_enter ();
		gtk_tree_model_get(GTK_TREE_MODEL(store),&iter, 2, &uri, -1);
		gdk_threads_leave ();
		//printf("this is the uri? : %s\n",uri);
				
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

		

		gdk_threads_enter ();
		
		if (NULL != pixbuf)
		{
/*
			g_signal_handler_block ()
			g_signal_stop_emission_by_name  (store,"row_changed");
			g_signal_stop_emission_by_name  (store,"row_inserted");
			g_signal_stop_emission_by_name  (store,"row_deleted");
			g_signal_stop_emission_by_name  (store,"rows_reordered");
*/			
			gtk_list_store_set (store, &iter,1, pixbuf,-1);
		}
		//gtk_list_store_set_value(store,&iter,1,&value);
		
		rval = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		gdk_threads_leave ();
	}
	
	rval = gtk_tree_model_get_iter_first   (GTK_TREE_MODEL(store),
                                             &iter);
	//printf("this is the second while loop\n");
	/*
	GtkListStore *	store2 = gtk_list_store_new (2, G_TYPE_STRING, GDK_TYPE_PIXBUF);
	GtkTreeIter iter2;	
	while (rval)
	{
		//usleep(100000);
		gdk_threads_enter ();
		
		GdkPixbuf * pixbuf = NULL;
		gchar *uri = NULL;
		
		gtk_tree_model_get(GTK_TREE_MODEL(store),&iter, 0, &uri, -1);
		//printf("getting from cache\n");
		pixbuf = iconCache.GetPixbuf(uri);

		if (NULL != pixbuf)
		{
			//printf("not null\n");
			
			gtk_list_store_append  (store2,&iter2);
			gtk_list_store_set (store2, &iter2,0,"",1, pixbuf,-1);
			
			//gtk_list_store_set (store2, &iter,1, pixbuf,-1);
			//gtk_list_store_set_value(store,&iter,1,&value);
		}
		//g_object_unref(pixbuf);
		rval = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		gdk_threads_leave ();
	}
	
	gdk_threads_enter ();
	
	gtk_icon_view_set_model (GTK_ICON_VIEW(icon_view), (GTK_TREE_MODEL (store2)));
	//gtk_icon_view_set_text_column   (GTK_ICON_VIEW(icon_view),0);
	gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW(icon_view),1);
	*/
	
	gdk_threads_leave ();
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
	gtk_container_add (GTK_CONTAINER (window),icon_view);
	
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

GdkPixbuf * GdkPixbufExifReorientate(GdkPixbuf * pixbuf, int orientation)
{
	//printf("orientation is: %d\n",orientation);

/*
  1        2       3      4         5            6           7          8
888888  888888      88  88      8888888888  88                  88  8888888888
88          88      88  88      88  88      88  88          88  88      88  88
8888      8888    8888  8888    88          8888888888  8888888888          88
88          88      88  88
88          88  888888  888888
1 = no rotation
2 = flip h
3 = rotate 180
4 = flip v
5 = flip v, rotate 90
6 = rotate 90
7 = flip v, rotate 270
8 = rotae 270 


*/
	//get rotaiton
	
	GdkPixbuf * modified = NULL;

	switch (orientation)
	{
		case 1:
			//1 = no rotation
			break;
		case 2:
			//2 = flip h
			modified = gdk_pixbuf_flip(pixbuf,TRUE);
			break;
		case 3:
			//3 = rotate 180
			modified = gdk_pixbuf_rotate_simple(pixbuf,(GdkPixbufRotation)180);
			break;
		case 4:
			//4 = flip v
			modified = gdk_pixbuf_flip(pixbuf,FALSE);
			break;
		case 5:
			//5 = flip v, rotate 90
			{
				GdkPixbuf *tmp = gdk_pixbuf_flip(pixbuf,FALSE);
				modified = gdk_pixbuf_rotate_simple(tmp,GDK_PIXBUF_ROTATE_CLOCKWISE);
				g_object_unref(tmp);
			}
			break;
		case 6:
			modified = gdk_pixbuf_rotate_simple(pixbuf,GDK_PIXBUF_ROTATE_CLOCKWISE);
			break;
		case 7:
			//7 = flip v, rotate 270
			{
				GdkPixbuf *tmp = gdk_pixbuf_flip(pixbuf,FALSE);
				modified = gdk_pixbuf_rotate_simple(tmp,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
				g_object_unref(tmp);
			}
			break;
		case 8:
			//8 = rotae 270 
			modified = gdk_pixbuf_rotate_simple(pixbuf,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
		default:
			break;
	}

	return modified;
}

GdkPixbuf* GetFancyIcon(QuiverFile f)
{
	
	// bgcolor 
	GtkStyle* style =   gtk_widget_get_style(icon_view);
	GdkColor bgcolor = style->base[GTK_STATE_NORMAL];

	/*
	typedef struct {
  guint32 pixel;
  guint16 red;
  guint16 green;
  guint16 blue;
} GdkColor;
*/

	//printf("colors: %d %d %d : %d \n",bgcolor.red/256,bgcolor.green/256,bgcolor.blue/256,bgcolor.pixel);
	//printf("getting pixbuf\n");
	GdkPixbuf *pixbuf = f.GetThumbnail();
	//printf("got it\n");
	if (NULL == pixbuf)
		return NULL;
	
	//printf("not null\n");
	GdkPixbuf *newpixbuf = GdkPixbufExifReorientate(pixbuf,f.GetExifOrientation());
	if (NULL != newpixbuf)
	{
		//printf("reorientated\n");
		g_object_unref(pixbuf);
		pixbuf = newpixbuf;
	}
	
	//printf("normal stuff\n");

	int icon_width = 136;
	int icon_height = 136;

	int thumb_size = 128;

	guchar *buffer;
	
	
	ArtVpath *vec = NULL;
	
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
		GdkPixbuf* newpixbuf = gdk_pixbuf_scale_simple (pixbuf,
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
	

	
	vec = art_new (ArtVpath, 10);
	
	art_u32 black = (0x00 << 24) | (0x00 <<16) | (0x00<<8) | (0xFF) ; /* RRGGBBAA */
	//art_u32 white = (0xFF << 24) | (0xFF <<16) | (0xFF<<8) | (0xFF) ; /* RRGGBBAA */
	
	art_u32 shadow_color = (0x00 << 24) | (0x00 <<16) | (0x00<<8) | (0x15) ; /* RRGGBBAA */
	
	
	
	ArtVpath* circle_vec = art_vpath_new_circle(w/2, h/2, h/2 );
	//ArtSVP *circle = art_svp_from_vpath(circle_vec);
	ArtSVP *circle = art_svp_vpath_stroke (circle_vec,ART_PATH_STROKE_JOIN_MITER,ART_PATH_STROKE_CAP_BUTT,1,0,0);

  
	GdkPixbuf* draw_buf = gdk_pixbuf_new (
								gdk_pixbuf_get_colorspace(pixbuf),
											FALSE,//gdk_pixbuf_get_has_alpha(pixbuf),
                                             gdk_pixbuf_get_bits_per_sample(pixbuf),
                                             icon_width,
                                             icon_height);

	
	buffer = gdk_pixbuf_get_pixels(draw_buf);
	
	//calculate center
	double top_x,top_y;
	top_x = (int)(icon_width/2 - w/2) + .5;
	top_y = (int)(icon_height/2 - h/2) + .5;

	// must offset by .5 because libart tries to draw bits on either side of the point
	vec[0].code = ART_MOVETO;
	vec[0].x = top_x -1;
	vec[0].y = top_y -1;
	vec[1].code = ART_LINETO;
	vec[1].x = top_x -1;
	vec[1].y = top_y + h -1;
	vec[2].code = ART_LINETO;
	vec[2].x = top_x + w -1;
	vec[2].y = top_y + h -1;
	vec[3].code = ART_LINETO;
	vec[3].x = top_x +w -1;
	vec[3].y = top_y -1;
	vec[4].code = ART_LINETO;
	vec[4].x = top_x -1;
	vec[4].y = top_y -1;
	vec[5].code = ART_END;

	ArtSVP *inner_path = art_svp_vpath_stroke (vec,ART_PATH_STROKE_JOIN_BEVEL,ART_PATH_STROKE_CAP_BUTT,3,0,1);
	ArtSVP * outer_path = art_svp_vpath_stroke (vec,ART_PATH_STROKE_JOIN_BEVEL,ART_PATH_STROKE_CAP_BUTT,1,0,1);


	vec[0].code = ART_MOVETO;
	vec[0].x = top_x;
	vec[0].y = top_y;
	vec[1].code = ART_LINETO;
	vec[1].x = top_x;
	vec[1].y = top_y + h;
	vec[2].code = ART_LINETO;
	vec[2].x = top_x + w;
	vec[2].y = top_y + h;
	vec[3].code = ART_LINETO;
	vec[3].x = top_x +w;
	vec[3].y = top_y;
	vec[4].code = ART_LINETO;
	vec[4].x = top_x;
	vec[4].y = top_y;
	vec[5].code = ART_END;
	
	int dst_rowstride = gdk_pixbuf_get_rowstride(draw_buf);
	
  	//art_rgba_fill_run_with_alpha (buffer, 0xFF, 0xFF, 0xFF, 0x00, icon_width*icon_height);
	art_rgb_fill_run (buffer, bgcolor.red/256,bgcolor.green/256,bgcolor.blue/256, icon_width*icon_height);
	
	for (int i = 8;i >= 1; i-=2)
	{
		ArtSVP * shadow = art_svp_vpath_stroke (vec,ART_PATH_STROKE_JOIN_ROUND,ART_PATH_STROKE_CAP_BUTT,i,0,1);
		
		//gnome_print_art_rgba_svp_alpha (shadow, 0, 0, icon_width, icon_height, shadow_color, buffer, dst_rowstride, NULL);
		art_rgb_svp_alpha (shadow, 0, 0, icon_width, icon_height, shadow_color, buffer, dst_rowstride, NULL);

		art_free(shadow);
	}
	
	gdk_pixbuf_composite            (pixbuf,
                                             draw_buf,
                                             (int)top_x-1,
                                             (int)top_y-1,
                                             w,
                                             h,
                                             (int)top_x-1,
                                             (int)top_y-1,
                                             1,
                                             1,
                                             GDK_INTERP_NEAREST,
                                             255);
  //art_rgb_run_alpha (buffer, 0xFF, 0xFF, 0xFF, 0xFF, WIDTH*HEIGHT);
	//gnome_print_art_rgba_svp_alpha (inner_path, 0, 0, icon_width, icon_height, white, buffer, dst_rowstride, NULL);
	//gnome_print_art_rgba_svp_alpha (outer_path, 0, 0, icon_width, icon_height, black, buffer, dst_rowstride, NULL);
	art_rgb_svp_alpha (outer_path, 0, 0, icon_width, icon_height, black, buffer, dst_rowstride, NULL);
  //art_rgb_svp_alpha (circle, 0, 0, w, h, color, buffer, rowstride, NULL);

	art_free (vec);
	art_free (circle_vec);
	art_free(inner_path);
	art_free(outer_path);
	art_free(circle);
	
	g_object_unref(pixbuf);
	return draw_buf;

/*
  GdkPixbuf *pixbuf;

  pixbuf = gdk_pixbuf_new_from_data (buffer,
				     GDK_COLORSPACE_RGB, 
				     FALSE, 8,
				     WIDTH, HEIGHT,
				     ROWSTRIDE,
				     NULL, NULL);

  pixbuf_save_to_file (pixbuf, filename);
 
  gdk_pixbuf_unref (pixbuf);
  */
}

GdkPixbuf * GetStockIcon(QuiverFile f)
{
	
	int icon_width = 136;
	int icon_height = 136;

	GError *error = NULL;

/*
	GError *error;
	error = NULL;
	
	GtkSettings *settings = gtk_settings_get_default ();
	gchar *default_backend = NULL;
	g_object_get (settings, "gtk-file-chooser-backend", &default_backend, NULL);
	GtkFileSystem *file_system = _gtk_file_system_create (default_backend);
	GtkFilePath *path = gtk_file_system_uri_to_path (file_system,f.GetURI());

	return gtk_file_system_render_icon   (	file_system,
					    					path,
					    					NULL, // widget?
					    					140, // pixel size
					    					&error);
*/

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
											FALSE,//gdk_pixbuf_get_has_alpha(pixbuf),
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
	
	gdk_pixbuf_fill (draw_buf,bgcolor.red<<24 | bgcolor.green<<16 | bgcolor.blue<<8);

	
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
	gtk_icon_view_set_text_column   (GTK_ICON_VIEW(icon_view),0);
	gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW(icon_view),1);
	gtk_icon_view_set_selection_mode (GTK_ICON_VIEW(icon_view),GTK_SELECTION_MULTIPLE);
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

