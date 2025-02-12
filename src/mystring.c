/* Copyright (C)
* 2023 - Christoph van Wüllen, DL1YCF
* 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   This source code has been forked and was adapted from piHPSDR by DL1YCF to deskHPSDR in October 2024
* Copyright (c) 1998, 2015 Todd C. Miller <Todd.Miller@courtesan.com>
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

//
// strlcat and strlcpy are the "better replacements" for strncat and strncpy.
// However, they are not in Linux glibc and not POSIX standardized. So for the
// time being, we take the functions from the libbsd repo with the names converted
// to uppercase.
//
//
/*  $OpenBSD: strlcat.c,v 1.15 2015/03/02 21:41:08 millert Exp $  */
/*  $OpenBSD: strlcpy.c,v 1.12 2015/01/15 03:54:12 millert Exp $  */

/*
 * Copyright (c) 1998, 2015 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <string.h>
// add by DH0DM
#if defined (__LDESK__)
  // #include <pthread.h>
  // pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  #include <gtk/gtk.h>
  static GMutex copy_string_mutex;
#endif

/*
 * Appends src to string dst of size dsize (unlike strncat, dsize is the
 * full size of dst, not space left).  At most dsize-1 characters
 * will be copied.  Always NUL terminates (unless dsize <= strlen(dst)).
 * Returns strlen(src) + MIN(dsize, strlen(initial dst)).
 * If retval >= dsize, truncation occurred.
 */
size_t
STRLCAT(char *dst, const char *src, size_t dsize) {
  const char *odst = dst;
  const char *osrc = src;
  size_t n = dsize;
  size_t dlen;
#if defined (__LDESK__)
  g_mutex_lock(&copy_string_mutex);
#endif

  /* Find the end of dst and adjust bytes left but don't go past end. */
  while (n-- != 0 && *dst != '\0') {
    dst++;
  }

  dlen = dst - odst;
  n = dsize - dlen;

  if (n-- == 0) {
    return (dlen + strlen(src));
  }

  while (*src != '\0') {
    if (n != 0) {
      *dst++ = *src;
      n--;
    }

    src++;
  }

  *dst = '\0';
#if defined (__LDESK__)
  g_mutex_unlock(&copy_string_mutex);
#endif
  return (dlen + (src - osrc)); /* count does not include NUL */
}

/*
 * Copy string src to buffer dst of size dsize.  At most dsize-1
 * chars will be copied.  Always NUL terminates (unless dsize == 0).
 * Returns strlen(src); if retval >= dsize, truncation occurred.
 */
size_t
STRLCPY(char *dst, const char *src, size_t dsize) {
  const char *osrc = src;
  size_t nleft = dsize;
  // add by DH0DM
#if defined (__LDESK__)
  // pthread_mutex_lock(&mutex);
  g_mutex_lock(&copy_string_mutex);
#endif

  /* Copy as many bytes as will fit. */
  if (nleft != 0) {
    while (--nleft != 0) {
      if ((*dst++ = *src++) == '\0') {
        break;
      }
    }
  }

  /* Not enough room in dst, add NUL and traverse rest of src. */
  if (nleft == 0) {
    if (dsize != 0) {
      *dst = '\0';  /* NUL-terminate dst */
    }

    while (*src++)
      ;
  }

  // add by DH0DM
#if defined (__LDESK__)
  // pthread_mutex_unlock(&mutex);
  g_mutex_unlock(&copy_string_mutex);
#endif
  return (src - osrc - 1); /* count does not include NUL */
}
