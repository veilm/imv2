Summary of veilm fork changes:

- Adds a thumbnail grid mode, toggled with `Enter` by default
	- asynchronous thumbnail loading and caching
	- thumbnail navigation integrated with the normal image list and commands
- `loop_input = false` now clamps at the first/last image in your input set, instead of wrapping
	- e.g. running `:next 10` when you're at image 8/10 now brings you to 10/10. stock imv could not do this

to build the Wayland binary and install it to `/usr/local/bin/imv`:
```
git clone https://github.com/veilm/imv2
./imv2/install.sh
```

Original README:

---

[![builds.sr.ht status](https://builds.sr.ht/~exec64/imv.svg)](https://builds.sr.ht/~exec64/imv?)
imv - X11/Wayland Image Viewer
==============================

`imv` is a command line image viewer intended for use with tiling window managers.

[Project home](https://sr.ht/~exec64/imv/)

Features
--------

* Native Wayland and X11 support
* Support for dozens of image formats including:
  * PNG
  * JPEG
  * WebP
  * Animated GIFs
  * SVG
  * TIFF
  * Various RAW formats
  * Photoshop PSD files
* Configurable key bindings and behaviour
* Highly scriptable with IPC via imv-msg

Packages
--------

[![Packaging status](https://repology.org/badge/vertical-allrepos/imv.svg)](https://repology.org/project/imv/versions)

Example Usage
-------------

The following examples are a quick illustration of how you can use imv.
For detailed documentation see the man page.

    # Opening images
    imv image1.png another_image.jpeg a_directory

    # Opening a directory recursively
    imv -r Photos

    # Opening images via stdin
    find . -type f -name "*.svg" | imv

    # Open an image fullscreen
    imv -f image.jpeg

    # Viewing images in a random order
    find . -type f -name "*.png" | shuf | imv

    # Viewing images from stdin
    curl http://somesi.te/img.png | imv -

    # Viewing multiple images from the web
    curl -Osw '%{filename_effective}\n' 'http://www.example.com/[1-10].jpg' | imv

### Slideshow

imv can be used to display slideshows. You can set the number of seconds to
show each image for with the `-t` option at start up, or you can configure it
at runtime using the `t` and `T` hotkeys to increase and decrease the image
display time, respectively.

To cycle through a folder of pictures, showing each one for 10 seconds:

    imv -t 10 ~/Pictures/London

#### Custom configuration

imv's key bindings can be customised to trigger custom behaviour:

    [binds]

    # Delete and then close an open image by pressing 'X'
    <Shift+X> = exec rm "$imv_current_file"; close

    # Rotate the currently open image by 90 degrees by pressing 'R'
    <Shift+R> = exec mogrify -rotate 90 "$imv_current_file"

    # Use dmenu as a prompt for tagging the current image
    u = exec echo "$imv_current_file" >> ~/tags/$(ls ~/tags | dmenu -p "tag")

### Scripting

With the default bindings, imv can be used to select images in a pipeline by
using the `p` hotkey to print the current image's path to stdout. The `-l` flag
can also be used to tell imv to list the remaining paths on exit for a "open
set of images, close unwanted ones with `x`, then quit imv to pass the
remaining images through" workflow.

Key bindings can be customised to run arbitrary shell commands. Environment
variables are exported to expose imv's state to scripts run by it. These
scripts can in turn modify imv's behaviour by invoking `imv-msg` with
`$imv_pid`.

For example:

    #!/usr/bin/bash
    imv "$@" &
    imv_pid = $!

    while true; do
      # Some custom logic
      # ...

      # Close all open files
      imv-msg $imv_pid close all
      # Open some new files
      imv-msg $imv_pid open ~/new_path

      # Run another script against the currently open file
      imv-msg $imv_pid exec another-script.sh '$imv_current_file'
    done


Installation
------------

### Dependencies

| Library        |  Version |  Notes                                         |
|---------------:|:---------|------------------------------------------------|
| pthreads       |          | Required.                                      |
| xkbcommon      |          | Required.                                      |
| pangocairo     |          | Required.                                      |
| icu            |          | Required.                                      |
| xxd            |          | Optional. Required for testing.                |
| cmocka         |          | Optional. Required for testing.                |
| X11            |          | Optional. Required for X11 support.            |
| GL             |          | Optional. Required for X11 support.            |
| xcb            |          | Optional. Required for X11 support.            |
| xkbcommon-x11  |          | Optional. Required for X11 support.            |
| wayland-client |          | Optional. Required for Wayland support.        |
| wayland-egl    |          | Optional. Required for Wayland support.        |
| EGL            |          | Optional. Required for Wayland support.        |
| libtiff        |          | Optional. Provides TIFF support.               |
| libpng         |          | Optional. Provides PNG support.                |
| libjpeg-turbo  |          | Optional. Provides JPEG support.               |
| LittleCMS      | 2        | Optional. Provides CMYK support for JPEGs.     |
| librsvg        | 2.44     | Optional. Provides SVG support.                |
| libnsgif       | 1.0.0    | Optional. Provides animated GIF support.       |
| libnsbmp       |          | Optional. Provides BMP support.                |
| libheif        | 1.13.0   | Optional. Provides HEIF support.               |
| libjxl         |          | Optional. Provides JPEGXL support.             |
| libwebp        |          | Optional. Provides WebP supprt.                |
| qoi            |          | Optional. Provides QOI support.                |

Dependencies are determined by which backends and window systems are enabled
when building `imv`. You can find a summary of which backends are available
in [meson_options.txt](meson_options.txt)

    $ meson setup build/
    $ ninja -C build/
    # ninja -C build/ install

`--prefix` controls installation prefix.  If more control over installation
paths is required, `--bindir`, `--mandir` and `--datadir` are
available.  Eg. to install `imv` to home directory, run:

    $ meson setup --bindir=~/bin --prefix=~/.local

License
-------
`imv`'s source is published under the terms of the [MIT](LICENSE) license.
