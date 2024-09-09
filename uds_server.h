#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include <cstdint>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <optional>

class UDSServer {
public:
    using UDSServiceFunction = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;

    enum class DiagnosticSession {
        DEFAULT,
        PROGRAMMING,
        EXTENDED,
        SAFETY_SYSTEM
    };

    enum class ResetType {
        HARD_RESET,
        KEY_OFF_ON_RESET,
        SOFT_RESET,
        ENABLE_RAPID_POWER_SHUTDOWN,
        DISABLE_RAPID_POWER_SHUTDOWN
    };

    enum class RoutineControlType {
        START = 0x01,
        STOP = 0x02,
        REQUEST_RESULTS = 0x03
    };

    struct MemoryAddress {
        uint32_t address;
        uint32_t size;
    };

    UDSServer();

    void registerService(uint8_t serviceId, UDSServiceFunction handler);
    std::vector<uint8_t> handleRequest(const std::vector<uint8_t>& request);

    bool isTesterPresent() const;
    DiagnosticSession getCurrentSession() const;

private:
    std::vector<uint8_t> handleTesterPresentRequest(const std::vector<uint8_t>& request);
    std::vector<uint8_t> handleDiagnosticSessionControlRequest(const std::vector<uint8_t>& request);
    std::vector<uint8_t> handleECUResetRequest(const std::vector<uint8_t>& request);
    std::vector<uint8_t> handleRequestDownloadRequest(const std::vector<uint8_t>& request);
    std::vector<uint8_t> handleTransferDataRequest(const std::vector<uint8_t>& request);
    std::vector<uint8_t> handleRequestTransferExitRequest(const std::vector<uint8_t>& request);
    std::vector<uint8_t> handleCheckMemoryRequest(const std::vector<uint8_t>& request);
    std::vector<uint8_t> handleRoutineControlRequest(const std::vector<uint8_t>& request);
    std::vector<uint8_t> handleEraseRoutine(RoutineControlType controlType, const std::vector<uint8_t>& data);
    std::vector<uint8_t> handleCheckProgrammingPreConditionsRoutine(RoutineControlType controlType, const std::vector<uint8_t>& data);
    std::vector<uint8_t> handleCheckProgrammingDependenciesRoutine(RoutineControlType controlType, const std::vector<uint8_t>& data);
    std::vector<uint8_t> handleCheckMemoryRoutine(RoutineControlType controlType, const std::vector<uint8_t>& data);
    void updateSessionTimeout();
    void checkSessionTimeout();
    void performReset(ResetType resetType);
    std::optional<MemoryAddress> parseMemoryAddress(const std::vector<uint8_t>& data);
    bool checkMemoryRange(uint32_t startAddress, uint32_t size, uint8_t& errorCode);
    std::vector<uint8_t> calculateChecksum(uint32_t startAddress, uint32_t size);
    bool checkVoltage() const;
    bool checkTemperature() const;
    bool checkSecurityAccess() const;
    bool checkSoftwareVersionCompatibility() const;
    bool checkHardwareVersionCompatibility() const;
    bool checkMemoryAvailability() const;
    bool checkMemoryRange(uint32_t startAddress, uint32_t size) const;
    std::vector<uint8_t> calculateChecksum(uint32_t startAddress, uint32_t size) const;

    std::map<uint8_t, UDSServiceFunction> services;
    bool testerPresent;
    DiagnosticSession currentSession;
    std::chrono::steady_clock::time_point lastActivityTime;
    std::chrono::seconds sessionTimeout;
    std::optional<MemoryAddress> currentDownloadMemoryAddress;
    uint32_t currentBlockSequenceCounter;
    std::vector<uint8_t> downloadBuffer;
    bool transferInProgress;
    bool eraseInProgress;
    MemoryAddress eraseAddress;
};

#endif // UDS_SERVER_H
