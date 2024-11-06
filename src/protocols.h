/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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

extern gboolean enable_protocol_1;
extern gboolean enable_protocol_2;
extern gboolean enable_saturn_xdma;
extern gboolean enable_soapy_protocol;
extern gboolean enable_stemlab;
extern gboolean enable_usbozy;
extern gboolean autostart;

extern void protocolsRestoreState(void);
extern void configure_protocols(GtkWidget *parent);
