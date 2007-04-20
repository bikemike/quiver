#include "Preferences.h"

// global preferences
#define QUIVER_PREFS_APP                       "application"
#define QUIVER_PREFS_APP_PROPS_SHOW            "properties_show"
#define QUIVER_PREFS_APP_STATUSBAR_HIDE        "hide_statusbar"
#define QUIVER_PREFS_APP_MENUBAR_HIDE          "hide_menubar"
#define QUIVER_PREFS_APP_TOOLBAR_HIDE          "hide_toolbar"
#define QUIVER_PREFS_APP_PHOTO_LIBRARY         "photo_library"



#define QUIVER_PREFS_APP_HPANE_POS             "hpane_position"
#define QUIVER_PREFS_APP_TOP                   "top"
#define QUIVER_PREFS_APP_LEFT                  "left"
#define QUIVER_PREFS_APP_WIDTH                 "width"
#define QUIVER_PREFS_APP_HEIGHT                "height"
#define QUIVER_PREFS_APP_USE_THEME_COLOR       "use_theme_color"
#define QUIVER_PREFS_APP_BG_IMAGEVIEW          "bgcolor_imageview"
#define QUIVER_PREFS_APP_BG_ICONVIEW           "bgcolor_iconview"
#define QUIVER_PREFS_APP_PHOTO_LIBRARY         "photo_library"
#define QUIVER_PREFS_APP_ASK_BEFORE_DELETE     "ask_before_delete"

// browser preferences
#define QUIVER_PREFS_BROWSER                   "browser"
#define QUIVER_PREFS_BROWSER_THUMB_SIZE        "thumb_size"
#define QUIVER_PREFS_BROWSER_FOLDER_HPANE      "folder_hpane"
#define QUIVER_PREFS_BROWSER_FOLDER_VPANE      "folder_vpane"
#define QUIVER_PREFS_BROWSER_PREVIEW_HIDE      "hide_preview"
#define QUIVER_PREFS_BROWSER_FOLDERTREE_HIDE   "hide_foldertree"


// viewer preferences
#define QUIVER_PREFS_VIEWER                    "viewer"
#define QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW     "filmstrip_show"
#define QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION "filmstrip_position"
#define QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE     "filmstrip_size"
#define QUIVER_PREFS_VIEWER_DEFAULT_VIEW_MODE  "default_view_mode"
#define QUIVER_PREFS_VIEWER_QUICK_PREVIEW      "quick_preview"
#define QUIVER_PREFS_VIEWER_GIF_ANIMATION      "gif_animation"
#define QUIVER_PREFS_VIEWER_SCROLLBARS_HIDE    "hide_scrollbars"

// slideshow preferences
#define QUIVER_PREFS_SLIDESHOW                 "slideshow"
#define QUIVER_PREFS_SLIDESHOW_DURATION        "duration"
#define QUIVER_PREFS_SLIDESHOW_LOOP            "loop"
#define QUIVER_PREFS_SLIDESHOW_FULLSCREEN      "fullscreen"
#define QUIVER_PREFS_SLIDESHOW_TRANSITION      "transition"
#define QUIVER_PREFS_SLIDESHOW_FILMSTRIP_HIDE  "hide_film_strip"





enum {
	FSTRIP_POS_TOP,
	FSTRIP_POS_LEFT,
	FSTRIP_POS_BOTTOM,
	FSTRIP_POS_RIGHT,
};
