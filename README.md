This is a fork of WebRTC intended to be used in [RingRTC](https://github.com/signalapp/ringrtc).
It currently has the following changes:
* Injections into the build system for RingRTC's Rust FFI
* Changes to Android and iOS SDKs for some more control/customization
* ICE forking (from https://webrtc-review.googlesource.com/c/src/+/167051/)
* Various things disabled (RTP header extensions, audio codecs)
* Various security patches (since the version when the fork branched off)

See [here][native-dev] for instructions on how to get started
developing with the native code.

[Authoritative list](native-api.md) of directories that contain the
native API header files.

### More info

 * Official web site: http://www.webrtc.org
 * Master source code repo: https://webrtc.googlesource.com/src
 * Samples and reference apps: https://github.com/webrtc
 * Mailing list: http://groups.google.com/group/discuss-webrtc
 * Continuous build: http://build.chromium.org/p/client.webrtc
 * [Coding style guide](style-guide.md)
 * [Code of conduct](CODE_OF_CONDUCT.md)
 * [Reporting bugs](docs/bug-reporting.md)

[native-dev]: https://webrtc.googlesource.com/src/+/refs/heads/master/docs/native-code/index.md
