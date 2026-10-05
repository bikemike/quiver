# Quiver

Quiver is an image viewer and photo manager for GNOME. It lets you view and
manage your photos and images, organize files into collections, adjust dates,
and manage bookmarks.

## Dependencies

### Core build dependencies

| Dependency        | Purpose                       | Debian/Ubuntu package          | Fedora package                  |
| ----------------- | ----------------------------- | ------------------------------ | ------------------------------- |
| CMake >= 3.10     | Build system                  | `cmake`                        | `cmake`                         |
| C++20 compiler    | Compilation                   | `build-essential`              | `gcc gcc-c++`                   |
| pkg-config        | Locating libraries            | `pkg-config`                   | `pkgconf-pkg-config`            |
| GTK 4            | GUI toolkit                   | `libgtk-4-dev`                 | `gtk4-devel`                    |
| GLib / GIO        | Core library & I/O            | `libglib2.0-dev`               | `glib2-devel`                   |
| libglycin         | Sandboxed image decoding      | `libglycin-2-dev`, `glycin-loaders` | `glycin-devel`, `glycin-loaders` |
| libexiv2          | EXIF metadata editing         | `libexiv2-dev`                 | `exiv2-devel`                   |
| FFmpeg            | Video metadata & processing   | `libavformat-dev libavcodec-dev libavutil-dev libswscale-dev` | `ffmpeg-devel` |
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
| `-DENABLE_GLYCIN` | `ON`                     | Enable libglycin backend for image loading |
| `-DENABLE_GDK_PIXBUF` | `OFF`                | Enable GdkPixbuf loader backend for image loading |
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

## External Tools

Quiver allows you to integrate custom external tools, commands, and scripts to process your images and videos or integrate with desktop utilities.

Access the configuration via **Tools → External Tools...** in the main menu.

### Command Line Tokens

When configuring a command line for an external tool, the following tokens are substituted:

| Token | Description | Example Replacement |
| :--- | :--- | :--- |
| `%f` | Absolute path(s) to the selected file(s) | `"/home/user/Pictures/photo.jpg"` |
| `%d` | Directory path(s) of the selected file(s) | `"/home/user/Pictures"` |

- **Command supports multiple files**:
  - When checked, all selected files are passed into a single execution (e.g. `my_script "%f"` expands to `my_script "photo1.jpg" "photo2.jpg"`).
  - When unchecked, the command is executed once for each selected file in sequence.

### Task Manager & Background Execution

- **Only show in Task Manager on error**:
  Check this option for fast launchers or desktop commands (e.g. `nautilus "%d"`, `gimp "%f"`, or clipboard scripts).
  - On clean completion (exit code 0), the command runs silently in the background without popping up the Task Manager dialog or leaving completed items.
  - If the command fails (non-zero exit code or spawn error), the Task Manager opens automatically with the error details and stderr expanded.
- **Task Manager "Clear Finished"**:
  When managing multiple tasks, click **Clear Finished** in the Task Manager header bar to dismiss all completed tasks in one click.

### Keyboard Shortcuts

You can assign custom hotkeys to any external tool:
1. In the External Tool editor, click **Set Shortcut...**.
2. Press the desired key combination (e.g. `<Control><Alt>O`, `F4`). Quiver automatically checks for conflicts against built-in shortcuts.
3. The assigned accelerator will appear directly in the Tools menu and will activate the tool in both Browser and Viewer modes.
4. External tool shortcuts can also be viewed and managed in **Preferences → Shortcuts** under the "External Tools" category.

### Reporting Progress from Scripts

Long-running external tools (e.g. batch image converters, watermarkers, video encoders) can stream real-time progress, status descriptions, and logs back to the Quiver Task Manager dialog.

`ExternalToolTask` parses standard output line-by-line while executing. In addition, Quiver automatically sets `PYTHONUNBUFFERED=1` in the child environment so Python scripts stream immediately without pipe buffering.

#### Supported Progress Protocols

1. **Zenity Protocol (Classic Linux/GNOME shell scripts)**:
   - Print an integer `0` to `100` on a line to update the progress percentage:
     ```sh
     echo "45"
     ```
   - Print a line starting with `#` to update the task status message:
     ```sh
     echo "# Optimizing image 3 of 10..."
     ```
2. **Prefix Directives (Self-documenting format)**:
   - `PROGRESS: <0-100>`: Updates the progress fraction.
   - `STATUS: <message>`: Updates the status description text.
   - `PROGRESS: <0-100> | <message>`: Updates both fraction and message simultaneously.
3. **Generic Percentage Matching**:
   - Any line containing standard percentage patterns (e.g. `[ 45%]`, `45%`, `Progress: 45%`) emitted by tools like `ffmpeg`, `rsync`, or `curl`.
4. **Live Log Output**:
   - All standard output and standard error lines stream live into the expandable **Details** panel of the Task Manager.

#### Script Examples

##### Bash Script Example (`batch_resize.sh`):

```bash
#!/bin/bash
# External tool command in Quiver: /path/to/batch_resize.sh %f
# Supports multiple files: [x]

TOTAL=$#
CURRENT=0

echo "STATUS: Starting batch resize of $TOTAL items..."

for FILE in "$@"; do
    ((CURRENT++))
    PERCENT=$(( CURRENT * 100 / TOTAL ))
    
    # Update progress and status
    echo "PROGRESS: $PERCENT | Processing ($CURRENT/$TOTAL): $(basename "$FILE")"
    
    # Process the file (e.g. with ImageMagick)
    magick "$FILE" -resize 1920x1080\> "${FILE%.*}_resized.jpg"
    
    sleep 0.1
done

echo "STATUS: Finished processing $TOTAL items."
echo "PROGRESS: 100"
```

##### Python Script Example (`optimize_images.py`):

```python
#!/usr/bin/env python3
# External tool command in Quiver: python3 /path/to/optimize_images.py %f
# Supports multiple files: [x]

import sys
import time

files = sys.argv[1:]
total = len(files)

print(f"STATUS: Optimizing {total} files...", flush=True)

for i, filepath in enumerate(files, 1):
    pct = int((i / total) * 100)
    print(f"PROGRESS: {pct} | [{i}/{total}] Optimizing {filepath}...", flush=True)
    
    # Perform your processing here
    time.sleep(0.2)

print("STATUS: Optimization complete!", flush=True)
print("PROGRESS: 100", flush=True)
```

## Testing

> [!NOTE]
> When testing the binary manually under Xvfb, you must force the GTK backend to X11 to prevent GTK4 from bypassing Xvfb and launching on your active Wayland session:
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
