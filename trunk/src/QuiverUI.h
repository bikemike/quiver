#ifndef FILE_QUIVER_UI_H
#define FILE_QUIVER_UI_H

#define QUIVER_ACTION_SLIDESHOW  "SlideShow"
#define QUIVER_ACTION_FULLSCREEN "FullScreen"

#define QUIVER_UI_MODE_BROWSER   1
#define QUIVER_UI_MODE_VIEWER    2


	
char * quiver_menubar =
"<ui> "
"	<menubar name='MenubarMain'>"
"		<menu action='MenuFile'>"
"			<menuitem action='FileOpen'/>"
"			<menuitem action='FileOpenFolder'/>"
"			<menuitem action='FileOpenLocation'/>"
"			<separator/>"
"			<menuitem action='FileSave'/>"
"			<separator/>"
"			<menuitem action='Quit'/>"

"			<placeholder action='FileMenuAdditions' />"
"		</menu>"
"		<menu action='MenuEdit'>"
"			<menuitem action='Cut'/>"
"			<menuitem action='Copy'/>"
"			<menuitem action='Paste'/>"
"			<separator/>"
"			<menuitem action='ImageTrash'/>"
"			<separator/>"
"			<menuitem action='Preferences'/>"
"		</menu>"
"		<menu action='MenuView'>"
"			<menuitem action='ViewMenubar'/>"
"			<menuitem action='ViewToolbarMain'/>"
"			<menuitem action='ViewStatusbar'/>"
"			<separator/>"
"			<menuitem action='FullScreen'/>"
"			<menuitem action='SlideShow'/>"
"			<separator/>"
"			<menu action='MenuZoom'>"
"		<menuitem action='ZoomFit'/>"
"		<menuitem action='Zoom100'/>"
"		<menuitem action='ZoomIn'/>"
"		<menuitem action='ZoomOut'/>"
"			</menu>"
"		</menu>"
"		<menu action='MenuImage'>"
"			<menuitem action='RotateCW'/>"
"			<menuitem action='RotateCCW'/>"
"			<separator/>"
"			<menuitem action='FlipH'/>"
"			<menuitem action='FlipV'/>"
"		</menu>"
"		<menu action='MenuGo'>"
"			<menuitem action='ImagePrevious'/>"
"			<menuitem action='ImageNext'/>"
"			<separator/>"
"			<menuitem action='ImageFirst'/>"
"			<menuitem action='ImageLast'/>"
"		</menu>"
"		<menu action='MenuBookmarks'>"
"			<menuitem action='BookmarksAdd'/>"
"			<menuitem action='BookmarksEdit'/>"
"		</menu>"
"		<menu action='MenuTools'>"
"		</menu>"
"		<menu action='MenuWindow'>"
"		</menu>"
"		<menu action='MenuHelp'>"
"			<menuitem action='About'/>"
"		</menu>"
"	</menubar>"
"</ui>";

char *quiver_toolbar_main =
"<ui>"
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='UIModeItems'>"
"		</placeholder>"
"		<placeholder name='NavToolItems'>"
"		</placeholder>"
"		<separator/>"
"		<toolitem action='FullScreen'/>"
"		<separator/>"
"		<toolitem action='SlideShow'/>"
"		<separator/>"
"		<toolitem action='ImageTrash'/>"
"		<separator/>"
"	</toolbar>"
"</ui>";


char *quiver_toolbar_browser =
"<ui>"
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='UIModeItems'>"
"			<separator/>"
"			<toolitem action='UIModeViewer'/>"
"			<separator/>"
"		</placeholder>"
"		<placeholder name='NavToolItems'>"
"			<separator/>"
"			<separator/>"
"		</placeholder>"
"	</toolbar>"
"</ui>";


char *quiver_toolbar_viewer =
"<ui>"
"	<toolbar name='ToolbarMain'>"
"		<placeholder name='UIModeItems'>"
"			<separator/>"
"			<toolitem action='UIModeBrowser'/>"
"			<separator/>"
"		</placeholder>"
"		<placeholder name='NavToolItems'>"
"			<separator/>"
"			<toolitem action='ImageFirst'/>"
"			<toolitem action='ImagePrevious'/>"
"			<toolitem action='ImageNext'/>"
"			<toolitem action='ImageLast'/>"
"			<separator/>"
"		</placeholder>"
"	</toolbar>"
"</ui>";


#endif
