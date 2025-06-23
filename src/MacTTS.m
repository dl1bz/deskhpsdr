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

  AVSpeechUtterance *utter = [[AVSpeechUtterance alloc] initWithString:str];
  AVSpeechSynthesisVoice *voice = [AVSpeechSynthesisVoice voiceWithLanguage:@"en-GB"];
  [utter setVoice:voice];

  //
  // ONLY ONCE: create an instance of the synthesizer. This remains alive
  //
  if (synth == NULL) {
   t_print("Creating the MacOS Speech Synthesizer Instance\n");
   synth = [[AVSpeechSynthesizer alloc] init];
  }

  //
  // Put the text into the queue of the synthesizer
  // and return fast. No problem if the synthesizer is
  // still busy with the preceeding text.
  //
  [synth speakUtterance:utter];

}

#endif
#endif
