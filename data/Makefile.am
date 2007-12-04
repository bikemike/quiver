if MAEMO
desktopdir = ${datadir}/applications/hildon
desktop_in_files = quiver.maemo.desktop.in
desktop_DATA = $(desktop_in_files:.maemo.desktop.in=.desktop)

servicedir = ${datadir}/dbus-1/services
service_DATA = org.yi.mike.quiver.service

icons_24x24dir = ${datadir}/icons/hicolor/24x24/hildon
icons_26x26dir = ${datadir}/icons/hicolor/26x26/hildon
icons_48x48dir = ${datadir}/icons/hicolor/48x48/hildon
else
icons_24x24dir = ${datadir}/icons/hicolor/24x24/apps
icons_26x26dir = ${datadir}/icons/hicolor/26x26/apps
icons_48x48dir = ${datadir}/icons/hicolor/48x48/apps
desktopdir = ${datadir}/applications
desktop_in_files = quiver.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

endif

@INTLTOOL_DESKTOP_RULE@

icons_24x24_DATA = \
	icons/26x26/quiver-icon-app.png \
	icons/26x26/quiver-icon-browser.png \
	icons/26x26/quiver-icon-rotate-ccw.png \
	icons/26x26/quiver-icon-rotate-cw.png \
	icons/26x26/quiver-icon-slideshow.png 

icons_26x26_DATA = \
	icons/26x26/quiver-icon-app.png \
	icons/26x26/quiver-icon-browser.png \
	icons/26x26/quiver-icon-rotate-ccw.png \
	icons/26x26/quiver-icon-rotate-cw.png \
	icons/26x26/quiver-icon-slideshow.png 

icons_48x48_DATA = \
	icons/48x48/quiver-icon-app.png



glade_DATA = quiver.glade

EXTRA_DIST = \
	${desktop_DATA} \
	${glade_DATA} \
	${service_DATA} \
	${icons_24x24_DATA} \
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



