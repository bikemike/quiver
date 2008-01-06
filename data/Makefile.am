if MAEMO
desktopdir = ${datadir}/applications/hildon

servicedir = ${datadir}/dbus-1/services
service_DATA = org.yi.mike.quiver.service

icon_dir = hildon
icons_64x54dir = ${datadir}/icons/hicolor/scalable/${icon_dir}

icons_64x54_DATA = \
	icons/64x54/quiver-icon-app.png


desktop_src_files = quiver.desktop.in-maemo
quiver.desktop.in: quiver.desktop.in-maemo
	cp ${srcdir}/quiver.desktop.in-maemo quiver.desktop.in

else
desktopdir = ${datadir}/applications
icon_dir = apps
desktop_src_files = quiver.desktop.in-desktop
quiver.desktop.in: quiver.desktop.in-desktop
	cp ${srcdir}/quiver.desktop.in-desktop quiver.desktop.in
endif

desktop_in_files = quiver.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

icons_22x22dir = ${datadir}/icons/hicolor/22x22/${icon_dir}
icons_24x24dir = ${datadir}/icons/hicolor/24x24/${icon_dir}
icons_26x26dir = ${datadir}/icons/hicolor/26x26/${icon_dir}
icons_32x32dir = ${datadir}/icons/hicolor/32x32/${icon_dir}
icons_48x48dir = ${datadir}/icons/hicolor/48x48/${icon_dir}
icons_64x64dir = ${datadir}/icons/hicolor/64x64/${icon_dir}
icons_128x128dir = ${datadir}/icons/hicolor/128x128/${icon_dir}

icons_22x22_DATA = \
	icons/22x22/quiver-icon-app.png

icons_24x24_DATA = \
	icons/24x24/quiver-icon-app.png \
	icons/24x24/quiver-icon-browser.png \
	icons/24x24/quiver-icon-rotate-ccw.png \
	icons/24x24/quiver-icon-rotate-cw.png \
	icons/24x24/quiver-icon-slideshow.png 

icons_26x26_DATA = \
	icons/26x26/quiver-icon-app.png \
	icons/26x26/quiver-icon-browser.png \
	icons/26x26/quiver-icon-rotate-ccw.png \
	icons/26x26/quiver-icon-rotate-cw.png \
	icons/26x26/quiver-icon-slideshow.png 

icons_32x32_DATA = \
	icons/32x32/quiver-icon-app.png

icons_48x48_DATA = \
	icons/48x48/quiver-icon-app.png

icons_64x64_DATA = \
	icons/64x64/quiver-icon-app.png

icons_128x128_DATA = \
	icons/128x128/quiver-icon-app.png

glade_DATA = quiver.glade

EXTRA_DIST = \
	${desktop_DATA} \
	${glade_DATA} \
	${service_DATA} \
	${icons_22x22_DATA} \
	${icons_24x24_DATA} \
	${icons_26x26_DATA} \
	${icons_32x32_DATA} \
	${icons_48x48_DATA} \
	${icons_54x54_DATA} \
	${icons_64x54_DATA} \
	${icons_64x64_DATA} \
	${icons_128x128_DATA} \
	${desktop_src_files}


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



