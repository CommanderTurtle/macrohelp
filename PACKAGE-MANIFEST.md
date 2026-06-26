# Package Manifest

Created: 2026-06-25

## Runtime Folders

```text
macrohelp-runtime/
  bin/
  source/

tasket-http-daemon/
  bin/
  bin/Qt6*.dll
  bin/platforms, styles, imageformats, networkinformation, tls, generic
  source/
```

## Binary Hashes (6/25/26 — may be build specific)

```text
CursorOverlay :
88E6F46BB7A4A117C73C2E51DA973813DBC4711CE48E4CA4AE680E65679652D1

tasket-httpd :
E80B5388A448A0C4E447AF5F7E79238D55458797E3D1F872228FE146066C82F6
```

Quick Hash with Powershell for Integrity:

```powershell
# Replace file-path (oneliner)
$fp="file-path"; "SHA256: $((Get-FileHash $fp -Algorithm SHA256).Hash)`nSHA1:   $((Get-FileHash $fp -Algorithm SHA1).Hash)`nMD5:    $((Get-FileHash $fp -Algorithm MD5).Hash)"
```
