XPM_IMAGES =	\
	nav_button.xpm 

quiver_icons = \
	quiver_icon_app.pixdata \
	quiver_icon_browser.pixdata \
	quiver_icon_slideshow.pixdata \
	quiver_icon_rotate_cw.pixdata \
	quiver_icon_rotate_ccw.pixdata 

icondir = $(datadir)/icons/hicolor/48x48/apps
icon_DATA = quiver-icon-app.png

all: $(quiver_icons)

quiver_icon_app.pixdata: quiver-icon-app.png
	$(GDK_PIXBUF_CSOURCE) --name=$(basename $@) $< > $(basename $@).pixdata

quiver_icon_browser.pixdata: quiver-icon-browser.png
	$(GDK_PIXBUF_CSOURCE) --name=$(basename $@) $< > $(basename $@).pixdata

quiver_icon_slideshow.pixdata: quiver-icon-slideshow.png
	$(GDK_PIXBUF_CSOURCE) --name=$(basename $@) $< > $(basename $@).pixdata

quiver_icon_rotate_ccw.pixdata: quiver-icon-rotate-ccw.png
	$(GDK_PIXBUF_CSOURCE) --name=$(basename $@) $< > $(basename $@).pixdata

quiver_icon_rotate_cw.pixdata: quiver-icon-rotate-cw.png
	$(GDK_PIXBUF_CSOURCE) --name=$(basename $@) $< > $(basename $@).pixdata

EXTRA_DIST = $(XPM_IMAGES) $(quiver_icons) $(icon_DATA)
