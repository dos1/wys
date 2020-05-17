/*
 * Copyright (C) 2019 Purism SPC
 *
 * This file is part of Wys.
 *
 * Wys is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wys is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wys.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Bob Ham <bob.ham@puri.sm>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef WYS_UTIL_H__
#define WYS_UTIL_H__

#include <glib.h>

#define APPLICATION_NAME "Wys"
#define APPLICATION_ID   "sm.puri.Wys"

G_BEGIN_DECLS

void wys_error (const gchar *format, ...);

G_END_DECLS

#endif /* WYS_UTIL_H__ */
