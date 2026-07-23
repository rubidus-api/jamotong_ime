/* Microsoft predefined TSF property GUID initializers.
 *
 * MinGW-w64 does not consistently expose/link these symbols, so the lab keeps
 * source initializers. The portable test byte-checks these exact initializers
 * against Microsoft Win32 metadata:
 * https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/manual/TextServices.Manual.cs
 */
#ifndef LAB_TSF_PROPERTY_GUIDS_H
#define LAB_TSF_PROPERTY_GUIDS_H

/* {3280CE20-8032-11D2-B603-00105A2799B5} */
#define LAB_GUID_PROP_LANGID_INITIALIZER \
    { 0x3280ce20, 0x8032, 0x11d2, \
      { 0xb6, 0x03, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 } }

/* {5463F7C0-8E31-11D2-BF46-00105A2799B5} */
#define LAB_GUID_PROP_READING_INITIALIZER \
    { 0x5463f7c0, 0x8e31, 0x11d2, \
      { 0xbf, 0x46, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 } }

#endif /* LAB_TSF_PROPERTY_GUIDS_H */
