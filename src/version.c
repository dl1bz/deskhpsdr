/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

char build_date[] = GIT_DATE;
char build_version[] = GIT_VERSION;
char build_commit[] = GIT_COMMIT;

char build_options[] =
#ifdef GPIO
  "GPIO "
#endif
#ifdef MIDI
  "MIDI "
#endif
#ifdef SATURN
  "SATURN "
#endif
#ifdef USBOZY
  "USBOZY "
#endif
#ifdef SOAPYSDR
  "SOAPYSDR "
#endif
#ifdef STEMLAB_DISCOVERY
  "STEMLAB "
#endif
#ifdef EXTNR
  "EXTNR "
#endif
#ifdef CLIENT_SERVER
  "SERVER "
#endif
  "";

char build_audio[] =
#ifdef ALSA
  "ALSA";
#endif
#ifdef PULSEAUDIO
  "PulseAudio";
#endif
#ifdef PORTAUDIO
  "PortAudio";
#endif
#if !defined(ALSA) && !defined(PORTAUDIO) && !defined(PULSEAUDIO)
  "(unkown)";
#endif
