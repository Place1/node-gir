const { load } = require('../');

const GLib = load('GLib');
const Gtk = load('Gtk', '3.0');

describe('types', () => {
  // test('functions can receive IN arrays', () => {
  //   const data = [104, 101, 108, 108, 111];
  //   const result = GLib.base64Encode(data, data.length);
  //   expect(typeof(result)).toEqual('string');
  //   expect(result).toEqual(Buffer.from('hello').toString('base64'));
  // });

  // test('functions can return OUT arrays', () => {
  //   const filepath = __filename;
  //   const result = GLib.fileGetContents(filepath);
  //   expect(result.length).toBe(3);
  //   expect(result[0]).toBe(true);
  //   expect(result[1].length).toEqual(result[2]);
  // });

  test('functions can receive and return INOUT arrays', () => {
    const result = Gtk.init(['argument1', '--gtk-debug', 'misc', 'argument2']);
    expect(result.length).toEqual(2);
    expect(result[0]).toEqual('argument1');
    expect(result[1]).toEqual('argument2');
  });
});
