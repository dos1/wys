/*
 * Copyright (C) 2018, 2019 Purism SPC
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

#ifndef WYS_AUDIO_H__
#define WYS_AUDIO_H__

#include "wys-direction.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define WYS_TYPE_AUDIO (wys_audio_get_type ())

G_DECLARE_FINAL_TYPE (WysAudio, wys_audio, WYS, AUDIO, GObject);

WysAudio *wys_audio_new                (const gchar  *codec,
                                        const gchar  *modem);
void      wys_audio_ensure_loopback    (WysAudio     *self,
                                        WysDirection  direction);
void      wys_audio_ensure_no_loopback (WysAudio     *self,
                                        WysDirection  direction);

G_END_DECLS

#endif /* WYS_AUDIO_H__ */
