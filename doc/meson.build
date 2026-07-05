# The man page is a template: @pkgdatadir@/@defaultmenu@/@version@ are substituted
# at configure time so the installed page names real on-disk paths. Mirrors the
# classic doc/Makefile.am sed rules; pkgdatadir matches src/meson.build's
# BBAI_DEFAULT_* so the page and the binary agree on where things live.
man_cfg = configuration_data()
pkgdatadir = get_option('prefix') / get_option('datadir') / 'blackboxai'
man_cfg.set('pkgdatadir',  pkgdatadir)
man_cfg.set('defaultmenu', pkgdatadir / 'menu')
man_cfg.set('version',     meson.project_version())

blackboxai_man = configure_file(
  input         : 'blackboxai.1.in',
  output        : 'blackboxai.1',
  configuration : man_cfg)

install_man(blackboxai_man)
