This is a fork of WebRTC intended to be used in [RingRTC](https://github.com/signalapp/ringrtc).
It currently has the following changes:
* Injections into the build system for RingRTC's Rust FFI
* Changes to Android and iOS SDKs for some more control/customization
* ICE forking (from https://webrtc-review.googlesource.com/c/src/+/167051/)
* Various things disabled (RTP header extensions, audio codecs)
* Various security patches (since the version when the fork branched off)

It began by branching at branch-heads/3987.  Since then, we have merge at the following points:

At branch-heads/4044:
* Update DEPS on Chromium, BoringSSL, FFMPEG, libvpx, ...
* Added support for "GOOG_PING" to ICE
  * Send normal STUN binding requests with a special attribute meaning "I support GOOG_PING" until the first response
  * Send "I support GOOG_PING" in all STUN binding responses (making them slightly bigger)
  * If the response has an "I support GOOG_PING" attribute, switch to supporting GOOG_PING.  Else, don't
  * Is supporting GOOG_PING mode and the last ACKed request is the same as the one just sent, send a GOOG_PING
  * GOOG_PINGs have an HMAC of 4 bytes (instead of 20) and no other attribute (no fingerpint, username, peer reflex priority, etc), which makes them really small
  * GOOG_PING responses also have an HMAC of 4 bytes
* Added support for selecting camera by name to Android SDK
* Added support for degredation preference to Android and iOS SDKs
* MessageQueue and Thread merged into one class
* Audio data includes absolute capture timestamps
* VideoEngine has different send and receive codec capabilities
* VideoEncoders report their requested resolution alignment 
* VideoDecoders allow specifying the size of the decoded frame buffer pool
* VideoSinks can specify a resolution alignment, which is back 
* Change default behavior of audio sending when ANA is enabled to dynamically adjust the packet overhead calculation according to the network route used by ICE and the size of the RTP headers.
* Different behavior for transport-cc1 and transport-cc2 when WebRTC-SendSideBwe-WithOverhead is enabled
* NetEq can take an injectable Clock (which comes from Call::Create)
* NetEq no longer supports INTER_ARRIVAL_TIME HistogramMode (it appeared to be unused)
* Removed AEC(1) (as opposed to AEC2 and AEC3)
* Removed ICE periodic regathering support
* Improved drawing over cursor over desktop capture
* Improved support for WebRTC-RtcpLossNotification ("goog-lntf")