/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "RTCAudioSession+Private.h"
#import "RTCAudioSessionConfiguration.h"

#import "base/RTCLogging.h"

@implementation RTC_OBJC_TYPE (RTCAudioSession)
(Configuration)

    - (BOOL)setConfiguration
    : (RTC_OBJC_TYPE(RTCAudioSessionConfiguration) *)configuration error
    : (NSError **)outError {
  return [self setConfiguration:configuration
                         active:NO
                shouldSetActive:NO
                          error:outError];
}

- (BOOL)setConfiguration:
            (RTC_OBJC_TYPE(RTCAudioSessionConfiguration) *)configuration
                  active:(BOOL)active
                   error:(NSError **)outError {
  return [self setConfiguration:configuration
                         active:active
                shouldSetActive:YES
                          error:outError];
}

#pragma mark - Private

- (BOOL)setConfiguration:
            (RTC_OBJC_TYPE(RTCAudioSessionConfiguration) *)configuration
                  active:(BOOL)active
         shouldSetActive:(BOOL)shouldSetActive
                   error:(NSError **)outError {
  NSParameterAssert(configuration);
  if (outError) {
    *outError = nil;
  }

  // Provide an error even if there isn't one so we can log it. We will not
  // return immediately on error in this function and instead try to set
  // everything we can.
  NSError *error = nil;

  // RingRTC change to ignore AudioSession category, mode, and options changes in objc sdk.
  // if (self.category != configuration.category ||
  //     self.mode != configuration.mode ||
  //     self.categoryOptions != configuration.categoryOptions) {
  //   NSError *configuringError = nil;
  //   if (![self setCategory:configuration.category
  //                     mode:configuration.mode
  //                  options:configuration.categoryOptions
  //                    error:&configuringError]) {
  //     RTCLogError(@"Failed to set category and mode: %@",
  //                 configuringError.localizedDescription);
  //     error = configuringError;
  //   } else {
  //     RTCLog(@"Set category to: %@, mode: %@",
  //            configuration.category,
  //            configuration.mode);
  //   }
  // }

  if (self.preferredSampleRate != configuration.sampleRate) {
    NSError *sampleRateError = nil;
    if (![self setPreferredSampleRate:configuration.sampleRate
                                error:&sampleRateError]) {
      RTCLogError(@"Failed to set preferred sample rate: %@",
                  sampleRateError.localizedDescription);
      if (!self.ignoresPreferredAttributeConfigurationErrors) {
        error = sampleRateError;
      }
    } else {
      RTCLog(@"Set preferred sample rate to: %.2f", configuration.sampleRate);
    }
  }

  if (self.preferredIOBufferDuration != configuration.ioBufferDuration) {
    NSError *bufferDurationError = nil;
    if (![self setPreferredIOBufferDuration:configuration.ioBufferDuration
                                      error:&bufferDurationError]) {
      RTCLogError(@"Failed to set preferred IO buffer duration: %@",
                  bufferDurationError.localizedDescription);
      if (!self.ignoresPreferredAttributeConfigurationErrors) {
        error = bufferDurationError;
      }
    } else {
      RTCLog(@"Set preferred IO buffer duration to: %f",
             configuration.ioBufferDuration);
    }
  }

  if (shouldSetActive) {
    NSError *activeError = nil;
    if (![self setActive:active error:&activeError]) {
      RTCLogError(@"Failed to setActive to %d: %@",
                  active,
                  activeError.localizedDescription);
      error = activeError;
    }
  }

  if (self.isActive &&
      // TODO(tkchin): Figure out which category/mode numChannels is valid for.
      // RingRTC change to support VoiceChat and VideoChat in objc sdk.
      ([self.mode isEqualToString:AVAudioSessionModeVoiceChat] ||
       [self.mode isEqualToString:AVAudioSessionModeVideoChat])) {
    // Try to set the preferred number of hardware audio channels. These calls
    // must be done after setting the audio session’s category and mode and
    // activating the session.
    NSInteger inputNumberOfChannels = configuration.inputNumberOfChannels;
    if (self.inputNumberOfChannels != inputNumberOfChannels) {
      NSError *inputChannelsError = nil;
      if (![self setPreferredInputNumberOfChannels:inputNumberOfChannels
                                             error:&inputChannelsError]) {
        RTCLogError(@"Failed to set preferred input number of channels: %@",
                    inputChannelsError.localizedDescription);
        if (!self.ignoresPreferredAttributeConfigurationErrors) {
          error = inputChannelsError;
        }
      } else {
        RTCLog(@"Set input number of channels to: %ld",
               (long)inputNumberOfChannels);
      }
    }
    NSInteger outputNumberOfChannels = configuration.outputNumberOfChannels;
    if (self.outputNumberOfChannels != outputNumberOfChannels) {
      NSError *outputChannelsError = nil;
      if (![self setPreferredOutputNumberOfChannels:outputNumberOfChannels
                                              error:&outputChannelsError]) {
        RTCLogError(@"Failed to set preferred output number of channels: %@",
                    outputChannelsError.localizedDescription);
        if (!self.ignoresPreferredAttributeConfigurationErrors) {
          error = outputChannelsError;
        }
      } else {
        RTCLog(@"Set output number of channels to: %ld",
               (long)outputNumberOfChannels);
      }
    }
  }

  if (outError) {
    *outError = error;
  }

  return error == nil;
}

@end
