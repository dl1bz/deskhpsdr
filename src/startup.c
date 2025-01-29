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
// The startup function first tries to detect whether piHPSDR is running
// in its own directory (as usual for compiled-from-the-source installations),
// or whether it is started from a desktop icon and resides in /usr/bin or
// /usr/local/bin.
//
// Only in the latter case, the following steps are taken (this eliminates the
// need for a wrapper startup script):
//
// - create a working directory, if it not yet exists
// - make this the current working directory
// - create files pihpsdr.stdout and pihpsdr.stderr there, and connect them
//   with stdout and stderr
//
// The working directory on Linux is $HOME/.config/pihpsdr, on MacOS it is
// "$HOME/Library/Application Support/piHPSDR".
//
// If something goes wrong (e.g. the $HOME environment variable does not exist,
// the working directory exists but is not a directory, or cannot be created)
// then $HOME is used as the working dir.
//
// On MacOS, if the program is started from an app bundle containing the file
// hpsdr.png, this file is copied to the working directory
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
#include "mystring.h"

void startup(const char *path) {
  struct stat statbuf;
  char filename[PATH_MAX];
  char workdir[PATH_MAX];
  int writeable;
  int found;
  int rc;
  const char *homedir;
  const struct passwd *pwd;
#ifdef __APPLE__
  static IOPMAssertionID keep_awake = 0;
  //
  //  This is to prevent "going to sleep" or activating the screen saver
  //  while piHPSDR is running
  //
  //  works from macOS 10.6 so no check on availability needed.
  //  no return check is needed: if it fails, it fails.
  //
  IOPMAssertionCreateWithName (kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn,
                               CFSTR ("deskHPSDR"), &keep_awake);
#endif
  writeable = 0;  // if zero, the current dir is not writeable
  found = 0;      // if nonzero, hpsdr.png or protocols.props found in current dir
  //
  // try to create a file with an unique file name
  //
#if defined (__LDESK__)
  snprintf(filename, PATH_MAX, "deskHPSDR.myFile.%ld", (long) getpid());
#else
  snprintf(filename, PATH_MAX, "piHPSDR.myFile.%ld", (long) getpid());
#endif
  rc = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0700);

  if (rc >= 0) {
    writeable = 1;
    close (rc);
    unlink(filename);
  }

  //
  // Look whether file pihpsdr.sh or directory release/pihpsdr exists
  //
  rc = stat("pihpsdr.sh", &statbuf);

  if (rc == 0 && (S_ISREG(statbuf.st_mode) || S_ISLNK(statbuf.st_mode))) { found = 1;}

  rc = stat("release/pihpsdr", &statbuf);

  if (rc == 0 && S_ISDIR(statbuf.st_mode)) { found = 1;}

  //
  // Most likely, piHPDSR is expected to run in the current working directory
  //
  if (writeable && found) {
    t_print("%s: working directory not changed.\n", __FUNCTION__);
    return;
  }

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
    // non-recoverable error
    t_print("%s: home dir not found, working directory not changed.\n", __FUNCTION__);
    return;
  }

#ifdef __APPLE__
#if defined (__LDESK__)
  snprintf(workdir, PATH_MAX, "%s/Library/Application Support/deskHPSDR", homedir);
#else
  snprintf(workdir, PATH_MAX, "%s/Library/Application Support/piHPSDR", homedir);
#endif

  if (stat(workdir, &statbuf) < 0) {
    mkdir (workdir, 0700);
  }

  rc = stat(workdir, &statbuf);

  if (rc < 0 || !S_ISDIR(statbuf.st_mode)) {
    STRLCPY(workdir, homedir, PATH_MAX);
  }

#else
  snprintf(workdir, PATH_MAX, "%s/.config", homedir);

  if (stat(workdir, &statbuf) < 0) {
    mkdir (workdir, 0700);
  }

#if defined (__LDESK__)
  snprintf(workdir, PATH_MAX, "%s/.config/deskhpsdr", homedir);
#else
  snprintf(workdir, PATH_MAX, "%s/.config/pihpsdr", homedir);
#endif

  if (stat(workdir, &statbuf) < 0) {
    mkdir (workdir, 0700);
  }

#endif
  //
  // Check if workdir exists and is a directory, if not, take home dir
  //
  rc = stat(workdir, &statbuf);

  if (rc < 0 || !S_ISDIR(statbuf.st_mode)) {
    STRLCPY(workdir, homedir, PATH_MAX);
  }

  //
  // At this point, the new working directory exists and the name
  // is in filename.
  //
  if (chdir(workdir) != 0) {
    // unrecoverable error, could not chdir to target
    t_print("%s: Could not chdir to working dir %s\n", __FUNCTION__, workdir);
    return;
  }

  //
  //  Make two local files for stdout and stderr, to allow
  //  post-mortem debugging
  //
#if defined (__LDESK__)
  (void) freopen("deskhpsdr.stdout", "w", stdout);
  (void) freopen("deskhpsdr.stderr", "w", stderr);
#else
  (void) freopen("pihpsdr.stdout", "w", stdout);
  (void) freopen("pihpsdr.stderr", "w", stderr);
#endif
  t_print("%s: working dir changed to %s\n", __FUNCTION__, workdir);
#ifdef __APPLE__

  //
  // path is the name of the executable. Try to find hpsdr.png *relative* to that.
  // If found and this file is not (yet) there, copy to work dir
  //
  if (stat("hpsdr.png", &statbuf) < 0)  {
    char *c;
    int fdin, fdout;
    char source[PATH_MAX];
    STRLCPY(source, path, PATH_MAX);
    c = rindex(source, '/');

    if (c) { *c = 0; }

    STRLCAT(source,  "/../Resources/hpsdr.png", PATH_MAX);
    //
    // Now copy the file from "source" to "workdir"
    //
    fdin = open(source, O_RDONLY);

    if (fdin >= 0) {
      fdout = open("hpsdr.png", O_WRONLY | O_CREAT | O_TRUNC, (mode_t) 0400);
    }

    if (fdin >= 0 && fdout >= 0) {
      //
      // Now do the copy, use "source" as I/O buffer
      //
      ssize_t bytesread;
      t_print("%s: copying hpsdr.png to working dir\n", __FUNCTION__);

      while ((bytesread = read(fdin, source, PATH_MAX)) > 0) { write (fdout, source, bytesread); }

      close(fdin);
      close(fdout);
    }
  }

#endif
}
