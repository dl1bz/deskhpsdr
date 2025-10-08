/* Copyright (C)
* 2023, 2024 - Christoph van Wüllen, DL1YCF
* 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   This source code has been forked and was adapted from piHPSDR by DL1YCF to deskHPSDR in October 2024
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

/*
 * This file contains data (tables) which describe the layout
 * e.g. of the VFO bar. The layout contains (x,y) coordinates of
 * the individual elements as well as font sizes.
 *
 * There can be more than one "layout", characterized by its size
 * request. So the program can choose the largest layout that
 * fits into the allocated area.
 *
 * What this should do is, that if the user increases the width of
 * the screen and the VFO bar, the program can automatically
 * switch to a larger font.
 */

#include <stdlib.h>

#include "appearance.h"

//
// When a VFO bar layout that fits is searched in this list,
// first mathing layout is taken,
// so the largest one come first and the smallest one last.
//
const VFO_BAR_LAYOUT vfo_layout_list[] = {
  //
  // A layout tailored for a screen 1280 px wide:
  // a Layout with dial digits of size 50, and a "LED" size 20
  // which requires a width of 875 and a height of 90
  //
  {
    .description = "VFO bar for 1280px windows",
    .width = 875,
    .height = 95,
    .size1 = 16,
    .size2 = 30,
    .size3 = 50,

    .vfo_a_x = -5,
    .vfo_a_y = 70,
    .vfo_b_x = -560,
    .vfo_b_y = 70,

    .mode_x = 5,
    .mode_y = 24,
    .agc_x = 250,
    .agc_y = 24,
    .nr_x = 380,
    .nr_y = 24,
    .nb_x = 430,
    .nb_y = 24,
    .anf_x = 470,
    .anf_y = 24,
    .snb_x = 520,
    .snb_y = 24,
    .div_x = 560,
    .div_y = 24,
    .eq_x = 610,
    .eq_y = 24,
    .cat_x = 677,
    .cat_y = 24,
    .base_x = 670,
    .base_y = 24,

    .cmpr_x = 380,
    .cmpr_y = 50,
    .ps_x = 520,
    .ps_y = 50,
    .dexp_x = 470,
    .dexp_y = 50,

    .vox_x = 380,
    .vox_y = 68,
    .dup_x = 470,
    .dup_y = 68,

    .mute_x = 510,
    .mute_y = 68,
    .tuned_x = 417,
    .tuned_y = 90,
    .preamp_x = 830,
    .preamp_y = 89,

    .mgain_x = 5,
    .mgain_y = 90,
    .lock_x = 80,
    .lock_y = 90,
    .zoom_x = 140,
    .zoom_y = 90,
    .ctun_x = 200,
    .ctun_y = 90,
    .step_x = 250,
    .step_y = 90,
    .split_x = 380,
    .split_y = 90,
    .sat_x = 470,
    .sat_y = 90,
    .rit_x = 560,
    .rit_y = 90,
    .xit_x = 660,
    .xit_y = 90,
    .filter_x = 730,
    .filter_y = 24,
    .multifn_x = 750,
    .multifn_y = 90
  },
  //
  // The last "layout" must have a negative width to
  // mark the end of the list
  //
  {
    .width = -1
  }
};

int vfo_layout = 0;
