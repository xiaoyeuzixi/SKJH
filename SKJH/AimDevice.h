#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <hidsdi.h>
#include <setupapi.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Hid.lib")
#pragma comment(lib, "Setupapi.lib")

namespace SKJH {
namespace Aim {

enum class AimDeviceKind : uint8_t {
    None = 0,
    KMBoxSerial,
    CH9329,
    GenericSerial,
    KMBoxNet,
    Makcu,
    MoBox,
    Lurker,
    RawHid,
};

enum class AimDeviceConnectionState : uint8_t {
    Disabled = 0,
    Disconnected,
    Connecting,
    Connected,
    Error,
    Unsupported,
};

enum class RawHidTransferMode : uint8_t {
    InterruptOut = 0,
    OutputReport,
    FeatureReport,
};

struct AimDeviceCapabilities {
    bool supported = false;
    bool relativeMovement = false;
    bool serialTransport = false;
    bool binaryProtocol = false;
    bool testMovement = false;
    bool requiresExternalAdapter = false;
    int32_t minimumDelta = 0;
    int32_t maximumDelta = 0;
    size_t maximumPacketBytes = 0;
};

struct AimDeviceStatus {
    AimDeviceConnectionState state = AimDeviceConnectionState::Disconnected;
    DWORD lastError = ERROR_SUCCESS;
    std::string message = "Disconnected";
    uint64_t commandsSent = 0;
    uint64_t bytesSent = 0;
    ULONGLONG lastTransitionTick = 0;

    bool IsConnected() const noexcept {
        return state == AimDeviceConnectionState::Connected;
    }
};

struct AimDeviceConfig {
    AimDeviceKind kind = AimDeviceKind::None;
    std::string serialPort = "COM3";
    uint32_t baudRate = 115200;
    uint32_t writeTimeoutMs = 250;
    uint32_t receiveTimeoutMs = 250;
    bool enableDtr = false;
    bool enableRts = false;

    // KMBox NET settings are public for the UI/configuration layer.
    std::string networkIp = "192.168.2.188";
    uint32_t networkPort = 8336;
    std::string networkUuid;

    // Native Windows RawHID output. Offsets address the complete HID report,
    // including report ID at byte zero. 0xFFFF disables the buttons field.
    uint16_t rawHidVid = 0;
    uint16_t rawHidPid = 0;
    uint16_t rawHidUsagePage = 0;
    uint16_t rawHidUsage = 0;
    uint32_t rawHidInterfaceIndex = 0;
    uint16_t rawHidExpectedReportBytes = 0;
    uint8_t rawHidReportId = 0;
    uint16_t rawHidButtonsOffset = 1;
    uint16_t rawHidXOffset = 2;
    uint16_t rawHidYOffset = 3;
    uint8_t rawHidAxisBytes = 1;
    bool rawHidLittleEndian = true;
    RawHidTransferMode rawHidTransferMode =
        RawHidTransferMode::InterruptOut;
    std::vector<uint8_t> rawHidReportTemplate;

    // GenericSerial accepts printable ASCII plus CR/LF/TAB and only the
    // placeholders {x} and {y}. printf-style formatting is intentionally not
    // supported.
    std::string genericMoveTemplate = "MOVE {x} {y}\r\n";
};

struct AimDeviceConfigValidation {
    bool valid = false;
    std::string error;
};

struct AimDeviceDescriptor {
    AimDeviceKind kind = AimDeviceKind::None;
    const char* key = "none";
    const char* displayName = "None";
    const char* protocol = "Disabled";
    const char* unsupportedReason = nullptr;
    AimDeviceCapabilities capabilities{};
};

class IAimDevice {
public:
    virtual ~IAimDevice() = default;

    virtual AimDeviceKind Kind() const noexcept = 0;
    virtual bool Connect(const AimDeviceConfig& config) = 0;
    virtual void Disconnect() noexcept = 0;
    virtual bool Move(int32_t deltaX, int32_t deltaY) = 0;
    virtual bool Test(int32_t distance = 8) = 0;
    virtual AimDeviceStatus Status() const = 0;
    virtual AimDeviceCapabilities Capabilities() const noexcept = 0;
};

namespace Detail {

inline constexpr AimDeviceCapabilities NoCapabilities() noexcept {
    return {};
}

inline constexpr AimDeviceCapabilities TextSerialCapabilities() noexcept {
    return {true, true, true, false, true, false,
            -32767, 32767, 64};
}

inline constexpr AimDeviceCapabilities CH9329Capabilities() noexcept {
    return {true, true, true, true, true, false,
            -127, 127, 11};
}

inline constexpr AimDeviceCapabilities LurkerCapabilities() noexcept {
    return {true, true, true, true, true, false,
            -127, 127, 7};
}

inline constexpr AimDeviceCapabilities KMBoxNetCapabilities() noexcept {
    return {true, true, false, true, true, false,
            -32767, 32767, 72};
}

inline constexpr AimDeviceCapabilities RawHidCapabilities() noexcept {
    return {true, true, false, true, true, false,
            -32767, 32767, 512};
}

inline constexpr AimDeviceCapabilities UnsupportedCapabilities() noexcept {
    return {false, false, false, false, false, true,
            0, 0, 0};
}

inline bool IsSupportedBaudRate(uint32_t baudRate) noexcept {
    return baudRate >= 1200 && baudRate <= 4000000;
}

inline bool ParseStrictIpv4(std::string_view input, uint32_t& networkOrder,
                            std::string& error) {
    networkOrder = 0;
    error.clear();
    if (input.empty() || input.size() > 15) {
        error = "KMBox NET IP must be a dotted-decimal IPv4 address";
        return false;
    }

    std::array<uint32_t, 4> octets{};
    size_t cursor = 0;
    for (size_t part = 0; part < octets.size(); ++part) {
        const size_t begin = cursor;
        uint32_t value = 0;
        size_t digits = 0;
        while (cursor < input.size() && input[cursor] != '.') {
            const unsigned char character =
                static_cast<unsigned char>(input[cursor]);
            if (character < '0' || character > '9' || digits == 3) {
                error = "KMBox NET IP contains an invalid IPv4 octet";
                return false;
            }
            value = value * 10u + static_cast<uint32_t>(character - '0');
            ++cursor;
            ++digits;
        }
        if (digits == 0 || value > 255u ||
            (digits > 1 && input[begin] == '0')) {
            error = "KMBox NET IP contains an invalid IPv4 octet";
            return false;
        }
        octets[part] = value;
        if (part + 1 < octets.size()) {
            if (cursor >= input.size() || input[cursor] != '.') {
                error = "KMBox NET IP must contain exactly four octets";
                return false;
            }
            ++cursor;
        } else if (cursor != input.size()) {
            error = "KMBox NET IP must contain exactly four octets";
            return false;
        }
    }

    const uint32_t hostOrder = (octets[0] << 24u) | (octets[1] << 16u) |
                               (octets[2] << 8u) | octets[3];
    if (hostOrder == 0u || hostOrder == 0xFFFFFFFFu || octets[0] >= 224u) {
        error = "KMBox NET IP must identify an IPv4 unicast host";
        return false;
    }
    networkOrder = htonl(hostOrder);
    return true;
}

inline bool ParseStrictUuid(std::string_view input, uint32_t& parsed,
                            std::string& error) {
    parsed = 0;
    error.clear();
    if (input.size() != 8) {
        error = "KMBox NET UUID must contain exactly 8 hexadecimal digits";
        return false;
    }
    for (const char raw : input) {
        const unsigned char character = static_cast<unsigned char>(raw);
        uint32_t nibble = 0;
        if (character >= '0' && character <= '9') {
            nibble = static_cast<uint32_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            nibble = static_cast<uint32_t>(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            nibble = static_cast<uint32_t>(character - 'A' + 10);
        } else {
            error = "KMBox NET UUID must contain only hexadecimal digits";
            return false;
        }
        parsed = (parsed << 4u) | nibble;
    }
    if (parsed == 0u) {
        error = "KMBox NET UUID cannot be 00000000";
        return false;
    }
    return true;
}

inline bool NormalizeSerialPort(const std::string& input,
                                std::wstring& normalized,
                                std::string& error) {
    normalized.clear();
    error.clear();

    std::string port = input;
    constexpr std::string_view prefix = "\\\\.\\";
    if (port.size() >= prefix.size() &&
        std::string_view(port.data(), prefix.size()) == prefix) {
        port.erase(0, prefix.size());
    }

    if (port.size() < 4 || port.size() > 7 ||
        (port[0] != 'C' && port[0] != 'c') ||
        (port[1] != 'O' && port[1] != 'o') ||
        (port[2] != 'M' && port[2] != 'm')) {
        error = "Serial port must be COM1 through COM4096";
        return false;
    }

    uint32_t number = 0;
    for (size_t index = 3; index < port.size(); ++index) {
        const unsigned char character =
            static_cast<unsigned char>(port[index]);
        if (character < '0' || character > '9') {
            error = "Serial port contains invalid characters";
            return false;
        }
        number = number * 10 + static_cast<uint32_t>(character - '0');
    }
    if (number < 1 || number > 4096) {
        error = "Serial port must be COM1 through COM4096";
        return false;
    }

    normalized = L"\\\\.\\COM" + std::to_wstring(number);
    return true;
}

inline bool ValidateMoveTemplate(std::string_view commandTemplate,
                                 std::string& error) {
    error.clear();
    if (commandTemplate.empty() || commandTemplate.size() > 256) {
        error = "Generic serial template must contain 1 to 256 bytes";
        return false;
    }

    bool hasX = false;
    bool hasY = false;
    for (size_t index = 0; index < commandTemplate.size();) {
        const unsigned char character =
            static_cast<unsigned char>(commandTemplate[index]);
        if (character == '{') {
            if (commandTemplate.substr(index, 3) == "{x}") {
                hasX = true;
                index += 3;
                continue;
            }
            if (commandTemplate.substr(index, 3) == "{y}") {
                hasY = true;
                index += 3;
                continue;
            }
            error = "Generic serial template contains an unknown placeholder";
            return false;
        }
        if (character == '}') {
            error = "Generic serial template contains an unmatched brace";
            return false;
        }
        if ((character < 0x20 || character > 0x7E) &&
            character != '\r' && character != '\n' && character != '\t') {
            error = "Generic serial template contains a non-ASCII control byte";
            return false;
        }
        ++index;
    }

    if (!hasX || !hasY) {
        error = "Generic serial template must contain both {x} and {y}";
        return false;
    }
    return true;
}

inline bool RenderMoveTemplate(std::string_view commandTemplate,
                               int32_t deltaX, int32_t deltaY,
                               std::string& rendered) {
    rendered.clear();
    rendered.reserve(commandTemplate.size() + 24);
    for (size_t index = 0; index < commandTemplate.size();) {
        if (commandTemplate.substr(index, 3) == "{x}") {
            rendered += std::to_string(deltaX);
            index += 3;
        } else if (commandTemplate.substr(index, 3) == "{y}") {
            rendered += std::to_string(deltaY);
            index += 3;
        } else {
            rendered.push_back(commandTemplate[index]);
            ++index;
        }
        if (rendered.size() > 512) return false;
    }
    return !rendered.empty();
}

inline const AimDeviceDescriptor* FindDescriptor(AimDeviceKind kind) noexcept;

inline AimDeviceConfigValidation ValidateConfig(const AimDeviceConfig& config) {
    const AimDeviceDescriptor* descriptor = FindDescriptor(config.kind);
    if (!descriptor) return {false, "Unknown aim device type"};
    if (config.kind == AimDeviceKind::None) return {true, {}};
    if (!descriptor->capabilities.supported) {
        return {false, descriptor->unsupportedReason
                           ? descriptor->unsupportedReason
                           : "Device adapter is unsupported"};
    }

    std::string error;
    if (config.kind == AimDeviceKind::KMBoxNet) {
        uint32_t networkOrder = 0;
        uint32_t uuid = 0;
        if (!ParseStrictIpv4(config.networkIp, networkOrder, error))
            return {false, std::move(error)};
        if (config.networkPort < 1 || config.networkPort > 65535)
            return {false, "KMBox NET port must be between 1 and 65535"};
        if (!ParseStrictUuid(config.networkUuid, uuid, error))
            return {false, std::move(error)};
        if (config.writeTimeoutMs < 1 || config.writeTimeoutMs > 5000)
            return {false, "Send timeout must be between 1 and 5000 ms"};
        if (config.receiveTimeoutMs < 1 || config.receiveTimeoutMs > 5000)
            return {false, "Receive timeout must be between 1 and 5000 ms"};
        return {true, {}};
    }
    if (config.kind == AimDeviceKind::RawHid) {
        if (config.rawHidVid == 0 || config.rawHidPid == 0)
            return {false, "RawHID VID and PID must both be non-zero"};
        if (config.rawHidAxisBytes != 1 && config.rawHidAxisBytes != 2)
            return {false, "RawHID axis width must be 1 or 2 bytes"};
        if (config.rawHidExpectedReportBytes > 512)
            return {false, "RawHID report length cannot exceed 512 bytes"};
        if (config.rawHidReportTemplate.size() > 512)
            return {false, "RawHID report template cannot exceed 512 bytes"};
        if (config.rawHidExpectedReportBytes != 0 &&
            config.rawHidReportTemplate.size() >
                config.rawHidExpectedReportBytes) {
            return {false, "RawHID report template exceeds expected report length"};
        }
        const uint32_t axisBytes = config.rawHidAxisBytes;
        if (config.rawHidXOffset == 0 || config.rawHidYOffset == 0 ||
            static_cast<uint32_t>(config.rawHidXOffset) + axisBytes > 512 ||
            static_cast<uint32_t>(config.rawHidYOffset) + axisBytes > 512) {
            return {false, "RawHID X/Y fields must fit after the report ID"};
        }
        const uint32_t xBegin = config.rawHidXOffset;
        const uint32_t xEnd = xBegin + axisBytes;
        const uint32_t yBegin = config.rawHidYOffset;
        const uint32_t yEnd = yBegin + axisBytes;
        if (xBegin < yEnd && yBegin < xEnd)
            return {false, "RawHID X and Y fields cannot overlap"};
        if (config.rawHidButtonsOffset != 0xFFFFu &&
            (config.rawHidButtonsOffset == 0 ||
             config.rawHidButtonsOffset >= 512)) {
            return {false, "RawHID buttons offset is outside the report"};
        }
        if (config.rawHidTransferMode != RawHidTransferMode::InterruptOut &&
            config.rawHidTransferMode != RawHidTransferMode::OutputReport &&
            config.rawHidTransferMode != RawHidTransferMode::FeatureReport) {
            return {false, "RawHID transfer mode is invalid"};
        }
        if (config.writeTimeoutMs < 1 || config.writeTimeoutMs > 5000)
            return {false, "RawHID write timeout must be between 1 and 5000 ms"};
        return {true, {}};
    }

    std::wstring normalizedPort;
    if (!NormalizeSerialPort(config.serialPort, normalizedPort, error))
        return {false, std::move(error)};
    if (!IsSupportedBaudRate(config.baudRate))
        return {false, "Baud rate must be between 1200 and 4000000"};
    if (config.writeTimeoutMs < 1 || config.writeTimeoutMs > 5000)
        return {false, "Write timeout must be between 1 and 5000 ms"};
    if (config.receiveTimeoutMs < 1 || config.receiveTimeoutMs > 5000)
        return {false, "Receive timeout must be between 1 and 5000 ms"};

    if ((config.kind == AimDeviceKind::KMBoxSerial ||
         config.kind == AimDeviceKind::CH9329) &&
        config.baudRate != 115200) {
        return {false, "This adapter requires 115200 baud"};
    }
    if (config.kind == AimDeviceKind::Lurker &&
        config.baudRate != 57600) {
        return {false, "Lurker requires 57600 baud"};
    }
    if (config.kind == AimDeviceKind::Makcu &&
        config.baudRate != 115200 && config.baudRate != 4000000) {
        return {false, "MAKCU requires 115200 or 4000000 baud"};
    }
    if (config.kind == AimDeviceKind::GenericSerial &&
        !ValidateMoveTemplate(config.genericMoveTemplate, error)) {
        return {false, std::move(error)};
    }
    return {true, {}};
}

class SerialAimDeviceBase : public IAimDevice {
public:
    ~SerialAimDeviceBase() override { Disconnect(); }

    bool Connect(const AimDeviceConfig& config) final {
        std::lock_guard<std::mutex> lock(mutex_);
        CloseLocked();
        status_ = {};
        status_.state = AimDeviceConnectionState::Connecting;
        status_.message = "Connecting";
        status_.lastTransitionTick = GetTickCount64();

        if (config.kind != Kind()) {
            SetErrorLocked(ERROR_INVALID_PARAMETER,
                           "Configuration does not match device type", false);
            return false;
        }
        const AimDeviceConfigValidation validation = ValidateConfig(config);
        if (!validation.valid) {
            SetErrorLocked(ERROR_INVALID_PARAMETER, validation.error, false);
            return false;
        }

        std::wstring portPath;
        std::string portError;
        if (!NormalizeSerialPort(config.serialPort, portPath, portError)) {
            SetErrorLocked(ERROR_INVALID_PARAMETER, portError, false);
            return false;
        }

        HANDLE handle = CreateFileW(portPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                    0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            SetErrorLocked(error, "Opening serial port failed", false);
            return false;
        }

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle, &dcb)) {
            const DWORD error = GetLastError();
            CloseHandle(handle);
            SetErrorLocked(error, "Reading serial configuration failed", false);
            return false;
        }
        dcb.BaudRate = config.baudRate;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fAbortOnError = FALSE;
        dcb.fDtrControl = config.enableDtr
                              ? DTR_CONTROL_ENABLE
                              : DTR_CONTROL_DISABLE;
        dcb.fRtsControl = config.enableRts
                              ? RTS_CONTROL_ENABLE
                              : RTS_CONTROL_DISABLE;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        if (!SetCommState(handle, &dcb)) {
            const DWORD error = GetLastError();
            CloseHandle(handle);
            SetErrorLocked(error, "Applying serial configuration failed", false);
            return false;
        }

        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = config.receiveTimeoutMs;
        timeouts.WriteTotalTimeoutConstant = config.writeTimeoutMs;
        if (!SetCommTimeouts(handle, &timeouts)) {
            const DWORD error = GetLastError();
            CloseHandle(handle);
            SetErrorLocked(error, "Applying serial timeouts failed", false);
            return false;
        }

        SetupComm(handle, 1024, 1024);
        PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR |
                          PURGE_TXABORT | PURGE_TXCLEAR);
        handle_ = handle;
        config_ = config;
        if (!InitializeLocked()) {
            if (status_.state != AimDeviceConnectionState::Error) {
                SetErrorLocked(ERROR_GEN_FAILURE,
                               "Serial device initialization failed", true);
            }
            return false;
        }
        status_.state = AimDeviceConnectionState::Connected;
        status_.lastError = ERROR_SUCCESS;
        status_.message = "Connected";
        status_.lastTransitionTick = GetTickCount64();
        return true;
    }

    void Disconnect() noexcept final {
        std::lock_guard<std::mutex> lock(mutex_);
        CloseLocked();
        status_.state = AimDeviceConnectionState::Disconnected;
        status_.lastError = ERROR_SUCCESS;
        status_.message = "Disconnected";
        status_.lastTransitionTick = GetTickCount64();
    }

    AimDeviceStatus Status() const final {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

    bool Test(int32_t distance = 8) final {
        const AimDeviceCapabilities capabilities = Capabilities();
        if (distance <= 0 || distance > capabilities.maximumDelta) {
            std::lock_guard<std::mutex> lock(mutex_);
            RejectLocked(ERROR_INVALID_PARAMETER,
                         "Test distance is outside device limits");
            return false;
        }
        if (!Move(distance, 0)) return false;
        Sleep(30);
        return Move(-distance, 0);
    }

protected:
    virtual bool InitializeLocked() { return true; }

    bool BeginMoveLocked(int32_t deltaX, int32_t deltaY) {
        if (handle_ == INVALID_HANDLE_VALUE ||
            status_.state != AimDeviceConnectionState::Connected) {
            RejectLocked(ERROR_INVALID_HANDLE, "Device is not connected");
            return false;
        }
        const AimDeviceCapabilities capabilities = Capabilities();
        if (deltaX < capabilities.minimumDelta ||
            deltaX > capabilities.maximumDelta ||
            deltaY < capabilities.minimumDelta ||
            deltaY > capabilities.maximumDelta) {
            RejectLocked(ERROR_INVALID_PARAMETER,
                         "Relative movement is outside device limits");
            return false;
        }
        return true;
    }

    bool WriteLocked(const uint8_t* data, size_t size,
                     std::string_view operation = "Writing to serial device") {
        if (!data || size == 0 || size > 512) {
            RejectLocked(ERROR_INVALID_PARAMETER, "Invalid serial packet");
            return false;
        }
        if (handle_ == INVALID_HANDLE_VALUE) {
            RejectLocked(ERROR_INVALID_HANDLE, "Device is not connected");
            return false;
        }

        size_t total = 0;
        while (total < size) {
            DWORD written = 0;
            const DWORD remaining = static_cast<DWORD>(size - total);
            if (!WriteFile(handle_, data + total, remaining, &written, nullptr)) {
                const DWORD error = GetLastError();
                SetErrorLocked(error, std::string(operation) + " failed", true);
                return false;
            }
            if (written == 0 || written > remaining) {
                SetErrorLocked(ERROR_WRITE_FAULT,
                               std::string(operation) +
                                   " accepted an invalid byte count", true);
                return false;
            }
            total += written;
        }

        ++status_.commandsSent;
        status_.bytesSent += total;
        status_.lastError = ERROR_SUCCESS;
        status_.message = "Connected";
        return true;
    }

    bool ReadUntilLocked(std::string_view expected,
                         std::string& response,
                         std::string_view operation) {
        response.clear();
        if (handle_ == INVALID_HANDLE_VALUE || expected.empty()) {
            SetErrorLocked(ERROR_INVALID_HANDLE,
                           std::string(operation) +
                               " cannot use a closed serial port", true);
            return false;
        }

        const ULONGLONG deadline =
            GetTickCount64() + static_cast<ULONGLONG>(config_.receiveTimeoutMs);
        std::array<char, 128> buffer{};
        while (response.size() < 512) {
            DWORD received = 0;
            if (!ReadFile(handle_, buffer.data(),
                          static_cast<DWORD>(buffer.size()),
                          &received, nullptr)) {
                const DWORD error = GetLastError();
                SetErrorLocked(error, std::string(operation) + " failed", true);
                return false;
            }
            if (received != 0) {
                response.append(buffer.data(), received);
                if (response.find(expected) != std::string::npos) return true;
            }
            if (GetTickCount64() >= deadline) break;
            if (received == 0) Sleep(1);
        }

        SetErrorLocked(WAIT_TIMEOUT,
                       std::string(operation) +
                           " timed out before marker '" +
                           std::string(expected) + "'", true);
        return false;
    }

    void RejectLocked(DWORD error, std::string message) {
        status_.lastError = error;
        status_.message = std::move(message);
    }

    mutable std::mutex mutex_;
    AimDeviceConfig config_{};

private:
    void CloseLocked() noexcept {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    void SetErrorLocked(DWORD error, std::string message,
                        bool closeHandle) noexcept {
        if (closeHandle) CloseLocked();
        status_.state = AimDeviceConnectionState::Error;
        status_.lastError = error;
        status_.message = std::move(message);
        status_.lastTransitionTick = GetTickCount64();
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    AimDeviceStatus status_{};
};

class KMBoxSerialDevice final : public SerialAimDeviceBase {
public:
    AimDeviceKind Kind() const noexcept override {
        return AimDeviceKind::KMBoxSerial;
    }

    AimDeviceCapabilities Capabilities() const noexcept override {
        return TextSerialCapabilities();
    }

    bool Move(int32_t deltaX, int32_t deltaY) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!BeginMoveLocked(deltaX, deltaY)) return false;
        if (deltaX == 0 && deltaY == 0) return true;
        const std::string command = "km.move(" + std::to_string(deltaX) +
                                    "," + std::to_string(deltaY) + ")\r\n";
        return WriteLocked(reinterpret_cast<const uint8_t*>(command.data()),
                           command.size());
    }

protected:
    bool InitializeLocked() override {
        constexpr std::array<uint8_t, 1> interrupt{{0x03}};
        if (!WriteLocked(interrupt.data(), interrupt.size(),
                         "Writing KMBox initialization Ctrl-C")) {
            return false;
        }
        Sleep(10);
        constexpr char frequency[] = "km.freq(1000)\r\n";
        return WriteLocked(
            reinterpret_cast<const uint8_t*>(frequency),
            sizeof(frequency) - 1,
            "Writing KMBox 1000 Hz initialization command");
    }
};

class MakcuDevice final : public SerialAimDeviceBase {
public:
    AimDeviceKind Kind() const noexcept override {
        return AimDeviceKind::Makcu;
    }

    AimDeviceCapabilities Capabilities() const noexcept override {
        return TextSerialCapabilities();
    }

    bool Move(int32_t deltaX, int32_t deltaY) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!BeginMoveLocked(deltaX, deltaY)) return false;
        if (deltaX == 0 && deltaY == 0) return true;
        const std::string command = "km.move(" + std::to_string(deltaX) +
                                    "," + std::to_string(deltaY) + ")\r\n";
        return WriteLocked(reinterpret_cast<const uint8_t*>(command.data()),
                           command.size(), "Writing MAKCU movement command");
    }

protected:
    bool InitializeLocked() override {
        constexpr char versionQuery[] = "km.version()\r\n";
        if (!WriteLocked(
                reinterpret_cast<const uint8_t*>(versionQuery),
                sizeof(versionQuery) - 1,
                "Writing MAKCU version query")) {
            return false;
        }
        std::string response;
        return ReadUntilLocked("km.MAKCU", response,
                               "Reading MAKCU version response");
    }
};

class CH9329Device final : public SerialAimDeviceBase {
public:
    AimDeviceKind Kind() const noexcept override {
        return AimDeviceKind::CH9329;
    }

    AimDeviceCapabilities Capabilities() const noexcept override {
        return CH9329Capabilities();
    }

    bool Move(int32_t deltaX, int32_t deltaY) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!BeginMoveLocked(deltaX, deltaY)) return false;
        if (deltaX == 0 && deltaY == 0) return true;

        // CH9329 CMD_SEND_MS_REL: header, address, command, payload length,
        // report id, buttons, signed X, signed Y, wheel, additive checksum.
        std::array<uint8_t, 11> packet{
            0x57, 0xAB, 0x00, 0x05, 0x05, 0x01, 0x00,
            static_cast<uint8_t>(static_cast<int8_t>(deltaX)),
            static_cast<uint8_t>(static_cast<int8_t>(deltaY)),
            0x00, 0x00};
        uint32_t checksum = 0;
        for (size_t index = 0; index + 1 < packet.size(); ++index)
            checksum += packet[index];
        packet.back() = static_cast<uint8_t>(checksum & 0xFFu);
        return WriteLocked(packet.data(), packet.size());
    }
};

class LurkerDevice final : public SerialAimDeviceBase {
public:
    AimDeviceKind Kind() const noexcept override {
        return AimDeviceKind::Lurker;
    }

    AimDeviceCapabilities Capabilities() const noexcept override {
        return LurkerCapabilities();
    }

    bool Move(int32_t deltaX, int32_t deltaY) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!BeginMoveLocked(deltaX, deltaY)) return false;
        if (deltaX == 0 && deltaY == 0) return true;

        const std::array<uint8_t, 7> packet{
            0x57, 0xAB, 0x02, 0x00,
            static_cast<uint8_t>(static_cast<int8_t>(deltaX)),
            static_cast<uint8_t>(static_cast<int8_t>(deltaY)),
            0x00};
        return WriteLocked(packet.data(), packet.size());
    }
};

class GenericSerialDevice final : public SerialAimDeviceBase {
public:
    AimDeviceKind Kind() const noexcept override {
        return AimDeviceKind::GenericSerial;
    }

    AimDeviceCapabilities Capabilities() const noexcept override {
        return TextSerialCapabilities();
    }

    bool Move(int32_t deltaX, int32_t deltaY) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!BeginMoveLocked(deltaX, deltaY)) return false;
        if (deltaX == 0 && deltaY == 0) return true;

        std::string command;
        if (!RenderMoveTemplate(config_.genericMoveTemplate,
                                deltaX, deltaY, command)) {
            RejectLocked(ERROR_INVALID_DATA,
                         "Rendering generic serial command failed");
            return false;
        }
        return WriteLocked(reinterpret_cast<const uint8_t*>(command.data()),
                           command.size());
    }
};

class KMBoxNetDevice final : public IAimDevice {
public:
    KMBoxNetDevice() = default;
    ~KMBoxNetDevice() override;

    AimDeviceKind Kind() const noexcept override;
    bool Connect(const AimDeviceConfig& config) override;
    void Disconnect() noexcept override;
    bool Move(int32_t deltaX, int32_t deltaY) override;
    bool Test(int32_t distance = 8) override;
    AimDeviceStatus Status() const override;
    AimDeviceCapabilities Capabilities() const noexcept override;

private:
    struct CommandHeader {
        uint32_t mac;
        uint32_t value;
        uint32_t sequence;
        uint32_t command;
    };

    struct SoftMouse {
        int32_t button;
        int32_t x;
        int32_t y;
        int32_t wheel;
        std::array<int32_t, 10> points;
    };

    static_assert(sizeof(CommandHeader) == 16,
                  "KMBox NET header wire size must be 16 bytes");
    static_assert(sizeof(SoftMouse) == 56,
                  "KMBox NET mouse wire size must be 56 bytes");

    static constexpr uint32_t kCommandConnect = 0xAF3C2828u;
    static constexpr uint32_t kCommandMouseMove = 0xAEDE7345u;

    bool BeginMoveLocked(int32_t deltaX, int32_t deltaY);
    bool ExchangeLocked(uint32_t command, uint32_t sequence, uint32_t value,
                        const void* payload, size_t payloadSize,
                        DWORD& error, std::string& message,
                        uint32_t timeoutMs);
    bool SetReceiveTimeoutLocked(uint32_t timeoutMs, DWORD& error);
    uint32_t NextRandomLocked() noexcept;
    void CloseLocked() noexcept;
    void RejectLocked(DWORD error, std::string message);
    void SetErrorLocked(DWORD error, std::string message,
                        bool closeTransport) noexcept;

    mutable std::mutex mutex_;
    AimDeviceConfig config_{};
    AimDeviceStatus status_{};
    SOCKET socket_ = INVALID_SOCKET;
    SOCKADDR_IN remote_{};
    bool wsaStarted_ = false;
    uint32_t uuid_ = 0;
    uint32_t sequence_ = 0;
    uint32_t randomState_ = 0;
};

inline KMBoxNetDevice::~KMBoxNetDevice() {
    Disconnect();
}

inline AimDeviceKind KMBoxNetDevice::Kind() const noexcept {
    return AimDeviceKind::KMBoxNet;
}

inline AimDeviceCapabilities KMBoxNetDevice::Capabilities() const noexcept {
    return KMBoxNetCapabilities();
}

inline bool KMBoxNetDevice::Connect(const AimDeviceConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    CloseLocked();
    status_ = {};
    status_.state = AimDeviceConnectionState::Connecting;
    status_.message = "Connecting";
    status_.lastTransitionTick = GetTickCount64();

    if (config.kind != Kind()) {
        SetErrorLocked(ERROR_INVALID_PARAMETER,
                       "Configuration does not match device type", false);
        return false;
    }
    const AimDeviceConfigValidation validation = ValidateConfig(config);
    if (!validation.valid) {
        SetErrorLocked(ERROR_INVALID_PARAMETER, validation.error, false);
        return false;
    }

    uint32_t networkAddress = 0;
    uint32_t parsedUuid = 0;
    std::string parseError;
    if (!ParseStrictIpv4(config.networkIp, networkAddress, parseError) ||
        !ParseStrictUuid(config.networkUuid, parsedUuid, parseError)) {
        SetErrorLocked(ERROR_INVALID_PARAMETER, parseError, false);
        return false;
    }

    WSADATA wsaData{};
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupResult != 0) {
        SetErrorLocked(static_cast<DWORD>(startupResult),
                       "WSAStartup failed", false);
        return false;
    }
    wsaStarted_ = true;
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        SetErrorLocked(WSAVERNOTSUPPORTED,
                       "Winsock 2.2 is not available", true);
        return false;
    }

    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        const DWORD error = static_cast<DWORD>(WSAGetLastError());
        SetErrorLocked(error, "Creating KMBox NET UDP socket failed", true);
        return false;
    }

    const DWORD sendTimeout = config.writeTimeoutMs;
    if (::setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char*>(&sendTimeout),
                     sizeof(sendTimeout)) == SOCKET_ERROR) {
        const DWORD error = static_cast<DWORD>(WSAGetLastError());
        SetErrorLocked(error, "Applying UDP send timeout failed", true);
        return false;
    }
    DWORD timeoutError = ERROR_SUCCESS;
    if (!SetReceiveTimeoutLocked(config.receiveTimeoutMs, timeoutError)) {
        SetErrorLocked(timeoutError, "Applying UDP receive timeout failed", true);
        return false;
    }

    remote_ = {};
    remote_.sin_family = AF_INET;
    remote_.sin_port = htons(static_cast<u_short>(config.networkPort));
    remote_.sin_addr.s_addr = networkAddress;
    config_ = config;
    uuid_ = parsedUuid;
    sequence_ = 0;

    const ULONGLONG tick = GetTickCount64();
    randomState_ = static_cast<uint32_t>(tick) ^
                   static_cast<uint32_t>(tick >> 32u) ^
                   static_cast<uint32_t>(GetCurrentProcessId()) ^
                   static_cast<uint32_t>(
                       reinterpret_cast<uintptr_t>(this));
    if (randomState_ == 0u) randomState_ = 0x9E3779B9u;

    DWORD exchangeError = ERROR_SUCCESS;
    std::string exchangeMessage;
    Sleep(20);
    if (!ExchangeLocked(kCommandConnect, sequence_, NextRandomLocked(),
                        nullptr, 0, exchangeError, exchangeMessage,
                        (std::max)(config.receiveTimeoutMs, 3000u))) {
        SetErrorLocked(exchangeError, std::move(exchangeMessage), true);
        return false;
    }

    status_.state = AimDeviceConnectionState::Connected;
    status_.lastError = ERROR_SUCCESS;
    status_.message = "Connected";
    status_.lastTransitionTick = GetTickCount64();
    return true;
}

inline void KMBoxNetDevice::Disconnect() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    CloseLocked();
    status_.state = AimDeviceConnectionState::Disconnected;
    status_.lastError = ERROR_SUCCESS;
    status_.message = "Disconnected";
    status_.lastTransitionTick = GetTickCount64();
}

inline bool KMBoxNetDevice::Move(int32_t deltaX, int32_t deltaY) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!BeginMoveLocked(deltaX, deltaY)) return false;
    if (deltaX == 0 && deltaY == 0) return true;

    SoftMouse mouse{};
    mouse.x = deltaX;
    mouse.y = deltaY;
    const uint32_t sequence = ++sequence_;
    DWORD error = ERROR_SUCCESS;
    std::string message;
    if (!ExchangeLocked(kCommandMouseMove, sequence, NextRandomLocked(),
                        &mouse, sizeof(mouse), error, message,
                        config_.receiveTimeoutMs)) {
        if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK)
            RejectLocked(error, std::move(message));
        else
            SetErrorLocked(error, std::move(message), false);
        return false;
    }
    status_.lastError = ERROR_SUCCESS;
    status_.message = "Connected";
    return true;
}

inline bool KMBoxNetDevice::Test(int32_t distance) {
    const AimDeviceCapabilities capabilities = Capabilities();
    if (distance <= 0 || distance > capabilities.maximumDelta) {
        std::lock_guard<std::mutex> lock(mutex_);
        RejectLocked(ERROR_INVALID_PARAMETER,
                     "Test distance is outside device limits");
        return false;
    }
    if (!Move(distance, 0)) return false;
    Sleep(30);
    return Move(-distance, 0);
}

inline AimDeviceStatus KMBoxNetDevice::Status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

inline bool KMBoxNetDevice::BeginMoveLocked(
    int32_t deltaX, int32_t deltaY) {
    if (socket_ == INVALID_SOCKET ||
        status_.state != AimDeviceConnectionState::Connected) {
        RejectLocked(ERROR_INVALID_HANDLE, "Device is not connected");
        return false;
    }
    const AimDeviceCapabilities capabilities = Capabilities();
    if (deltaX < capabilities.minimumDelta ||
        deltaX > capabilities.maximumDelta ||
        deltaY < capabilities.minimumDelta ||
        deltaY > capabilities.maximumDelta) {
        RejectLocked(ERROR_INVALID_PARAMETER,
                     "Relative movement is outside device limits");
        return false;
    }
    return true;
}

inline bool KMBoxNetDevice::ExchangeLocked(
    uint32_t command, uint32_t sequence, uint32_t value,
    const void* payload, size_t payloadSize,
    DWORD& error, std::string& message, uint32_t timeoutMs) {
    error = ERROR_SUCCESS;
    message.clear();
    if (socket_ == INVALID_SOCKET || payloadSize > sizeof(SoftMouse) ||
        (payloadSize != 0 && !payload)) {
        error = ERROR_INVALID_PARAMETER;
        message = "Invalid KMBox NET request";
        return false;
    }

    std::array<uint8_t, sizeof(CommandHeader) + sizeof(SoftMouse)> packet{};
    CommandHeader header{uuid_, value, sequence, command};
    std::memcpy(packet.data(), &header, sizeof(header));
    if (payloadSize)
        std::memcpy(packet.data() + sizeof(header), payload, payloadSize);
    const int packetSize = static_cast<int>(sizeof(header) + payloadSize);
    const int sent = ::sendto(
        socket_, reinterpret_cast<const char*>(packet.data()), packetSize, 0,
        reinterpret_cast<const SOCKADDR*>(&remote_), sizeof(remote_));
    if (sent != packetSize) {
        error = sent == SOCKET_ERROR
            ? static_cast<DWORD>(WSAGetLastError())
            : ERROR_WRITE_FAULT;
        message = "Sending KMBox NET request failed";
        return false;
    }

    timeoutMs = (std::clamp)(timeoutMs, 1u, 5000u);
    const ULONGLONG deadline =
        GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            error = WSAETIMEDOUT;
            message = "KMBox NET response timed out";
            return false;
        }
        const uint32_t remaining = static_cast<uint32_t>(
            (std::max)(1ull, deadline - now));
        if (!SetReceiveTimeoutLocked(remaining, error)) {
            message = "Applying KMBox NET receive timeout failed";
            return false;
        }

        CommandHeader response{};
        SOCKADDR_IN source{};
        int sourceSize = sizeof(source);
        const int received = ::recvfrom(
            socket_, reinterpret_cast<char*>(&response),
            sizeof(response), 0,
            reinterpret_cast<SOCKADDR*>(&source), &sourceSize);
        if (received == SOCKET_ERROR) {
            error = static_cast<DWORD>(WSAGetLastError());
            message = (error == WSAETIMEDOUT ||
                       error == WSAEWOULDBLOCK)
                ? "KMBox NET response timed out"
                : "Receiving KMBox NET response failed";
            return false;
        }
        if (received < static_cast<int>(sizeof(response)))
            continue;
        if (source.sin_addr.s_addr != remote_.sin_addr.s_addr ||
            source.sin_port != remote_.sin_port) {
            continue;
        }
        // Official firmware treats mac/value as opaque request fields and
        // validates only command plus packet sequence in its reply path.
        if (response.command != command ||
            response.sequence != sequence) {
            continue;
        }
        break;
    }

    ++status_.commandsSent;
    status_.bytesSent += static_cast<uint64_t>(sent);
    return true;
}

inline bool KMBoxNetDevice::SetReceiveTimeoutLocked(
    uint32_t timeoutMs, DWORD& error) {
    const DWORD timeout = timeoutMs;
    if (::setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout),
            sizeof(timeout)) == SOCKET_ERROR) {
        error = static_cast<DWORD>(WSAGetLastError());
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

inline uint32_t KMBoxNetDevice::NextRandomLocked() noexcept {
    uint32_t value = randomState_;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    randomState_ = value ? value : 0xA341316Cu;
    return randomState_;
}

inline void KMBoxNetDevice::CloseLocked() noexcept {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    if (wsaStarted_) {
        WSACleanup();
        wsaStarted_ = false;
    }
    remote_ = {};
    uuid_ = 0;
    sequence_ = 0;
}

inline void KMBoxNetDevice::RejectLocked(
    DWORD error, std::string message) {
    status_.lastError = error;
    status_.message = std::move(message);
}

inline void KMBoxNetDevice::SetErrorLocked(
    DWORD error, std::string message, bool closeTransport) noexcept {
    if (closeTransport) CloseLocked();
    status_.state = AimDeviceConnectionState::Error;
    status_.lastError = error;
    status_.message = std::move(message);
    status_.lastTransitionTick = GetTickCount64();
}

class RawHidDevice final : public IAimDevice {
public:
    ~RawHidDevice() override { Disconnect(); }

    AimDeviceKind Kind() const noexcept override {
        return AimDeviceKind::RawHid;
    }

    AimDeviceCapabilities Capabilities() const noexcept override {
        return RawHidCapabilities();
    }

    bool Connect(const AimDeviceConfig& config) override {
        std::lock_guard<std::mutex> lock(mutex_);
        CloseLocked();
        status_ = {};
        status_.state = AimDeviceConnectionState::Connecting;
        status_.message = "Connecting";
        status_.lastTransitionTick = GetTickCount64();

        if (config.kind != Kind()) {
            SetErrorLocked(ERROR_INVALID_PARAMETER,
                           "Configuration does not match device type", false);
            return false;
        }
        const AimDeviceConfigValidation validation = ValidateConfig(config);
        if (!validation.valid) {
            SetErrorLocked(ERROR_INVALID_PARAMETER, validation.error, false);
            return false;
        }

        GUID hidGuid{};
        HidD_GetHidGuid(&hidGuid);
        HDEVINFO deviceInfo = SetupDiGetClassDevsW(
            &hidGuid, nullptr, nullptr,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (deviceInfo == INVALID_HANDLE_VALUE) {
            SetErrorLocked(GetLastError(),
                           "Enumerating RawHID interfaces failed", false);
            return false;
        }

        HANDLE selectedHandle = INVALID_HANDLE_VALUE;
        size_t selectedReportBytes = 0;
        DWORD enumerationError = ERROR_SUCCESS;
        DWORD openError = ERROR_SUCCESS;
        bool foundVidPid = false;
        bool foundCompatibleLayout = false;
        uint32_t compatibleIndex = 0;

        for (DWORD index = 0;; ++index) {
            SP_DEVICE_INTERFACE_DATA interfaceData{};
            interfaceData.cbSize = sizeof(interfaceData);
            if (!SetupDiEnumDeviceInterfaces(
                    deviceInfo, nullptr, &hidGuid, index, &interfaceData)) {
                const DWORD error = GetLastError();
                if (error != ERROR_NO_MORE_ITEMS) enumerationError = error;
                break;
            }

            DWORD requiredBytes = 0;
            SetupDiGetDeviceInterfaceDetailW(
                deviceInfo, &interfaceData, nullptr, 0,
                &requiredBytes, nullptr);
            if (requiredBytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) ||
                GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                continue;
            }

            auto detailStorage = std::make_unique<uint8_t[]>(requiredBytes);
            auto* detail = reinterpret_cast<
                SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailStorage.get());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(
                    deviceInfo, &interfaceData, detail, requiredBytes,
                    nullptr, nullptr)) {
                continue;
            }

            HANDLE probe = CreateFileW(
                detail->DevicePath, 0,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (probe == INVALID_HANDLE_VALUE) continue;

            HIDD_ATTRIBUTES attributes{};
            attributes.Size = sizeof(attributes);
            if (!HidD_GetAttributes(probe, &attributes) ||
                attributes.VendorID != config.rawHidVid ||
                attributes.ProductID != config.rawHidPid) {
                CloseHandle(probe);
                continue;
            }
            foundVidPid = true;

            PHIDP_PREPARSED_DATA preparsed = nullptr;
            HIDP_CAPS caps{};
            bool capsValid = false;
            if (HidD_GetPreparsedData(probe, &preparsed)) {
                capsValid =
                    HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS;
                HidD_FreePreparsedData(preparsed);
            }
            CloseHandle(probe);
            if (!capsValid) continue;
            if (config.rawHidUsagePage != 0 &&
                caps.UsagePage != config.rawHidUsagePage) {
                continue;
            }
            if (config.rawHidUsage != 0 &&
                caps.Usage != config.rawHidUsage) {
                continue;
            }

            const size_t reportBytes = ReportBytesForCaps(
                caps, config.rawHidTransferMode);
            if (!LayoutFits(config, reportBytes)) continue;
            foundCompatibleLayout = true;
            if (compatibleIndex++ != config.rawHidInterfaceIndex) continue;

            const DWORD flags =
                config.rawHidTransferMode ==
                        RawHidTransferMode::InterruptOut
                    ? FILE_FLAG_OVERLAPPED
                    : FILE_ATTRIBUTE_NORMAL;
            selectedHandle = CreateFileW(
                detail->DevicePath, GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, flags, nullptr);
            if (selectedHandle == INVALID_HANDLE_VALUE) {
                openError = GetLastError();
                break;
            }
            selectedReportBytes = reportBytes;
            break;
        }
        SetupDiDestroyDeviceInfoList(deviceInfo);

        if (selectedHandle == INVALID_HANDLE_VALUE) {
            if (openError != ERROR_SUCCESS) {
                SetErrorLocked(openError,
                               "Opening RawHID output interface failed", false);
            } else if (enumerationError != ERROR_SUCCESS) {
                SetErrorLocked(enumerationError,
                               "Enumerating RawHID interfaces failed", false);
            } else if (!foundVidPid) {
                SetErrorLocked(ERROR_NOT_FOUND,
                               "RawHID VID/PID was not found", false);
            } else if (!foundCompatibleLayout) {
                SetErrorLocked(
                    ERROR_INVALID_DATA,
                    "RawHID device was found but usage or report layout is incompatible",
                    false);
            } else {
                SetErrorLocked(
                    ERROR_NOT_FOUND,
                    "RawHID compatible interface index was not found", false);
            }
            return false;
        }

        handle_ = selectedHandle;
        reportBytes_ = selectedReportBytes;
        config_ = config;
        status_.state = AimDeviceConnectionState::Connected;
        status_.lastError = ERROR_SUCCESS;
        status_.message = "Connected";
        status_.lastTransitionTick = GetTickCount64();
        return true;
    }

    void Disconnect() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        CloseLocked();
        status_.state = AimDeviceConnectionState::Disconnected;
        status_.lastError = ERROR_SUCCESS;
        status_.message = "Disconnected";
        status_.lastTransitionTick = GetTickCount64();
    }

    bool Move(int32_t deltaX, int32_t deltaY) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!BeginMoveLocked(deltaX, deltaY)) return false;
        if (deltaX == 0 && deltaY == 0) return true;

        std::array<uint8_t, 512> report{};
        if (!config_.rawHidReportTemplate.empty()) {
            std::memcpy(report.data(),
                        config_.rawHidReportTemplate.data(),
                        config_.rawHidReportTemplate.size());
        }
        report[0] = config_.rawHidReportId;
        if (config_.rawHidButtonsOffset != 0xFFFFu)
            report[config_.rawHidButtonsOffset] = 0;
        EncodeAxis(report.data() + config_.rawHidXOffset, deltaX);
        EncodeAxis(report.data() + config_.rawHidYOffset, deltaY);
        return SendReportLocked(report.data());
    }

    bool Test(int32_t distance = 8) override {
        if (distance <= 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            RejectLocked(ERROR_INVALID_PARAMETER,
                         "Test distance must be positive");
            return false;
        }
        if (!Move(distance, 0)) return false;
        Sleep(30);
        return Move(-distance, 0);
    }

    AimDeviceStatus Status() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

private:
    static size_t ReportBytesForCaps(
        const HIDP_CAPS& caps, RawHidTransferMode mode) noexcept {
        if (mode == RawHidTransferMode::FeatureReport)
            return caps.FeatureReportByteLength;
        return caps.OutputReportByteLength;
    }

    static bool LayoutFits(
        const AimDeviceConfig& config, size_t reportBytes) noexcept {
        if (reportBytes == 0 || reportBytes > 512) return false;
        if (config.rawHidExpectedReportBytes != 0 &&
            config.rawHidExpectedReportBytes != reportBytes) {
            return false;
        }
        if (config.rawHidReportTemplate.size() > reportBytes) return false;
        const size_t axisBytes = config.rawHidAxisBytes;
        if (static_cast<size_t>(config.rawHidXOffset) + axisBytes >
                reportBytes ||
            static_cast<size_t>(config.rawHidYOffset) + axisBytes >
                reportBytes) {
            return false;
        }
        if (config.rawHidButtonsOffset != 0xFFFFu) {
            const size_t button = config.rawHidButtonsOffset;
            if (button >= reportBytes) return false;
            const size_t xBegin = config.rawHidXOffset;
            const size_t yBegin = config.rawHidYOffset;
            if ((button >= xBegin && button < xBegin + axisBytes) ||
                (button >= yBegin && button < yBegin + axisBytes)) {
                return false;
            }
        }
        return true;
    }

    bool BeginMoveLocked(int32_t deltaX, int32_t deltaY) {
        if (handle_ == INVALID_HANDLE_VALUE ||
            status_.state != AimDeviceConnectionState::Connected) {
            RejectLocked(ERROR_INVALID_HANDLE, "Device is not connected");
            return false;
        }
        const int32_t limit =
            config_.rawHidAxisBytes == 1 ? 127 : 32767;
        if (deltaX < -limit || deltaX > limit ||
            deltaY < -limit || deltaY > limit) {
            RejectLocked(ERROR_INVALID_PARAMETER,
                         "Relative movement exceeds RawHID axis width");
            return false;
        }
        return true;
    }

    void EncodeAxis(uint8_t* destination, int32_t value) const noexcept {
        if (config_.rawHidAxisBytes == 1) {
            destination[0] =
                static_cast<uint8_t>(static_cast<int8_t>(value));
            return;
        }
        const uint16_t encoded =
            static_cast<uint16_t>(static_cast<int16_t>(value));
        if (config_.rawHidLittleEndian) {
            destination[0] = static_cast<uint8_t>(encoded & 0xFFu);
            destination[1] = static_cast<uint8_t>(encoded >> 8u);
        } else {
            destination[0] = static_cast<uint8_t>(encoded >> 8u);
            destination[1] = static_cast<uint8_t>(encoded & 0xFFu);
        }
    }

    bool SendReportLocked(const uint8_t* report) {
        DWORD error = ERROR_SUCCESS;
        bool success = false;
        if (config_.rawHidTransferMode ==
            RawHidTransferMode::OutputReport) {
            success = HidD_SetOutputReport(
                          handle_, const_cast<uint8_t*>(report),
                          static_cast<ULONG>(reportBytes_)) != FALSE;
            if (!success) error = GetLastError();
        } else if (config_.rawHidTransferMode ==
                   RawHidTransferMode::FeatureReport) {
            success = HidD_SetFeature(
                          handle_, const_cast<uint8_t*>(report),
                          static_cast<ULONG>(reportBytes_)) != FALSE;
            if (!success) error = GetLastError();
        } else {
            HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!event) {
                error = GetLastError();
            } else {
                OVERLAPPED overlapped{};
                overlapped.hEvent = event;
                DWORD written = 0;
                success = WriteFile(
                              handle_, report,
                              static_cast<DWORD>(reportBytes_),
                              &written, &overlapped) != FALSE;
                if (!success) {
                    error = GetLastError();
                    if (error == ERROR_IO_PENDING) {
                        const DWORD wait = WaitForSingleObject(
                            event, config_.writeTimeoutMs);
                        if (wait == WAIT_OBJECT_0) {
                            success = GetOverlappedResult(
                                          handle_, &overlapped,
                                          &written, FALSE) != FALSE;
                            if (!success) error = GetLastError();
                        } else {
                            CancelIoEx(handle_, &overlapped);
                            WaitForSingleObject(event, INFINITE);
                            error = wait == WAIT_TIMEOUT
                                        ? WAIT_TIMEOUT
                                        : GetLastError();
                        }
                    }
                }
                if (success && written != reportBytes_) {
                    success = false;
                    error = ERROR_WRITE_FAULT;
                }
                CloseHandle(event);
            }
        }

        if (!success) {
            if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
            SetErrorLocked(error, "Sending RawHID report failed", true);
            return false;
        }
        ++status_.commandsSent;
        status_.bytesSent += reportBytes_;
        status_.lastError = ERROR_SUCCESS;
        status_.message = "Connected";
        return true;
    }

    void CloseLocked() noexcept {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(handle_, nullptr);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
        reportBytes_ = 0;
    }

    void RejectLocked(DWORD error, std::string message) {
        status_.lastError = error;
        status_.message = std::move(message);
    }

    void SetErrorLocked(DWORD error, std::string message,
                        bool closeHandle) noexcept {
        if (closeHandle) CloseLocked();
        status_.state = AimDeviceConnectionState::Error;
        status_.lastError = error;
        status_.message = std::move(message);
        status_.lastTransitionTick = GetTickCount64();
    }

    mutable std::mutex mutex_;
    AimDeviceConfig config_{};
    AimDeviceStatus status_{};
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    size_t reportBytes_ = 0;
};

class NoneDevice final : public IAimDevice {
public:
    AimDeviceKind Kind() const noexcept override { return AimDeviceKind::None; }

    bool Connect(const AimDeviceConfig& config) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (config.kind != AimDeviceKind::None) {
            status_.state = AimDeviceConnectionState::Error;
            status_.lastError = ERROR_INVALID_PARAMETER;
            status_.message = "Configuration does not match device type";
            status_.lastTransitionTick = GetTickCount64();
            return false;
        }
        status_.state = AimDeviceConnectionState::Disabled;
        status_.lastError = ERROR_SUCCESS;
        status_.message = "Aim device is disabled";
        status_.lastTransitionTick = GetTickCount64();
        return true;
    }

    void Disconnect() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = AimDeviceConnectionState::Disabled;
        status_.lastError = ERROR_SUCCESS;
        status_.message = "Aim device is disabled";
        status_.lastTransitionTick = GetTickCount64();
    }

    bool Move(int32_t, int32_t) override { return false; }
    bool Test(int32_t = 8) override { return false; }

    AimDeviceStatus Status() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

    AimDeviceCapabilities Capabilities() const noexcept override {
        return NoCapabilities();
    }

private:
    mutable std::mutex mutex_;
    AimDeviceStatus status_{AimDeviceConnectionState::Disabled,
                            ERROR_SUCCESS, "Aim device is disabled"};
};

class UnsupportedDevice final : public IAimDevice {
public:
    explicit UnsupportedDevice(AimDeviceKind kind) : kind_(kind) {
        const AimDeviceDescriptor* descriptor = FindDescriptor(kind_);
        status_.state = AimDeviceConnectionState::Unsupported;
        status_.lastError = ERROR_NOT_SUPPORTED;
        status_.message = descriptor && descriptor->unsupportedReason
                              ? descriptor->unsupportedReason
                              : "Device adapter is unsupported";
    }

    AimDeviceKind Kind() const noexcept override { return kind_; }

    bool Connect(const AimDeviceConfig& config) override {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = AimDeviceConnectionState::Unsupported;
        status_.lastError = config.kind == kind_
                                ? ERROR_NOT_SUPPORTED
                                : ERROR_INVALID_PARAMETER;
        const AimDeviceDescriptor* descriptor = FindDescriptor(kind_);
        status_.message = config.kind != kind_
                              ? "Configuration does not match device type"
                              : (descriptor && descriptor->unsupportedReason
                                     ? descriptor->unsupportedReason
                                     : "Device adapter is unsupported");
        status_.lastTransitionTick = GetTickCount64();
        return false;
    }

    void Disconnect() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = AimDeviceConnectionState::Unsupported;
        status_.lastError = ERROR_NOT_SUPPORTED;
        status_.lastTransitionTick = GetTickCount64();
    }

    bool Move(int32_t, int32_t) override { return false; }
    bool Test(int32_t = 8) override { return false; }

    AimDeviceStatus Status() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

    AimDeviceCapabilities Capabilities() const noexcept override {
        return UnsupportedCapabilities();
    }

private:
    AimDeviceKind kind_;
    mutable std::mutex mutex_;
    AimDeviceStatus status_{};
};

} // namespace Detail

class AimDeviceRegistry {
public:
    static const std::array<AimDeviceDescriptor, 9>& Devices() noexcept {
        static const std::array<AimDeviceDescriptor, 9> devices{{
            {AimDeviceKind::None, "none", "None", "Disabled", nullptr,
             Detail::NoCapabilities()},
            {AimDeviceKind::KMBoxSerial, "kmbox_serial",
             "KMBox B+/B Pro (Serial)", "km.move(x,y) text / 115200 8N1",
             nullptr, Detail::TextSerialCapabilities()},
            {AimDeviceKind::CH9329, "ch9329", "CH9329",
             "CMD_SEND_MS_REL binary / 115200 8N1", nullptr,
             Detail::CH9329Capabilities()},
            {AimDeviceKind::GenericSerial, "generic_serial",
             "Generic Serial", "Validated {x}/{y} text template / 8N1",
             nullptr, Detail::TextSerialCapabilities()},
            {AimDeviceKind::KMBoxNet, "kmbox_net", "KMBox NET",
             "Classic official UDP request/reply protocol", nullptr,
             Detail::KMBoxNetCapabilities()},
            {AimDeviceKind::Makcu, "makcu", "MAKCU",
             "km.version/km.move text / 115200 or 4000000 8N1",
             nullptr, Detail::TextSerialCapabilities()},
            {AimDeviceKind::MoBox, "mobox", "MoBox / Magic Box",
             "Vendor SDK",
             "MoBox adapter is not bundled; a trusted vendor SDK adapter is required",
             Detail::UnsupportedCapabilities()},
            {AimDeviceKind::Lurker, "lurker", "Lurker",
             "57 AB relative movement / 57600 8N1", nullptr,
             Detail::LurkerCapabilities()},
            {AimDeviceKind::RawHid, "raw_hid", "RawHID (VID/PID)",
             "Windows HID configurable output/feature report", nullptr,
             Detail::RawHidCapabilities()},
        }};
        return devices;
    }

    static const AimDeviceDescriptor* Find(AimDeviceKind kind) noexcept {
        for (const AimDeviceDescriptor& descriptor : Devices()) {
            if (descriptor.kind == kind) return &descriptor;
        }
        return nullptr;
    }

    static const AimDeviceDescriptor* Find(std::string_view key) noexcept {
        for (const AimDeviceDescriptor& descriptor : Devices()) {
            if (key == descriptor.key) return &descriptor;
        }
        return nullptr;
    }

    static AimDeviceConfig DefaultConfig(AimDeviceKind kind) {
        AimDeviceConfig config;
        config.kind = kind;
        if (kind == AimDeviceKind::GenericSerial)
            config.genericMoveTemplate = "MOVE {x} {y}\r\n";
        if (kind == AimDeviceKind::Lurker)
            config.baudRate = 57600;
        if (kind == AimDeviceKind::Makcu)
            config.baudRate = 4000000;
        return config;
    }

    static AimDeviceConfigValidation Validate(const AimDeviceConfig& config) {
        return Detail::ValidateConfig(config);
    }

    static std::unique_ptr<IAimDevice> Create(AimDeviceKind kind) {
        switch (kind) {
        case AimDeviceKind::None:
            return std::make_unique<Detail::NoneDevice>();
        case AimDeviceKind::KMBoxSerial:
            return std::make_unique<Detail::KMBoxSerialDevice>();
        case AimDeviceKind::CH9329:
            return std::make_unique<Detail::CH9329Device>();
        case AimDeviceKind::GenericSerial:
            return std::make_unique<Detail::GenericSerialDevice>();
        case AimDeviceKind::KMBoxNet:
            return std::make_unique<Detail::KMBoxNetDevice>();
        case AimDeviceKind::Lurker:
            return std::make_unique<Detail::LurkerDevice>();
        case AimDeviceKind::Makcu:
            return std::make_unique<Detail::MakcuDevice>();
        case AimDeviceKind::RawHid:
            return std::make_unique<Detail::RawHidDevice>();
        case AimDeviceKind::MoBox:
            return std::make_unique<Detail::UnsupportedDevice>(kind);
        default:
            return nullptr;
        }
    }

    static std::unique_ptr<IAimDevice> Create(
        const AimDeviceConfig& config) {
        return Create(config.kind);
    }
};

namespace Detail {
inline const AimDeviceDescriptor* FindDescriptor(AimDeviceKind kind) noexcept {
    return AimDeviceRegistry::Find(kind);
}
} // namespace Detail

} // namespace Aim
} // namespace SKJH
