#pragma once
// =============================================================================
// CursorShare — Bluetooth Capability Validator
// Enumerates local Bluetooth radios and validates HID profile support.
// =============================================================================

#include <string>
#include <vector>
#include <cstdint>

namespace CursorShare {

/// Result of a single diagnostic check.
struct DiagnosticCheck {
    std::string name;
    bool        passed;
    std::string detail;
};

/// Overall Bluetooth validation result.
struct BluetoothValidationResult {
    bool allPassed;
    std::vector<DiagnosticCheck> checks;

    // Adapter info (populated if adapter found)
    std::string adapterName;
    std::string adapterAddress;
    uint32_t    adapterClass;
    uint16_t    adapterManufacturer;
    uint16_t    adapterSubversion;

    void AddCheck(const std::string& name, bool passed, const std::string& detail = "") {
        checks.push_back({name, passed, detail});
        if (!passed) allPassed = false;
    }

    std::string GetSummary() const;
};

/// Validate the host Bluetooth adapter for CursorShare compatibility.
/// Must be called before enabling broadcast mode.
class BluetoothValidator {
public:
    /// Run all validation checks. Returns structured result.
    static BluetoothValidationResult Validate();

    /// Quick check: is a BT adapter present and minimally functional?
    static bool IsBluetoothAvailable();

private:
    static bool CheckAdapterPresent(BluetoothValidationResult& result);
    static bool CheckAdapterEnabled(BluetoothValidationResult& result);
    static bool CheckClassicSupport(BluetoothValidationResult& result);
    static bool CheckDiscoverability(BluetoothValidationResult& result);
    static bool CheckDriverStack(BluetoothValidationResult& result);
    static bool CheckPolicyRestrictions(BluetoothValidationResult& result);
};

}  // namespace CursorShare
