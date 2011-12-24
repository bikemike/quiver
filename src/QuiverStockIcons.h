#ifndef FILE_QUIVER_STOCK_ICONS_H
#define FILE_QUIVER_STOCK_ICONS_H


#define QUIVER_STOCK_APP            GTK_STOCK_FILE //"quiver-icon-app"
#define QUIVER_STOCK_BROWSER        GTK_STOCK_DIRECTORY //"quiver-icon-browser"
#define QUIVER_STOCK_ROTATE_CW      GTK_STOCK_REDO //"quiver-icon-rotate-cw"
#define QUIVER_STOCK_ROTATE_CCW     GTK_STOCK_UNDO //"quiver-icon-rotate-ccw"
#define QUIVER_STOCK_SLIDESHOW      GTK_STOCK_INDEX //"quiver-icon-slideshow"

#define QUIVER_STOCK_ABOUT           GTK_STOCK_ABOUT


#define QUIVER_STOCK_OK              GTK_STOCK_OK
#define QUIVER_STOCK_CANCEL          GTK_STOCK_CANCEL


#define QUIVER_STOCK_OPEN            GTK_STOCK_OPEN
#define QUIVER_STOCK_SAVE            GTK_STOCK_SAVE
#define QUIVER_STOCK_QUIT            GTK_STOCK_QUIT
#define QUIVER_STOCK_CLOSE           GTK_STOCK_CLOSE

#define QUIVER_STOCK_EDIT            GTK_STOCK_EDIT

#define QUIVER_STOCK_PROPERTIES      GTK_STOCK_PROPERTIES
#define QUIVER_STOCK_PREFERENCES     GTK_STOCK_PREFERENCES
#define QUIVER_STOCK_REFRESH         GTK_STOCK_REFRESH

#define QUIVER_STOCK_ADD             GTK_STOCK_ADD
#define QUIVER_STOCK_REMOVE          GTK_STOCK_REMOVE
#define QUIVER_STOCK_EXECUTE         GTK_STOCK_EXECUTE

#ifdef QUIVER_MAEMO
#define QUIVER_STOCK_GO_UP           "qgn_toolb_gene_up"
#define QUIVER_STOCK_GO_BACK         "qgn_toolb_gene_back"
#define QUIVER_STOCK_GO_FORWARD      "qgn_toolb_gene_forward"
#define QUIVER_STOCK_DELETE          "qgn_toolb_gene_deletebutton"
#define QUIVER_STOCK_FULLSCREEN      "qgn_list_hw_button_view_toggle"
#define QUIVER_STOCK_DIRECTORY       "qgn_list_filesys_common_fldr"

#ifdef HAVE_HILDON_1 // hack for OS2008 detection
#define QUIVER_STOCK_ZOOM_IN         "qgn_toolb_gene_zoomin"
#define QUIVER_STOCK_ZOOM_OUT        "qgn_toolb_gene_zoomout"
#else
#define QUIVER_STOCK_ZOOM_IN         GTK_STOCK_ZOOM_IN
#define QUIVER_STOCK_ZOOM_OUT        GTK_STOCK_ZOOM_OUT
#endif
/*
"qgn_toolb_gene_bookmark_add"
"qgn_toolb_gene_bookmark"
*/
#define QUIVER_STOCK_CUT             "qgn_list_gene_cut"
#define QUIVER_STOCK_COPY            "qgn_list_gene_copy"
#define QUIVER_STOCK_PASTE           "qgn_list_gene_paste"
#else // not maemo
#define QUIVER_STOCK_DELETE          GTK_STOCK_DELETE
#define QUIVER_STOCK_GO_UP           GTK_STOCK_GO_UP
#define QUIVER_STOCK_GO_BACK         GTK_STOCK_GO_BACK
#define QUIVER_STOCK_GO_FORWARD      GTK_STOCK_GO_FORWARD
#define QUIVER_STOCK_ZOOM_IN         GTK_STOCK_ZOOM_IN
#define QUIVER_STOCK_ZOOM_OUT        GTK_STOCK_ZOOM_OUT
#if GTK_MAJOR_VERSION > 2 || GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION >= 8
#define QUIVER_STOCK_FULLSCREEN      GTK_STOCK_FULLSCREEN
#else
#define QUIVER_STOCK_FULLSCREEN      QUIVER_STOCK_GOTO_TOP
#endif
#define QUIVER_STOCK_DIRECTORY       GTK_STOCK_DIRECTORY
#define QUIVER_STOCK_CUT             GTK_STOCK_CUT
#define QUIVER_STOCK_COPY            GTK_STOCK_COPY
#define QUIVER_STOCK_PASTE           GTK_STOCK_PASTE

#endif 

#define QUIVER_STOCK_GOTO_TOP        GTK_STOCK_GOTO_TOP
#define QUIVER_STOCK_GOTO_FIRST      GTK_STOCK_GOTO_FIRST
#define QUIVER_STOCK_GOTO_LAST       GTK_STOCK_GOTO_LAST

#define QUIVER_STOCK_ZOOM_100        GTK_STOCK_ZOOM_100
#define QUIVER_STOCK_ZOOM_FIT        GTK_STOCK_ZOOM_FIT

#define QUIVER_STOCK_SORT_DESCENDING GTK_STOCK_SORT_DESCENDING






#ifdef QUIVER_MAEMO

#define ICON_MAEMO_DEVICE        "qgn_list_filesys_divc_cls"
#define ICON_MAEMO_AUDIO         "qgn_list_filesys_audio_fldr"
#define ICON_MAEMO_DOCUMENTS     "qgn_list_filesys_doc_fldr"
#define ICON_MAEMO_GAMES         "qgn_list_filesys_games_fldr"
#define ICON_MAEMO_IMAGES        "qgn_list_filesys_image_fldr"
#define ICON_MAEMO_VIDEOS        "qgn_list_filesys_video_fldr"
#define ICON_MAEMO_MMC           "qgn_list_filesys_mmc_root"
#define ICON_MAEMO_MMC_INTERNAL  "qgn_list_gene_internal_memory_card"
#define ICON_MAEMO_MMC_REMOVABLE "qgn_list_gene_removable_memory_card"

// folder expanders
#define ICON_MAEMO_EXPANDED      "qgn_list_gene_fldr_exp"
#define ICON_MAEMO_COLLAPSED     "qgn_list_gene_fldr_clp"

// folder open / closed
#define ICON_MAEMO_FOLDER_CLOSED "qgn_list_gene_fldr_cls"
#define ICON_MAEMO_FOLDER_OPEN   "qgn_list_gene_fldr_opn"

#define MAEMO_EXPANDED                       "_exp"
#define MAEMO_COLLAPSED                      "_clp"

#endif

class QuiverStockIcons
{
public:
	static void Load();		
};

#endif

