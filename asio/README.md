# Hibiki ASIO bridge

The Hibiki ASIO bridge is a GPL user-space component. DAWs must select this
endpoint to receive Hibiki correction and Windows-linked Group Master volume.
Vendor ASIO clients remain outside the interception boundary.

`AsioBridgeModel` is the current deterministic stream contract: it accepts only
2/6/8-channel LPCM at 44.1/48/96/192 kHz, applies the canonical Group Master
once, and exposes a bounded interleaved process call with no allocation. It is
not yet a Steinberg ASIO DLL; SDK hosting, COM registration and device I/O are
the next implementation layer.
