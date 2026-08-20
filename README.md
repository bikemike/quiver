# Quiver

Quiver is an image viewer and photo manager for GNOME. It lets you view and
manage your photos and images, organize files into collections, adjust dates,
and manage bookmarks.

## Dependencies

### Core build dependencies

| Dependency        | Purpose                       | Debian/Ubuntu package          | Fedora package                  |
| ----------------- | ----------------------------- | ------------------------------ | ------------------------------- |
| CMake >= 3.10     | Build system                  | `cmake`                        | `cmake`                         |
| C/C++ compiler    | Compilation                   | `build-essential`              | `gcc gcc-c++`                   |
| pkg-config        | Locating libraries            | `pkg-config`                   | `pkgconf-pkg-config`            |
| GTK+ 3            | GUI toolkit                   | `libgtk-3-dev`                 | `gtk3-devel`                    |
| GLib / GIO        | Core library & I/O            | `libglib2.0-dev`               | `glib2-devel`                   |
| libexif           | EXIF metadata reading         | `libexif-dev`                  | `libexif-devel`                 |
| SQLite            | Database storage              | `libsqlite3-dev`               | `sqlite-devel`                  |
| GStreamer 1.0     | Video playback                | `libgstreamer1.0-dev`          | `gstreamer1-devel`              |
| GStreamer plugins | Base plugins (video, GL, app) | `libgstreamer-plugins-base1.0-dev` | `gstreamer1-plugins-base-devel` |
| libjpeg           | JPEG encode/decode            | `libjpeg-dev`                  | `libjpeg-turbo-devel`           |
| Boost            | Header-only utilities         | `libboost-dev`                 | `boost-devel`                   |

GStreamer pulls in the following modules, all required:

- `gstreamer-1.0`
- `gstreamer-plugins-base-1.0`
- `gstreamer-video-1.0`
- `gstreamer-gl-1.0`
- `gstreamer-app-1.0`

`glib-genmarshal` (shipped with the GLib development package) is also required
at build time.

### GStreamer video crop/zoom plugins

Video playback uses a `playbin` pipeline with a custom zoom/crop bin. At run
time GStreamer needs the following elements; the accelerated ones are optional
but recommended if you have matching hardware:

| Plugin                   | Element(s)                 | Purpose                          | Debian/Ubuntu package          | Fedora package             |
| ------------------------ | -------------------------- | -------------------------------- | ------------------------------ | -------------------------- |
| GStreamer base plugins   | `videoscale`, `videoconvert` | Software scaling/format convert | `gstreamer1.0-plugins-base`    | `gstreamer1-plugins-base`  |
| GStreamer good plugins   | `videocrop`                | Video cropping                  | `gstreamer1.0-plugins-good`    | `gstreamer1-plugins-good`  |
| GStreamer VAAPI          | `vavideoprocess`, `vaapipostproc` | Hardware-accelerated crop/zoom (Intel VAAPI) | `gstreamer1.0-vaapi` | `gstreamer1-vaapi` |
| NVIDIA GStreamer plugins | `nvvidconv`                | Hardware-accelerated crop/zoom (NVIDIA) | proprietary, from the NVIDIA GStreamer SDK / JetPack | n/a |
| Intel Media SDK plugin   | `vapostproc`               | Hardware-accelerated crop/zoom (Intel Media SDK) | not in standard repos | not in standard repos |

The software path (`videocrop` + `videoscale` + `videoconvert`) is always
available and is used automatically when no accelerated element is present.
The accelerated elements are probed at run time in this order: NVIDIA
(`nvvidconv`), Intel Media SDK (`vapostproc`), then VAAPI
(`vavideoprocess`/`vaapipostproc`).

### Test-only dependencies

Building and running the GUI integration tests requires:

| Dependency | Debian/Ubuntu package |
| ---------- | --------------------- |
| X11 headers | `libx11-dev` |
| XTest       | `libxtst-dev` |
| Xvfb        | `xvfb` |
| ImageMagick | `imagemagick` |
| Python 3    | `python3` |

## Building

Configure and build with CMake:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binary is produced at `build/src/quiver`.

### Configuration options

| Option            | Default                  | Description                         |
| ----------------- | ------------------------ | ----------------------------------- |
| `-DCMAKE_BUILD_TYPE` | `Release`            | Build type (e.g. `Debug`, `Release`) |
| `-DQUIVER_DATADIR` | `<source>/data`      | Directory containing `quiver.ui`    |
| `-DBUILD_TESTING` | `ON`                    | Build and register tests            |

By default the data directory is taken from the source tree, so the binary can
be run straight out of the build directory:

```sh
./build/src/quiver
```

To use an installed data directory instead:

```sh
cmake -B build -DQUIVER_DATADIR=/usr/share/quiver
```

## Testing

> [!NOTE]
> When testing the binary manually under Xvfb, you must force the GTK backend to X11 to prevent GTK3 from bypassing Xvfb and launching on your active Wayland session:
> ```sh
> GDK_BACKEND=x11 xvfb-run -a build/src/quiver
> ```

The test suite launches the binary under Xvfb and drives it with synthetic
input via XTest (`tests/scrollsim.c` → `quiver-inputsim`), asserting on
screenshots and process liveness:

```sh
ctest --test-dir build --output-on-failure -R gui_tests
```

The same harness can be run directly from the source tree (without CMake) with:

```sh
tests/run_gui_tests.sh src/quiver tests/quiver-inputsim tests build
```

Test media (used to populate the browsed folders) is generated by
`tests/gen_media.py`.

## License

See the `COPYING` file for license information.
