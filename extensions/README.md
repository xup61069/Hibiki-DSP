# Chrome/Edge extension

The MV3 extension starts single-tab capture only after an explicit user gesture.
Captured audio is handed to a native lane; the extension never receives private
Hibiki signing or Gumroad credentials.

The source includes a minimal MV3 popup, service worker and offscreen document.
`tabCapture.getMediaStreamId` is requested only from the popup click path. The
offscreen graph keeps the user-selected stream alive after the popup closes;
the native bridge/audio processing contract is intentionally a later component.
