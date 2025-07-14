/*
#include <config.h>
#include "QuiverStockIcons.h"
#include <gtk/gtk.h>

#include "icons/quiver.xpm"
#include "icons/quiver_exec.xpm"

const gchar *QuiverStockIcons::icons[] = {
	QUIVER_STOCK_APP,
	QUIVER_STOCK_BROWSER,
	QUIVER_STOCK_VIEWER,
	QUIVER_STOCK_SLIDESHOW,
	QUIVER_STOCK_ZOOM_FIT,
	QUIVER_STOCK_ZOOM_100,
	QUIVER_STOCK_ZOOM_IN,
	QUIVER_STOCK_ZOOM_OUT,
	QUIVER_STOCK_ROTATE_CW,
	QUIVER_STOCK_ROTATE_CCW,
	QUIVER_STOCK_TRASH,
	QUIVER_STOCK_CUT,
	QUIVER_STOCK_COPY,
	QUIVER_STOCK_PASTE,
	QUIVER_STOCK_DELETE,
	QUIVER_STOCK_SAVE,
	QUIVER_STOCK_SAVE_AS,
	QUIVER_STOCK_PRINT,
	QUIVER_STOCK_PRINT_PREVIEW,
	QUIVER_STOCK_PROPERTIES,
	QUIVER_STOCK_PREFERENCES,
	QUIVER_STOCK_OPEN,
	QUIVER_STOCK_GO_BACK,
	QUIVER_STOCK_GO_FORWARD,
	QUIVER_STOCK_GOTO_FIRST,
	QUIVER_STOCK_GOTO_LAST,
	QUIVER_STOCK_RELOAD,
	QUIVER_STOCK_STOP,
	QUIVER_STOCK_BOOKMARKS,
	QUIVER_STOCK_FULLSCREEN,
	QUIVER_STOCK_LEAVE_FULLSCREEN,
	QUIVER_STOCK_ABOUT,
	QUIVER_STOCK_QUIT,
	QUIVER_STOCK_HELP,
	QUIVER_STOCK_UNDO,
	QUIVER_STOCK_REDO,
	QUIVER_STOCK_NEW_FOLDER,
	QUIVER_STOCK_ADJUST_DATE,
	QUIVER_STOCK_RENAME,
	QUIVER_STOCK_IMAGEINFO,
	QUIVER_STOCK_TOOLS,
	QUIVER_STOCK_SELECT_ALL,
	QUIVER_STOCK_DESELECT_ALL,
	QUIVER_STOCK_INVERT_SELECTION,
	QUIVER_STOCK_AUTOROTATE_JPEG,
	QUIVER_STOCK_CONVERT,
	QUIVER_STOCK_SLIDESHOW_SETTINGS,
	QUIVER_STOCK_TAG,
	QUIVER_STOCK_TAGS_EDIT,
	QUIVER_STOCK_TAGS_REMOVE,
	QUIVER_STOCK_SORT_ASC,
	QUIVER_STOCK_SORT_DESC,
	QUIVER_STOCK_RATING_0,
	QUIVER_STOCK_RATING_1,
	QUIVER_STOCK_RATING_2,
	QUIVER_STOCK_RATING_3,
	QUIVER_STOCK_RATING_4,
	QUIVER_STOCK_RATING_5,
	QUIVER_STOCK_FILTER,
	QUIVER_STOCK_FILTER_CLEAR,
	QUIVER_STOCK_LOCATION,
	QUIVER_STOCK_EXEC,
	QUIVER_STOCK_SCRIPT_CONSOLE,
	QUIVER_STOCK_SCRIPT_EDITOR,
	QUIVER_STOCK_SCRIPT_NEW,
	QUIVER_STOCK_SCRIPT_SAVE,
	QUIVER_STOCK_SCRIPT_SAVE_AS,
	QUIVER_STOCK_SCRIPT_OPEN,
	QUIVER_STOCK_SCRIPT_RUN,
	QUIVER_STOCK_SCRIPT_STOP,
	QUIVER_STOCK_SCRIPT_DEBUG_STEP_INTO,
	QUIVER_STOCK_SCRIPT_DEBUG_STEP_OVER,
	QUIVER_STOCK_SCRIPT_DEBUG_STEP_OUT,
	QUIVER_STOCK_SCRIPT_DEBUG_CONTINUE,
	QUIVER_STOCK_SCRIPT_DEBUG_VIEW_BREAKPOINTS,
	QUIVER_STOCK_SCRIPT_DEBUG_CLEAR_CONSOLE,
	QUIVER_STOCK_SCRIPT_DEBUG_STACK,
	QUIVER_STOCK_SCRIPT_DEBUG_LOCALS,
	QUIVER_STOCK_SCRIPT_DEBUG_GLOBALS,
	NULL
};


const char *QuiverStockIcons::pixmaps[][128] = {
	quiver_xpm,
	quiver_exec_xpm
};

const char *QuiverStockIcons::pixmap_ids[] = {
	"quiver-icon-app",
	"quiver-icon-exec"
};

void QuiverStockIcons::Load()
{
	GtkIconFactory* factory = gtk_icon_factory_new();
	
	//for (int i=0; icons[i] != NULL; i++)
	//{
	//	gtk_icon_factory_add_default(factory);
	//}

	for (unsigned int i=0; i < (sizeof(pixmaps)/sizeof(*pixmaps)); i++)
	{
		GtkIconSource* source = gtk_icon_source_new();
		gtk_icon_source_set_icon_name(source, icons[i]);
		//gtk_icon_source_set_pixbuf(source,pixmaps[i]);
		GtkIconSet* icon_set;
		icon_set = gtk_icon_set_new ();
		gtk_icon_set_add_source(icon_set, source);
		//gtk_icon_set_add_source(icon_set,source);
		gtk_icon_factory_add(factory,icons[i],icon_set);
		//gtk_icon_factory_add(factory,pixmap_ids[i],icon_set);
		gtk_icon_set_unref(icon_set);
		gtk_icon_source_free(source);
	}

	gtk_icon_factory_add_default (factory);
}
*/
// Entire file content commented out for GTK4 migration due to GtkIconFactory and GtkStock dependencies.
// TODO: Migrate to GtkIconTheme, GdkPixbuf, or named icons.
