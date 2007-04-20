#ifndef FILE_QUIVER_STOCK_ICONS_H
#define FILE_QUIVER_STOCK_ICONS_H


#define QUIVER_STOCK_APP        "quiver-icon_app"
#define QUIVER_STOCK_SLIDESHOW  "quiver-icon-slideshow"
#define QUIVER_STOCK_BROWSER    "quiver-icon-browser"
#define QUIVER_STOCK_ROTATE_CW  "quiver-icon-rotate-cw"
#define QUIVER_STOCK_ROTATE_CCW "quiver-icon-rotate-ccw"

#ifdef QUIVER_MAEMO

#define ICON_MAEMO_DEVICE "qgn_list_filesys_divc_cls"
#define ICON_MAEMO_AUDIO "qgn_list_filesys_audio_fldr"
#define ICON_MAEMO_DOCUMENTS "qgn_list_filesys_doc_fldr"
#define ICON_MAEMO_GAMES "qgn_list_filesys_games_fldr"
#define ICON_MAEMO_IMAGES "qgn_list_filesys_image_fldr"
#define ICON_MAEMO_VIDEOS "qgn_list_filesys_video_fldr"
#define ICON_MAEMO_MMC "qgn_list_filesys_mmc_root"

// folder expanders
#define ICON_MAEMO_EXPANDED "qgn_list_gene_fldr_exp"
#define ICON_MAEMO_COLLAPSED "qgn_list_gene_fldr_clp"

// folder open / closed
#define ICON_MAEMO_FOLDER_CLOSED "qgn_list_gene_fldr_cls"
#define ICON_MAEMO_FOLDER_OPEN "qgn_list_gene_fldr_opn"

#define MAEMO_EXPANDED                       "_exp"
#define MAEMO_COLLAPSED                      "_clp"

#endif

class QuiverStockIcons
{
public:
	static void Load();		
};

#endif

