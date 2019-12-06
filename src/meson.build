#
# Copyright (C) 2019 Purism SPC
#
# This file is part of Wys.
#
# Wys is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# Wys is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
# License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Wys.  If not, see <http://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

gnome = import('gnome')

wys_deps = [
  libmchk_dep,
  dependency('gobject-2.0'),
  dependency('gio-unix-2.0'),
  dependency('ModemManager'),
  dependency('mm-glib'),
  dependency('libpulse'),
  dependency('libpulse-mainloop-glib'),
]

config_h = configure_file (
  output: 'config.h',
  configuration: config_data
)

wys_enum_headers = files(['wys-direction.h'])
wys_enum_sources = gnome.mkenums_simple('enum-types',
                                        sources : wys_enum_headers)

executable (
  'wys',
  config_h,
  wys_enum_sources,
  [
    'main.c',
    'util.h', 'util.c',
    'wys-direction.h', 'wys-direction.c',
    'wys-modem.h', 'wys-modem.c',
    'wys-audio.h', 'wys-audio.c',
  ],
  dependencies : wys_deps,
  include_directories : include_directories('..'),
  install : true
)
