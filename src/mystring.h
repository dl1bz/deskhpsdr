/* Copyright (C)
* 2023 - Christoph van Wüllen, DL1YCF
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
// However, they are not in Linux glibc and not POSIX standardized.
// Therefore we include them with the name capitalized in this program.
//

#ifndef _MYSTRING_H_
#define _MYSTRING_H_

#include <sys/types.h>
#include <string.h>

size_t STRLCAT(char *dst, const char *src, size_t dsize);
size_t STRLCPY(char *dst, const char *src, size_t dsize);

#endif
