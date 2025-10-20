project(
  'gif',
  'c',
  meson_version : '>= 1.3.0',
  version : '0.1',
  default_options : ['warning_level=3', 'c_std=c99', 'werror=true'],
)

srcs = ['src/gif.c']
incdir = include_directories('include')

lib = library(
  'gif',
  srcs,
  include_directories : incdir,
  install : true,
)

sdl3_dep = dependency('sdl3', default_options : ['werror=false'])

basic_sdl = executable(
  'basic',
  'examples/basic_sdl.c',
  include_directories : incdir,
  dependencies : [sdl3_dep],
  install : true,
  link_with : lib,
)

example_dir = meson.project_source_root() + '/examples'

foreach i : range(3)
  num = i + 1
  test(f'basic@num@', basic_sdl, args : [f'basic@num@.gif', '-t'], workdir : example_dir)
endforeach 