# Abandoned

I currently don't have enough time to properly develop this library. If you're keen to contribute in this space have a look at: https://github.com/romgrk/node-gtk

# node-gir

[![CircleCI](https://circleci.com/gh/Place1/node-gir/tree/master.svg?style=svg)](https://circleci.com/gh/Place1/node-gir/tree/master)
[![npm](https://img.shields.io/npm/v/node-gir.svg?style=flat-square)](https://www.npmjs.com/package/node-gir)
[![npm](https://img.shields.io/npm/dt/node-gir.svg?style=flat-square)](https://www.npmjs.com/package/node-gir)

Node-gir is Node.js bindings to [GObject Introspection](https://live.gnome.org/GObjectIntrospection/) making it possible to make automatic and dynamic calls to any library that has GI annotations installed. This includes most libraries from the [GNOME project](http://developer.gnome.org/).

This will make it possible to script a GNOME desktop system entirely from node much in the way it's done today with Seed, GJS or pygtk. It also allows using GNOME libraries in Node.js applications. With it you can also write the performance-intensive parts of your applications [in Vala](https://github.com/antono/vala-object) and call them from Node.js and other languages.

## Installation

You need GObject Introspection library to be installed. On a Debian-like system this would be handled by:

    $ sudo apt-get install libgirepository1.0-dev

On an arch based system:

    $ sudo pacman -S gobject-introspection

On macOS:

    $ brew install gobject-introspection

Then just install node-gir with:

    $ npm install node-gir

## Running tests

The tests load the `gtk3` library to use as a testing target. On a Debian-like system it's likely you already have `gtk3` installed, if not, it can be installed using:

    $ sudo apt-get install libgtk-3-dev

Or on macOS:

    $ brew install gtk+3

You can then run the tests with the following:

    $ npm test

## Linting code

[ClangFormat](https://clang.llvm.org/docs/ClangFormat.html) is used for linting the C/C++ code. On a Debian-like system you can install it by running the following:

    $ sudo apt-get install clang-format

You can then run the linter with the following:

    $ npm run lint

## Things which work

- Bindings for classes are generated
- Classes support inheritance
- Interface methods are inherited
- C structures are propagated as objects (fields are properties)
    - This is likely to be re-implemented though as it's very buggy currently
- Both methods and static method can be called
- functions can be called
    - `out` arguments are currently buggy.
- GError is propagated as generic exception
- Properties can be set/get
- Support for signals using `.connect('signal', callback)`
- Support for glib main loop.
    - the Node eventloop will be nested in the glib loop
    - we need to write documentation to detailing how this works

## Things which dont work (correct)

- Conversion between a v8 value and a GValue/GArgument is veeeery buggy (but everything needs it so most things are buggy)
- There is no good way to delete an object (memory management sucks at all)
- GError should be propagated as derived classes depending on GError domain
- Structs and boxed types are still being implemented
