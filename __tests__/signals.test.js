const { Gtk } = require('../');
const Gdk = require('../').load('Gdk');

describe('signals', () => {
  test('connect() returns a number', () => {
    const button = new Gtk.Button();
    const result = button.connect('clicked', () => undefined);
    expect(typeof (result)).toEqual('number');
  });

  test('disconnect() will stop the connected callback from being called', (done) => {
    const button = new Gtk.Button();
    const connection = button.connect('clicked', () => {
      done('the signal callback shouldn\'t have been called!');
    });
    button.disconnect(connection);
    button.clicked();
    setTimeout(() => done(), 0);
  });

  test('should be called when emitted', (done) => {
    // this test will timeout if the signal handler is never called
    const button = new Gtk.Button();
    button.connect('clicked', () => {
      done();
    });
    button.clicked();
  });

  test('a signal that passes parameters to it\'s callback should work', (done) => {
    const window = new Gtk.Window({ type: Gtk.WindowType.TOPLEVEL, title: 'signal-test-window' });
    const button = new Gtk.Button();
    button.connect('size-allocate', (self, allocation) => {
      expect(allocation).toHaveProperty('width');
      expect(allocation).toHaveProperty('height');
      expect(allocation).toHaveProperty('x');
      expect(allocation).toHaveProperty('y');
      done();
    });
    window.add(button);
    window.showAll(); // this triggers the 'size-allocate' signal!
  });

  test('signals that pass the instance to the closure should be the same object (i.e. ===)', (done) => {
    const entry = new Gtk.Entry();
    entry.connect('changed', (instance) => {
      expect(instance === entry).toBe(true);
      done();
    });
    entry.setText('dummy text');
  });

  test('signals for property change notifications should work', (done) => {
    // these signals are a little bit interesting because they sometimes (maybe always?)
    // don't have GICallableInfo associated with the signal. That's why we're testing them
    // explicitly.
    const button = new Gtk.ColorButton()
    button.connect('notify::color', () => {
      done();
    });
    button.setColor(new Gdk.Color({ red: 0, green: 0, blue: 0 }));
  });
});
