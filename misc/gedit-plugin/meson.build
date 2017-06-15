plugin_dir = join_paths(devhelp_libdir, 'gedit', 'plugins')

install_data(
  'devhelp.py',
  install_dir: plugin_dir
)

plugin = 'devhelp.plugin'

i18n.merge_file(
  plugin,
  type: 'desktop',
  input: plugin + '.desktop.in',
  output: plugin,
  po_dir: po_dir,
  install: true,
  install_dir: plugin_dir
)
