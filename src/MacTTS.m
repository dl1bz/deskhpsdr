#ifdef __APPLE__
#ifdef TTS

#undef MIDI  // This conflicts with Apple stuff

#include <Foundation/Foundation.h>
#include <AVFoundation/AVFoundation.h>

#include "message.h"

static AVSpeechSynthesizer *synth = NULL;

void MacTTS(char *text) {

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

}

#endif
#endif
