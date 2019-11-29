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

project (
  'wys',
  'c',
  version : '0.1.4',
  license : 'GPLv3+',
  meson_version : '>= 0.42.0',
  default_options :
    [
      'warning_level=1',
      'buildtype=debugoptimized',
      'c_std=gnu11'
    ],
)


libmchk_proj = subproject('libmachine-check')
libmchk_dep = libmchk_proj.get_variable('libmachine_check_dep')


app_name = meson.project_name()

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
config_data.set_quoted('APP_DATA_NAME', app_name)
config_data.set_quoted('DATADIR', full_datadir)
config_data.set_quoted('SYSCONFDIR', full_sysconfdir)

subdir('src')

install_subdir (
  'machine-conf',
  install_dir : join_paths(datadir, app_name)
)

install_subdir (
  'machine-check',
  install_dir : join_paths(datadir, 'machine-check', app_name),
  strip_directory : true
)
