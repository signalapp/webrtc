/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_DESKTOP_CAPTURE_LINUX_X11_X_ERROR_TRAP_H_
#define MODULES_DESKTOP_CAPTURE_LINUX_X11_X_ERROR_TRAP_H_

#include <X11/Xlib.h>

<<<<<<< HEAD:modules/desktop_capture/linux/x11/x_error_trap.h
=======
#include "rtc_base/synchronization/mutex.h"

>>>>>>> m108:modules/desktop_capture/linux/x_error_trap.h
namespace webrtc {

// Helper class that registers an X Window error handler. Caller can use
// GetLastErrorAndDisable() to get the last error that was caught, if any.
class XErrorTrap {
 public:
  explicit XErrorTrap(Display* display);

  XErrorTrap(const XErrorTrap&) = delete;
  XErrorTrap& operator=(const XErrorTrap&) = delete;

  ~XErrorTrap();

<<<<<<< HEAD:modules/desktop_capture/linux/x11/x_error_trap.h
  XErrorTrap(const XErrorTrap&) = delete;
  XErrorTrap& operator=(const XErrorTrap&) = delete;

  // Returns last error and removes unregisters the error handler.
  int GetLastErrorAndDisable();

 private:
  XErrorHandler original_error_handler_;
  bool enabled_;
=======
  // Returns the last error if one was caught, otherwise 0. Also unregisters the
  // error handler and replaces it with `original_error_handler_`.
  int GetLastErrorAndDisable();

 private:
  MutexLock mutex_lock_;
  XErrorHandler original_error_handler_ = nullptr;
>>>>>>> m108:modules/desktop_capture/linux/x_error_trap.h
};

}  // namespace webrtc

#endif  // MODULES_DESKTOP_CAPTURE_LINUX_X11_X_ERROR_TRAP_H_
