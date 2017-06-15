subdir('xml')

private_headers = [
  'dh-app.h',
  'dh-assistant.h',
  'dh-enum-types-app.h',
  'dh-error.h',
  'dh-language.h',
  'dh-parser.h',
  'dh-preferences.h',
  'dh-settings.h',
  'dh-util.h',
  'dh-window.h'
]

doc_path = join_paths(devhelp_datadir, 'gtk-doc', 'html')

gnome.gtkdoc(
  devhelp_name,
  main_xml: devhelp_name + '-docs.xml',
  src_dir: src_inc,
  dependencies: libdevhelp_dep,
  scan_args: [
    '--rebuild-types',
    '--ignore-headers=' + ' '.join(private_headers)
  ],
  gobject_typesfile: devhelp_name + '.types',
  fixxref_args: '--html-dir=' + doc_path,
  install: true,
  install_dir: join_paths(doc_path, libdevhelp_name)
)
