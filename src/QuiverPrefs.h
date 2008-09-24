#include     "Preferences.h"

// global preferences
#define QUIVER_PREFS_APP                           "application"
#define QUIVER_PREFS_APP_START_FULLSCREEN          "start_fullscreen"
#define QUIVER_PREFS_APP_PROPS_SHOW                "properties_show"
#define QUIVER_PREFS_APP_STATUSBAR_SHOW            "statusbar_show"
#define QUIVER_PREFS_APP_STATUSBAR_SHOW_FS         "statusbar_show_fs"
#define QUIVER_PREFS_APP_MENUBAR_SHOW              "menubar_show"
#define QUIVER_PREFS_APP_MENUBAR_SHOW_FS           "menubar_show_fs"
#define QUIVER_PREFS_APP_TOOLBAR_SHOW              "toolbar_show"
#define QUIVER_PREFS_APP_TOOLBAR_SHOW_FS           "toolbar_show_fs"
#define QUIVER_PREFS_APP_PHOTO_LIBRARY             "photo_library"
#define QUIVER_PREFS_APP_WINDOW_FULLSCREEN         "window_fullscreen"
#define QUIVER_PREFS_APP_SORT_BY                   "sort_by"
#define QUIVER_PREFS_APP_SORT_REVERSED             "sort_reversed"


#define QUIVER_PREFS_APP_HPANE_POS                 "hpane_position"
#define QUIVER_PREFS_APP_TOP                       "top"
#define QUIVER_PREFS_APP_LEFT                      "left"
#define QUIVER_PREFS_APP_WIDTH                     "width"
#define QUIVER_PREFS_APP_HEIGHT                    "height"
#define QUIVER_PREFS_APP_USE_THEME_COLOR           "use_theme_color"
#define QUIVER_PREFS_APP_BG_IMAGEVIEW              "bgcolor_imageview"
#define QUIVER_PREFS_APP_BG_ICONVIEW               "bgcolor_iconview"
#define QUIVER_PREFS_APP_PHOTO_LIBRARY             "photo_library"
#define QUIVER_PREFS_APP_ASK_BEFORE_DELETE         "ask_before_delete"

// browser preferences
#define QUIVER_PREFS_BROWSER                       "browser"
#define QUIVER_PREFS_BROWSER_THUMB_SIZE            "thumb_size"
#define QUIVER_PREFS_BROWSER_FOLDER_HPANE          "folder_hpane"
#define QUIVER_PREFS_BROWSER_FOLDER_VPANE          "folder_vpane"
#define QUIVER_PREFS_BROWSER_PREVIEW_SHOW          "preview_show"
#define QUIVER_PREFS_BROWSER_FOLDERTREE_SHOW       "foldertree_show"
#define QUIVER_PREFS_BROWSER_FOLDERTREE_HIDE_FS    "foldertree_hide_fs"


// viewer preferences
#define QUIVER_PREFS_VIEWER                        "viewer"
#define QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW         "filmstrip_show"
#define QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION     "filmstrip_position"
#define QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE         "filmstrip_size"
#define QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE      "default_view_mode"
#define QUIVER_PREFS_VIEWER_QUICK_PREVIEW          "quick_preview"
#define QUIVER_PREFS_VIEWER_GIF_ANIMATION          "gif_animation"
#define QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE        "hide_scrollbars"
#define QUIVER_PREFS_VIEWER_ROTATE_FOR_BEST_FIT    "rotate_for_best_fit"

// slideshow preferences
#define QUIVER_PREFS_SLIDESHOW                     "slideshow"
#define QUIVER_PREFS_SLIDESHOW_DURATION            "duration"
#define QUIVER_PREFS_SLIDESHOW_LOOP                "loop"
#define QUIVER_PREFS_SLIDESHOW_FULLSCREEN          "fullscreen"
#define QUIVER_PREFS_SLIDESHOW_TRANSITION          "transition"
#define QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE      "hide_film_strip"
#define QUIVER_PREFS_SLIDESHOW_ROTATE_FOR_BEST_FIT "rotate_for_best_fit"
#define QUIVER_PREFS_SLIDESHOW_RANDOM_ORDER        "random_order"





enum {
	FSTRIP_POS_TOP,
	FSTRIP_POS_LEFT,
	FSTRIP_POS_BOTTOM,
	FSTRIP_POS_RIGHT,
};
