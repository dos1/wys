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

#include <glib.h>

#include <stdarg.h>
#include <stdlib.h>

void
wys_error (const gchar *format, ...)
{
  va_list ap;

  va_start (ap, format);
  g_logv (NULL, G_LOG_LEVEL_CRITICAL, format, ap);
  va_end (ap);

  exit (EXIT_FAILURE);
}
