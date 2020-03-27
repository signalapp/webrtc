This is a fork of WebRTC intended to be used in [RingRTC](https://github.com/signalapp/ringrtc).
It currently has the following changes:
* Injections into the build system for RingRTC's Rust FFI
* Changes to Android and iOS SDKs for some more control/customization
* ICE forking (from https://webrtc-review.googlesource.com/c/src/+/167051/)
* Various things disabled (RTP header extensions, audio codecs)
* Various security patches (since the version when the fork branched off)