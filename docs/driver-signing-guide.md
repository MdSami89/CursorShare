# Driver Signing Guide

## Development (Test Signing)

### Enable Test Signing
```powershell
bcdedit /set testsigning on
# Reboot required
```

### Create Test Certificate
```powershell
makecert -r -pe -ss PrivateCertStore -n "CN=CursorShareTest" CursorShareTest.cer
```

### Sign Driver
```powershell
signtool sign /v /s PrivateCertStore /n CursorShareTest /t http://timestamp.digicert.com CursorShareKbFilter.sys
signtool sign /v /s PrivateCertStore /n CursorShareTest /t http://timestamp.digicert.com CursorShareMouFilter.sys
```

### Install Driver
```powershell
# Keyboard filter
pnputil /add-driver kbfilter.inf /install

# Mouse filter
pnputil /add-driver moufilter.inf /install
```

### Uninstall Driver
```powershell
pnputil /delete-driver kbfilter.inf /uninstall
pnputil /delete-driver moufilter.inf /uninstall
```

## Production (Attestation Signing)

1. Obtain EV code-signing certificate from approved CA
2. Register with Windows Hardware Dev Center
3. Submit driver package via Hardware Lab Kit (HLK)
4. Microsoft attestation-signs the driver
5. Distributed driver works without test-signing mode

### Requirements
- EV certificate from DigiCert, GlobalSign, or Sectigo
- Windows Hardware Dev Center account
- HLK test results passing
- WHQL certification (optional but recommended)

## Disable Test Signing (After Development)
```powershell
bcdedit /set testsigning off
# Reboot required
```
