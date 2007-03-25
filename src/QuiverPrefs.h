#include "Preferences.h"

// global preferences
#define QUIVER_PREFS_APP                       "application"
#define QUIVER_PREFS_APP_PROPS_SHOW            "properties_show"
#define QUIVER_PREFS_APP_HPANE_POS             "hpane_position"
#define QUIVER_PREFS_APP_TOP                   "top"
#define QUIVER_PREFS_APP_LEFT                  "left"
#define QUIVER_PREFS_APP_WIDTH                 "width"
#define QUIVER_PREFS_APP_HEIGHT                "height"
#define QUIVER_PREFS_APP_USE_THEME_COLOR       "use_theme_color"
#define QUIVER_PREFS_APP_BG_IMAGEVIEW          "bgcolor_imageview"
#define QUIVER_PREFS_APP_BG_ICONVIEW           "bgcolor_iconview"

// browser preferences
#define QUIVER_PREFS_BROWSER                   "browser"
#define QUIVER_PREFS_BROWSER_THUMB_SIZE        "thumb_size"
#define QUIVER_PREFS_BROWSER_FOLDER_HPANE      "folder_hpane"
#define QUIVER_PREFS_BROWSER_FOLDER_VPANE      "folder_vpane"
#define QUIVER_PREFS_BROWSER_PREVIEW_SHOW      "preview_show"


// viewer preferences
#define QUIVER_PREFS_VIEWER                    "viewer"
#define QUIVER_PREFS_VIEWER_FILMSTRIP_SHOW     "filmstrip_show"
#define QUIVER_PREFS_VIEWER_FILMSTRIP_POSITION "filmstrip_position"
#define QUIVER_PREFS_VIEWER_FILMSTRIP_SIZE     "filmstrip_size"

// slideshow preferences
#define QUIVER_PREFS_SLIDESHOW                 "slideshow"
#define QUIVER_PREFS_SLIDESHOW_DURATION        "duration"
#define QUIVER_PREFS_SLIDESHOW_LOOP            "loop"


enum {
	FSTRIP_POS_TOP,
	FSTRIP_POS_LEFT,
	FSTRIP_POS_BOTTOM,
	FSTRIP_POS_RIGHT,
};
