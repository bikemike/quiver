icondir = $(datadir)/icons/hicolor/48x48/apps
icon_DATA = quiver-icon-app.png

XPM_IMAGES =	\
	nav_button.xpm 

PNG_IMAGES = \
	$(icon_DATA) \
	quiver-icon-browser.png \
	quiver-icon-slideshow.png \
	quiver-icon-rotate-ccw.png \
	quiver-icon-rotate-cw.png 

quiver_icons = \
	quiver_icon_app.pixdata \
	quiver_icon_browser.pixdata \
	quiver_icon_slideshow.pixdata \
	quiver_icon_rotate_cw.pixdata \
	quiver_icon_rotate_ccw.pixdata 


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


gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook: update-icon-cache

uninstall-hook: update-icon-cache

update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi


EXTRA_DIST = $(PNG_IMAGES) $(XPM_IMAGES) $(quiver_icons)

