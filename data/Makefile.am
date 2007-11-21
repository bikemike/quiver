if MAEMO
desktopdir = ${datadir}/applications/hildon
servicedir = ${datadir}/dbus-1/services
service_DATA = org.yi.mike.quiver.service

icons_26x26dir = ${datadir}/icons/hicolor/26x26/hildon
icons_48x48dir = ${datadir}/icons/hicolor/48x48/hildon
else
icons_26x26dir = ${datadir}/icons/hicolor/26x26/apps
icons_48x48dir = ${datadir}/icons/hicolor/48x48/apps
desktopdir = ${datadir}/applications

endif

icons_26x26_DATA = \
	icons/26x26/quiver-icon-app.png \
	icons/26x26/quiver-icon-browser.png \
	icons/26x26/quiver-icon-rotate-ccw.png \
	icons/26x26/quiver-icon-rotate-cw.png \
	icons/26x26/quiver-icon-slideshow.png 

icons_48x48_DATA = \
	icons/48x48/quiver-icon-app.png


desktop_DATA = quiver.desktop

glade_DATA = quiver.glade

EXTRA_DIST = \
	${desktop_DATA} \
	${glade_DATA} \
	${service_DATA} \
	${icons_26x26_DATA} \
	${icons_48x48_DATA}


# gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

# install-data-hook: update-icon-cache

# uninstall-hook: update-icon-cache

# update-icon-cache:
# @-if test -z "$(DESTDIR)"; then \
# echo "Updating Gtk icon cache."; \
# $(gtk_update_icon_cache); \
# else \
# echo "*** Icon cache not updated.  After install, run this:"; \
# echo "***   $(gtk_update_icon_cache)"; \
# fi



