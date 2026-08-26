/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "rffi/api/logging.h"

namespace webrtc {
namespace rffi {

// A simple implementation of LogSink that just passes the message
// to Rust.
class Logger : public LogSink {
 public:
  Logger(LoggerCallbacks* cbs) : cbs_(*cbs) {}

  void OnLogMessage(const std::string& message) override {
    OnLogMessage(message, LS_NONE);
  }

  void OnLogMessage(const std::string& message,
                    webrtc::LoggingSeverity severity) override {
    cbs_.onLogMessage(severity, message.c_str());
  }

 private:
  LoggerCallbacks cbs_;
};

RUSTEXPORT void Rust_setLogger(LoggerCallbacks* cbs_borrowed,
                               webrtc::LoggingSeverity min_sev) {
  Logger* logger_owned = new Logger(cbs_borrowed);
  // LEAK: it's only called once, so it shouldn't matter.
  Logger* logger_borrowed = logger_owned;
  // Stores the sink, but does not delete it.
  LogMessage::AddLogToStream(logger_borrowed, min_sev);
}

}  // namespace rffi
}  // namespace webrtc