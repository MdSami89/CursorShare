# Safe Shutdown & Rollback

## Shutdown Sequence

CursorShare performs a deterministic shutdown sequence:

1. **Switch to Host mode** — flush keyboard/mouse state
2. **Stop Raw Input capture** — unregister `RAWINPUTDEVICE` entries
3. **Unregister global shortcut** — `UnregisterHotKey`
4. **Close all L2CAP channels** — `closesocket` for control + interrupt
5. **Unregister SDP record** — `WSASetService(RNRSERVICE_DELETE)`
6. **Restore discoverability** — `BluetoothEnableDiscovery(originalState)`
7. **Restore connectability** — `BluetoothEnableIncomingConnections(originalState)`
8. **Verify paired devices** — compare current list to startup snapshot
9. **Cleanup Winsock** — `WSACleanup()`
10. **Report results** — log any discrepancies

## Driver Cleanup (if installed)

1. Detach filter driver via `SetupDiCallClassInstaller`
2. Remove upper-filter registry entry
3. Unmap shared memory sections
4. Close kernel event objects
5. Verify input stack restored to default

## Guarantees

- All operations are idempotent (safe to call multiple times)
- Signal handlers catch Ctrl+C, console close, logoff, shutdown
- Paired device list is preserved (snapshot comparison)
- No persistent system modifications unless explicitly configured
- Stuck-key prevention via "all keys up" report on every mode switch

## Paired Device Preservation

On startup, `BluetoothPairingManager::SnapshotPairedDevices()` captures:
- Device address, name, class of device
- Authentication and connection state
- Remembered status

On shutdown, `VerifyPairedDevices()` confirms all original devices still exist.
