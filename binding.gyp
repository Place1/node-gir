{
    'targets': [
        {
            'target_name': 'girepository',
            'sources': [
                'src/main.cc',
                'src/util.cc',
                'src/namespace_loader.cc',
                'src/arguments.cc',
                'src/function.cc',
                'src/values.cc',
                'src/types/object.cc',
                'src/types/struct.cc',
                'src/types/function_type.cc',
                'src/types/enum.cc',
                'src/loop.cc',
                'src/signal_closure.cc'
            ],
            'conditions': [
                ['OS=="linux"',
                    {
                        'libraries': [
                            '<!@(pkg-config --libs glib-2.0 gobject-introspection-1.0)'
                        ],
                        'cflags': [
                            '<!@(pkg-config --cflags glib-2.0 gobject-introspection-1.0)',
                            '-D_FILE_OFFSET_BITS=64',
                            '-D_LARGEFILE_SOURCE'
                        ],
                        'ldflags': [
                            '<!@(pkg-config --libs glib-2.0 gobject-introspection-1.0)'
                        ]
                    }
                ]
            ],
            'include_dirs': [
                '<!(node -e "require(\'nan\')")'
            ],
            'cflags': [
                '-std=c++11',
                '-g'
            ],
            'cflags!': [
                '-fno-exceptions'
            ],
            'cflags_cc!': [
                '-fno-exceptions'
            ]
        }
    ]
}
