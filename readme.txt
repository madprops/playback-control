This is meant to add playback options for finer control.

Right now it has an action to stop after the last queued song.

This can be found in the Services menu.

To compile, go to audacious-plugins and run this:

rm -rf build && meson setup build --prefix=/usr && cd build && ninja && sudo ninja install && cd ..

Then run the compiled audacious.

You might need to run this first: pkg-config --variable=plugin_dir audacious