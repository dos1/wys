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


#include "wys-direction.h"

const gchar *
wys_direction_get_description (WysDirection direction)
{
  switch (direction)
    {
    case WYS_DIRECTION_FROM_NETWORK:
      return "from network";
    case WYS_DIRECTION_TO_NETWORK:
      return "to network";
    default:
      return NULL;
    }
}
