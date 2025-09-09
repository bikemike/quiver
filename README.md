# quiver

A GTK-based image viewer.

## Building

To build quiver from source, you will need the following dependencies:

*   `automake`
*   `autoconf`
*   `libgtk-4-dev`
*   `libboost-dev`
*   `libjpeg-dev`
*   `libgstreamer1.0-dev`
*   `libgstreamer-plugins-base1.0-dev`
*   `libexif-dev`
*   `intltool`

On a Debian-based system, you can install them with the following command:

```bash
sudo apt-get install -y automake autoconf libgtk-4-dev libboost-dev libjpeg-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libexif-dev intltool
```

Once the dependencies are installed, you can build the project with the following commands:

```bash
autoreconf -vifs
intltoolize --force
./configure
make
```

The executable will be located at `src/quiver`.
