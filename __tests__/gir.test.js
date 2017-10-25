var Gir = require('../gir');

var Gtk = Gir.load('Gtk', '3.0');
Gtk.init(0);

var GLib = Gir.load('GLib');
var GObject = Gir.load('GObject');
var GdkPixbuf = Gir.load('GdkPixbuf');

exports.GdkPixbuf = GdkPixbuf;
exports.GLib = GLib;
exports.GObject = GObject;
exports.Gtk = Gtk;

