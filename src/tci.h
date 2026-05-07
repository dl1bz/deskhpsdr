/* Copyright (C)
* 2024 - Christoph van W"ullen, DL1YCF
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

extern int tci_enable;
extern int tci_port;   // usually 40001
extern int tci_txonly; // only report TX frequency

void launch_tci(void);
void shutdown_tci(void);
void tci_mox_changed(int state);
void tci_vfo_changed(int id);
void tci_vfos_changed(void);
void tci_mode_changed(int id);
void tci_tx_frequency_changed(void);
int tci_is_applying(void);
void tci_tune_changed(int state);
void tci_split_changed(void);
void tci_begin_tune_transition(void);
void tci_end_tune_transition(void);
int tci_is_tune_transition(void);
