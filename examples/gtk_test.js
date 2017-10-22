var gtk = require("./gtk");
var axios = require('axios');

gtk.init(0);

var win = new gtk.Window({type: gtk.WindowType.toplevel, title:"Node.JS GTK Window"});
var button = new gtk.Button();

win.set_border_width(10);

button.set_label("hallo, welt!");

win.add(button);
win.show_all();

var w2 = button.get_parent_window();
console.log(w2);

// win.on("destroy", function() {
//     console.log("destroyed", arguments[0] instanceof gtk.Window);
//     gtk.main_quit();
// });
// button.on("clicked", function() {
//     console.log("click :)", arguments[0] instanceof gtk.Button, arguments[0] == button);
// });

console.log(win.set_property("name", "test"));
console.log(win.__get_property__("name"));

setTimeout(() => {
    console.log('set timeout resolved');
}, 1000);

const promise = new Promise(resolve => resolve('promise resolved'));
promise.then(result => console.log(result));

gtk.main();
