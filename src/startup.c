/* Copyright (C)
* 2023 - Christoph van Wüllen, DL1YCF
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

//
// The startup function first tries to detect whether deskHPSDR is running
// in its own directory (as usual for compiled-from-the-source installations),
// or whether it is started from a desktop icon and resides in /usr/bin or
// /usr/local/bin.
//
// Only in the latter case, the following steps are taken (this eliminates the
// need for a wrapper startup script):
//
// - create a working directory, if it not yet exists
// - make this the current working directory
//
// The working directory on Linux is $HOME/.config/deskhpsdr, on MacOS it is
// "$HOME/Library/Application Support/deskHPSDR".
//
// If something goes wrong (e.g. the $HOME environment variable does not exist,
// the working directory exists but is not a directory, or cannot be created)
// then $HOME is used as the working dir.
//
// Note no output (via t_print) should be made until either stdout is "reconnected"
// or we know that we won't reconnect it.
//
// This routine is also the right place to set priorities, etc., if the operating
// system allows. For MacOS, we set the "Keep awake" flag.
//

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>

#ifdef __APPLE__
  #include <IOKit/IOKitLib.h>
  #include <IOKit/pwr_mgt/IOPMLib.h>
#endif

#include "message.h"

char workdir[PATH_MAX];

void startup(const char *path) {
  struct stat statbuf;
  int rc;
  const char *homedir;
  const struct passwd *pwd;
#ifdef __APPLE__
  static IOPMAssertionID keep_awake = 0;
  //
  //  This is to prevent "going to sleep" or activating the screen saver
  //  while deskHPSDR is running
  //
  //  works from macOS 10.6 so no check on availability needed.
  //  no return check is needed: if it fails, it fails.
  //
  IOPMAssertionCreateWithName (kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn,
                               CFSTR ("deskHPSDR"), &keep_awake);
#endif
  //
  // Get home dir
  //
  homedir = getenv("HOME");

  if (homedir == NULL) {
    pwd = getpwuid(getuid());

    if (pwd != NULL) {
      homedir = pwd->pw_dir;
    }
  }

  if (homedir == NULL) {
    // If $HOME cannot be determined: stay in CWD if writable, otherwise fall back to /tmp/deskhpsdr.
    if (access(".", W_OK) == 0) {
      return;
    }

    g_strlcpy(workdir, "/tmp/deskhpsdr", PATH_MAX);

    if (stat(workdir, &statbuf) < 0) {
      if (mkdir(workdir, 0755) != 0) {
        t_print("%s: Could not create %s\n", __func__, workdir);
        return;
      }
    } else if (!S_ISDIR(statbuf.st_mode)) {
      t_print("%s: %s exists but is not a directory\n", __func__, workdir);
      return;
    } else {
      if (chmod(workdir, 0755) != 0) {
        t_print("%s: Could not chmod %s to 0755\n", __func__, workdir);
        return;
      }
    }

    if (chdir(workdir) != 0) {
      t_print("%s: Could not chdir to working dir %s\n", __func__, workdir);
    } else {
      (void) freopen("deskhpsdr.log", "w", stdout);
      (void) freopen("deskhpsdr.err", "w", stderr);
      t_print("%s: working dir changed to %s\n", __func__, workdir);
    }

    return;
  }

#ifdef __APPLE__
  snprintf(workdir, PATH_MAX, "%s/Library/Application Support/deskHPSDR", homedir);

  if (stat(workdir, &statbuf) < 0) {
    mkdir (workdir, 0700);
  }

  rc = stat(workdir, &statbuf);

  if (rc < 0 || !S_ISDIR(statbuf.st_mode)) {
    g_strlcpy(workdir, homedir, PATH_MAX);
  }

#else
  snprintf(workdir, PATH_MAX, "%s/.config", homedir);

  if (stat(workdir, &statbuf) < 0) {
    mkdir (workdir, 0700);
  }

  snprintf(workdir, PATH_MAX, "%s/.config/deskhpsdr", homedir);

  if (stat(workdir, &statbuf) < 0) {
    mkdir (workdir, 0700);
  }

#endif
  //
  // Check if workdir exists and is a directory, if not, take home dir
  //
  rc = stat(workdir, &statbuf);

  if (rc < 0 || !S_ISDIR(statbuf.st_mode)) {
    g_strlcpy(workdir, homedir, PATH_MAX);
  }

  //
  // At this point, the new working directory exists and the name
  // is in filename.
  //
  if (chdir(workdir) != 0) {
    // unrecoverable error, could not chdir to target
    t_print("%s: Could not chdir to working dir %s\n", __func__, workdir);
    return;
  }

  //
  //  Make two local files for stdout and stderr, to allow
  //  post-mortem debugging
  //
  (void) freopen("deskhpsdr.log", "w", stdout);
  (void) freopen("deskhpsdr.err", "w", stderr);
  t_print("%s: working dir changed to %s\n", __func__, workdir);
}
