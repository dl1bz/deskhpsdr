/* Copyright (C)
* 2025 - Christoph van Wüllen, DL1YCF
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

#ifdef __APPLE__
#ifdef TTS

#undef MIDI  // This conflicts with Apple stuff

#include <Foundation/Foundation.h>
#include <AVFoundation/AVFoundation.h>

#include "message.h"

static AVSpeechSynthesizer *synth = NULL;

void MacTTS(const char *text) {

  //
  // Convert C string to a NSString and init an AVSpeechUtterance instance
  // with English language
  //
  NSString* str = [NSString stringWithUTF8String:text];

  //
  // ONLY ONCE: create an instance of the synthesizer. This remains alive
  //
  if (synth == NULL) {
   t_print("Creating the MacOS Speech Synthesizer Instance\n");
   synth = [[AVSpeechSynthesizer alloc] init];
  }

  AVSpeechUtterance *utter = [[AVSpeechUtterance alloc] initWithString:str];
  AVSpeechSynthesisVoice *voice = [AVSpeechSynthesisVoice voiceWithLanguage:@"en-GB"];
  [utter setVoice:voice];

  //
  // If the previous text is not yet completely spoken,
  // abort such that the new text can be spoken immediately
  //
  if ([synth isSpeaking]) {
    [synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate ];
  }

  //
  // Put the text into the queue of the synthesizer
  // and return. The synthesizer will be busy with speaking
  // for some more time, we do not wait for the speech being
  // complete.
  //
  [synth speakUtterance:utter];
  [utter release];
}

#endif
#endif
