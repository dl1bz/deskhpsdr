/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
* 2024-2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* SPDX-License-Identifier: GPL-3.0-or-later
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

#include <stddef.h>
#include <netinet/in.h>

extern int  discover_only_stemlab;

extern int delayed_discovery(gpointer data);
extern void discovery(void);
extern char *ipaddr_radio;
extern int radio_port;
extern int active_device_index;

int discovery_resolve_target(const char *host,
                             struct sockaddr_in *target,
                             struct sockaddr_in *local_addr,
                             struct sockaddr_in *netmask,
                             char *ifname,
                             size_t ifname_len,
                             int *is_direct);
