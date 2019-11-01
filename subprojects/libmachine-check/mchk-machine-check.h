/*
 * Copyright (C) 2019 Purism SPC
 *
 * This file is part of libmachine-check.
 *
 * libmachine-check is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * libmachine-check is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libmachine-check.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Bob Ham <bob.ham@puri.sm>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

gchar *  mchk_read_machine  (GError      **error);
gboolean mchk_check_machine (const gchar  *param,
                             const gchar  *machine,
                             gboolean     *passed,
                             GError      **error);

G_END_DECLS
