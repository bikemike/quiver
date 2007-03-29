if MAEMO
desktopdir = ${datadir}/applications/hildon
servicedir = ${datadir}/dbus-1/services
service_DATA = org.yi.mike.quiver.service
icons_26x26dir = ${datadir}/icons/hicolor/26x26/hildon
icons_26x26_DATA = icons/26x26/quiver-icon-app.png
else
desktopdir = ${datadir}/applications
endif

desktop_DATA = quiver.desktop


gladedir = ${datadir}/quiver/glade
glade_DATA = quiver.glade

EXTRA_DIST = ${desktop_DATA} ${glade_DATA} ${service_DATA} ${icons_26x26_DATA}
