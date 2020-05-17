#
# Copyright (C) 2019 Purism SPC
#
# This file is part of libmachine-check.
#
# libmachine-check is free software: you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# libmachine-check is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with libmachine-check.  If not, see
# <http://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

project (
  'libmachine-check',
  'c',
  version: '0.1.0',
  license: 'GPLv3+',
  default_options:
    [
      'warning_level=1',
      'buildtype=debugoptimized',
      'c_std=gnu11'
    ],
)

prefix = get_option('prefix')
datadir = get_option('datadir')
sysconfdir = get_option('sysconfdir')

if datadir.startswith('/')
  full_datadir = datadir
else
  full_datadir = join_paths(prefix, datadir)
endif

if sysconfdir.startswith('/')
  full_sysconfdir = sysconfdir
else
  full_sysconfdir = join_paths(prefix, sysconfdir)
endif

config_data = configuration_data()
config_data.set_quoted('DATADIR', full_datadir)
config_data.set_quoted('SYSCONFDIR', full_sysconfdir)

config_h = configure_file (
  output: 'config.h',
  configuration: config_data
)

libmachine_check_deps = [
  dependency('gio-unix-2.0'),
]

libmachine_check_inc = include_directories('.')

libmachine_check = static_library (
  'machine-check',
  config_h,
  [
    'mchk-machine-check.h', 'mchk-machine-check.c',
  ],
  dependencies : libmachine_check_deps,
  include_directories : libmachine_check_inc,
)

libmachine_check_dep = declare_dependency (
  dependencies: libmachine_check_deps,
  link_with: libmachine_check,
  include_directories: libmachine_check_inc,
)
