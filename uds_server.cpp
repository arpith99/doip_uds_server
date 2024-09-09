#include "uds_server.h"
#include <iostream>
#include <cstring>
#include <algorithm>

UDSServer::UDSServer()
    : testerPresent(false),
      currentSession(DiagnosticSession::DEFAULT),
      sessionTimeout(std::chrono::seconds(5)),
      currentBlockSequenceCounter(0),
      transferInProgress(false),
      eraseInProgress(false)
{
    // Register UDS services
    registerService(0x10, [this](const std::vector<uint8_t>& req) {
        return handleDiagnosticSessionControlRequest(req);
    });

    registerService(0x11, [this](const std::vector<uint8_t>& req) {
        return handleECUResetRequest(req);
    });

    registerService(0x3E, [this](const std::vector<uint8_t>& req) {
        return handleTesterPresentRequest(req);
    });

    registerService(0x34, [this](const std::vector<uint8_t>& req) {
        return handleRequestDownloadRequest(req);
    });

    registerService(0x36, [this](const std::vector<uint8_t>& req) {
        return handleTransferDataRequest(req);
    });

    registerService(0x37, [this](const std::vector<uint8_t>& req) {
        return handleRequestTransferExitRequest(req);
    });

    registerService(0x31, [this](const std::vector<uint8_t>& req) {
        return handleRoutineControlRequest(req);
    });

    lastActivityTime = std::chrono::steady_clock::now();
}

void UDSServer::registerService(uint8_t serviceId, UDSServiceFunction handler) {
    services[serviceId] = handler;
}

std::vector<uint8_t> UDSServer::handleRequest(const std::vector<uint8_t>& request) {
    checkSessionTimeout();
    updateSessionTimeout();

    if (request.empty()) {
        return {0x7F, 0x00, 0x11}; // General reject response
    }

    uint8_t serviceId = request[0];
    auto it = services.find(serviceId);
    if (it != services.end()) {
        return it->second(request);
    } else {
        return {0x7F, serviceId, 0x11}; // Service not supported
    }
}

std::vector<uint8_t> UDSServer::handleTesterPresentRequest(const std::vector<uint8_t>& request) {
    if (request.size() < 2) {
        return {0x7F, 0x3E, 0x13}; // Invalid format
    }

    if (request[1] == 0x00) { // Tester Present request
        testerPresent = true;
        return {0x7E, 0x00}; // Tester Present positive response
    } else if (request[1] == 0x01) { // Tester Present cancel request
        testerPresent = false;
        return {0x7E, 0x00}; // Tester Present positive response
    } else {
        return {0x7F, 0x3E, 0x31}; // Request out of range
    }
}

std::vector<uint8_t> UDSServer::handleDiagnosticSessionControlRequest(const std::vector<uint8_t>& request) {
    if (request.size() < 2) {
        return {0x7F, 0x10, 0x13}; // Invalid format
    }

    uint8_t requestedSession = request[1];
    std::vector<uint8_t> response{0x50, requestedSession}; // Positive response

    switch (requestedSession) {
        case 0x01: // Default Session
            currentSession = DiagnosticSession::DEFAULT;
            sessionTimeout = std::chrono::seconds(5);
            break;
        case 0x02: // Programming Session
            currentSession = DiagnosticSession::PROGRAMMING;
            sessionTimeout = std::chrono::seconds(10);
            break;
        case 0x03: // Extended Session
            currentSession = DiagnosticSession::EXTENDED;
            sessionTimeout = std::chrono::seconds(7);
            break;
        case 0x04: // Safety System Diagnostic Session
            currentSession = DiagnosticSession::SAFETY_SYSTEM;
            sessionTimeout = std::chrono::seconds(15);
            break;
        default:
            return {0x7F, 0x10, 0x12}; // Sub-function not supported
    }

    // Append P2server_max and P2*server_max times (example values)
    response.push_back(0x00); // P2server_max high byte
    response.push_back(0x32); // P2server_max low byte (50ms)
    response.push_back(0x01); // P2*server_max high byte
    response.push_back(0xF4); // P2*server_max low byte (500ms)

    return response;
}

std::vector<uint8_t> UDSServer::handleECUResetRequest(const std::vector<uint8_t>& request) {
    if (request.size() < 2) {
        return {0x7F, 0x11, 0x13}; // Invalid format
    }

    uint8_t resetType = request[1];
    std::vector<uint8_t> response{0x51, resetType}; // Positive response

    switch (resetType) {
        case 0x01: // Hard Reset
            performReset(ResetType::HARD_RESET);
            break;
        case 0x02: // Key Off On Reset
            performReset(ResetType::KEY_OFF_ON_RESET);
            break;
        case 0x03: // Soft Reset
            performReset(ResetType::SOFT_RESET);
            break;
        case 0x04: // Enable Rapid Power Shutdown
            performReset(ResetType::ENABLE_RAPID_POWER_SHUTDOWN);
            break;
        case 0x05: // Disable Rapid Power Shutdown
            performReset(ResetType::DISABLE_RAPID_POWER_SHUTDOWN);
            break;
        default:
            return {0x7F, 0x11, 0x12}; // Sub-function not supported
    }

    return response;
}

std::vector<uint8_t> UDSServer::handleRequestDownloadRequest(const std::vector<uint8_t>& request) {
    if (request.size() < 3) {
        return {0x7F, 0x34, 0x13}; // Invalid format
    }

    uint8_t dataFormatIdentifier = request[1];
    uint8_t addressAndLengthFormatIdentifier = request[2];

    (void)dataFormatIdentifier; // Unused for now
    (void)addressAndLengthFormatIdentifier; // Unused for now


    // Extract memory address and size
    auto memoryAddress = parseMemoryAddress(std::vector<uint8_t>(request.begin() + 3, request.end()));
    if (!memoryAddress) {
        return {0x7F, 0x34, 0x13}; // Invalid format
    }

    // In a real implementation, you would check if the memory address and size are valid
    // and if the ECU is in a state that allows downloading (e.g., programming session)
    if (currentSession != DiagnosticSession::PROGRAMMING) {
        return {0x7F, 0x34, 0x22}; // Conditions not correct
    }

    // Store the current download information
    currentDownloadMemoryAddress = memoryAddress;
    currentBlockSequenceCounter = 0;
    downloadBuffer.clear();
    transferInProgress = true;

    // Prepare positive response
    std::vector<uint8_t> response{0x74};

    // Add maxNumberOfBlockLength (example: 0x0400 = 1024 bytes)
    response.push_back(0x04);
    response.push_back(0x00);

    return response;
}

std::vector<uint8_t> UDSServer::handleTransferDataRequest(const std::vector<uint8_t>& request) {
    if (request.size() < 2) {
        return {0x7F, 0x36, 0x13}; // Invalid format
    }

    if (!currentDownloadMemoryAddress) {
        return {0x7F, 0x36, 0x22}; // Conditions not correct
    }

    if (!transferInProgress) {
        return {0x7F, 0x36, 0x22}; // Conditions not correct
    }

    uint8_t blockSequenceCounter = request[1];

    // Check if the block sequence counter is correct
    if (blockSequenceCounter != (currentBlockSequenceCounter + 1) % 256) {
        return {0x7F, 0x36, 0x73}; // Wrong Block Sequence Counter
    }

    currentBlockSequenceCounter = blockSequenceCounter;

    // Extract the data from the request
    std::vector<uint8_t> data(request.begin() + 2, request.end());

    // In a real implementation, you would write this data to the appropriate memory location
    // For this example, we'll just append it to our download buffer
    downloadBuffer.insert(downloadBuffer.end(), data.begin(), data.end());

    // Check if we've received all the data
    if (downloadBuffer.size() >= currentDownloadMemoryAddress->size) {
        // Truncate any excess data
        downloadBuffer.resize(currentDownloadMemoryAddress->size);

        // In a real implementation, you would now process the complete download
        // (e.g., flash it to memory, verify checksums, etc.)
        std::cout << "Download complete. Total bytes received: " << downloadBuffer.size() << std::endl;

        // Reset download state
        currentDownloadMemoryAddress.reset();
        currentBlockSequenceCounter = 0;
    }

    // Prepare positive response
    return {0x76, blockSequenceCounter};
}

std::vector<uint8_t> UDSServer::handleRequestTransferExitRequest(const std::vector<uint8_t>& request) {
    (void)request; // Unused for now

    if (!transferInProgress) {
        return {0x7F, 0x37, 0x22}; // Conditions not correct
    }

    // In a real implementation, you might want to perform some final checks or processing here
    // For example, verifying a checksum, finalizing the data write, etc.

    // For this example, we'll just print the total bytes received
    std::cout << "Transfer complete. Total bytes received: " << downloadBuffer.size() << std::endl;

    // Reset transfer state
    currentDownloadMemoryAddress.reset();
    currentBlockSequenceCounter = 0;
    downloadBuffer.clear();
    transferInProgress = false;

    // Prepare positive response
    // The response can include optional transmission verification parameters
    // For this example, we'll just send a simple positive response
    return {0x77};
}

std::vector<uint8_t> UDSServer::handleRoutineControlRequest(const std::vector<uint8_t>& request) {
    if (request.size() < 4) {
        return {0x7F, 0x31, 0x13}; // Invalid format
    }

    RoutineControlType controlType = static_cast<RoutineControlType>(request[1]);
    uint16_t routineIdentifier = (request[2] << 8) | request[3];

    // Check if we're in the correct session (e.g., programming session)
    if (currentSession != DiagnosticSession::PROGRAMMING) {
        return {0x7F, 0x31, 0x22}; // Conditions not correct
    }

    // Handle different routines
    switch (routineIdentifier) {
        case 0xFF00: { // Erase routine
            return handleEraseRoutine(controlType, std::vector<uint8_t>(request.begin() + 4, request.end()));
        }
        case 0xFF01: { // Check Programming Preconditions routine
            return handleCheckProgrammingPreConditionsRoutine(controlType, std::vector<uint8_t>(request.begin() + 4, request.end()));
        }
        case 0xFF02: {
            return handleCheckProgrammingDependenciesRoutine(controlType, std::vector<uint8_t>(request.begin() + 4, request.end()));
        }
        case 0xFF03: { // Check Memory routine
            return handleCheckMemoryRoutine(controlType, std::vector<uint8_t>(request.begin() + 4, request.end()));
        }
        default:
            return {0x7F, 0x31, 0x31}; // Request out of range
    }
}

std::vector<uint8_t> UDSServer::handleEraseRoutine(RoutineControlType controlType, const std::vector<uint8_t>& data) {
    switch (controlType) {
        case RoutineControlType::START: {
            if (data.size() < 8) {
                return {0x7F, 0x31, 0x13}; // Invalid format
            }
            
            // Parse erase address and size
            auto eraseAddressOpt = parseMemoryAddress(data);
            if (!eraseAddressOpt) {
                return {0x7F, 0x31, 0x13}; // Invalid format
            }
            
            eraseAddress = *eraseAddressOpt;
            eraseInProgress = true;
            
            std::cout << "Starting erase operation at address 0x" 
                      << std::hex << eraseAddress.address 
                      << " for " << std::dec << eraseAddress.size << " bytes" << std::endl;
            
            // In a real implementation, you would start the erase operation here
            // For this example, we'll just simulate it
            
            return {0x71, 0x01, 0xFF, 0x00}; // Positive response for start
        }

        case RoutineControlType::STOP: {
            if (!eraseInProgress) {
                return {0x7F, 0x31, 0x22}; // Conditions not correct
            }
            
            eraseInProgress = false;
            std::cout << "Stopping erase operation" << std::endl;
            
            // In a real implementation, you would stop the erase operation here
            
            return {0x71, 0x02, 0xFF, 0x00}; // Positive response for stop
        }

        case RoutineControlType::REQUEST_RESULTS: {
            if (!eraseInProgress) {
                return {0x71, 0x03, 0xFF, 0x00, 0x00}; // Completed successfully
            } else {
                return {0x71, 0x03, 0xFF, 0x00, 0x01}; // Still in progress
            }
        }

        default: {
            return {0x7F, 0x31, 0x31}; // Request out of range
        }
    }
}

std::vector<uint8_t> UDSServer::handleCheckProgrammingPreConditionsRoutine(RoutineControlType controlType, const std::vector<uint8_t>& data) {
    (void)data; // Unused for now

    switch (controlType) {
        case RoutineControlType::START: {
            // Perform checks
            bool voltageOk = checkVoltage();
            bool temperatureOk = checkTemperature();
            bool securityOk = checkSecurityAccess();

            // Prepare result
            uint8_t result = 0;
            result |= (voltageOk ? 0x01 : 0);
            result |= (temperatureOk ? 0x02 : 0);
            result |= (securityOk ? 0x04 : 0);

            std::cout << "Checking programming preconditions: "
                      << "Voltage " << (voltageOk ? "OK" : "Not OK") << ", "
                      << "Temperature " << (temperatureOk ? "OK" : "Not OK") << ", "
                      << "Security " << (securityOk ? "OK" : "Not OK") << std::endl;

            // Return positive response with result
            return {0x71, 0x01, 0xFF, 0x01, result};
        }

        case RoutineControlType::STOP: {
            // This routine doesn't support stopping
            return {0x7F, 0x31, 0x22}; // Conditions not correct
        }

        case RoutineControlType::REQUEST_RESULTS: {
            // This routine doesn't support requesting results separately
            return {0x7F, 0x31, 0x22}; // Conditions not correct
        }

        default: {
            return {0x7F, 0x31, 0x31}; // Request out of range
        }
    }
}

std::vector<uint8_t> UDSServer::handleCheckProgrammingDependenciesRoutine(RoutineControlType controlType, const std::vector<uint8_t>& data) {
    (void)data; // Unused for now

    switch (controlType) {
        case RoutineControlType::START: {
            // Perform checks
            bool softwareVersionOk = checkSoftwareVersionCompatibility();
            bool hardwareVersionOk = checkHardwareVersionCompatibility();
            bool memoryAvailableOk = checkMemoryAvailability();

            // Prepare result
            uint8_t result = 0;
            result |= (softwareVersionOk ? 0x01 : 0);
            result |= (hardwareVersionOk ? 0x02 : 0);
            result |= (memoryAvailableOk ? 0x04 : 0);

            std::cout << "Checking programming dependencies: "
                      << "Software Version " << (softwareVersionOk ? "OK" : "Not OK") << ", "
                      << "Hardware Version " << (hardwareVersionOk ? "OK" : "Not OK") << ", "
                      << "Memory Availability " << (memoryAvailableOk ? "OK" : "Not OK") << std::endl;

            // Return positive response with result
            return {0x71, 0x01, 0xFF, 0x02, result};
        }

        case RoutineControlType::STOP: {
            // This routine doesn't support stopping
            return {0x7F, 0x31, 0x22}; // Conditions not correct
        }

        case RoutineControlType::REQUEST_RESULTS: {
            // This routine doesn't support requesting results separately
            return {0x7F, 0x31, 0x22}; // Conditions not correct
        }

        default: {
            return {0x7F, 0x31, 0x31}; // Request out of range
        }
    }
}

std::vector<uint8_t> UDSServer::handleCheckMemoryRoutine(RoutineControlType controlType, const std::vector<uint8_t>& data) {
    switch (controlType) {
        case RoutineControlType::START: {
            if (data.size() < 8) { // Minimum length: address (4) + size (4)
                return {0x7F, 0x31, 0x13}; // Invalid format
            }

            uint32_t startAddress = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            uint32_t size = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];

            if (!checkMemoryRange(startAddress, size)) {
                return {0x7F, 0x31, 0x31}; // Request out of range
            }

            std::vector<uint8_t> checksum = calculateChecksum(startAddress, size);

            // Positive response
            std::vector<uint8_t> response = {0x71, 0x01, 0xFF, 0x03};
            response.insert(response.end(), checksum.begin(), checksum.end());
            return response;
        }

        case RoutineControlType::STOP:
            // Check Memory doesn't support stopping
            return {0x7F, 0x31, 0x22}; // Conditions not correct

        case RoutineControlType::REQUEST_RESULTS:
            // Check Memory doesn't support requesting results separately
            return {0x7F, 0x31, 0x22}; // Conditions not correct

        default:
            return {0x7F, 0x31, 0x31}; // Request out of range
    }
}

bool UDSServer::isTesterPresent() const {
    return testerPresent;
}

UDSServer::DiagnosticSession UDSServer::getCurrentSession() const {
    return currentSession;
}

void UDSServer::updateSessionTimeout() {
    lastActivityTime = std::chrono::steady_clock::now();
}

void UDSServer::checkSessionTimeout() {
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastActivityTime);

    if (elapsedTime > sessionTimeout) {
        currentSession = DiagnosticSession::DEFAULT;
        sessionTimeout = std::chrono::seconds(5);
    }
}

void UDSServer::performReset(ResetType resetType) {
    // Simulating reset actions
    switch (resetType) {
        case ResetType::HARD_RESET:
            std::cout << "Performing Hard Reset..." << std::endl;
            // Simulate hard reset actions
            break;
        case ResetType::KEY_OFF_ON_RESET:
            std::cout << "Performing Key Off On Reset..." << std::endl;
            // Simulate key off on reset actions
            break;
        case ResetType::SOFT_RESET:
            std::cout << "Performing Soft Reset..." << std::endl;
            // Simulate soft reset actions
            break;
        case ResetType::ENABLE_RAPID_POWER_SHUTDOWN:
            std::cout << "Enabling Rapid Power Shutdown..." << std::endl;
            // Simulate enabling rapid power shutdown
            break;
        case ResetType::DISABLE_RAPID_POWER_SHUTDOWN:
            std::cout << "Disabling Rapid Power Shutdown..." << std::endl;
            // Simulate disabling rapid power shutdown
            break;
    }

    // Reset to default session after any reset type
    currentSession = DiagnosticSession::DEFAULT;
    sessionTimeout = std::chrono::seconds(5);
}

std::optional<UDSServer::MemoryAddress> UDSServer::parseMemoryAddress(const std::vector<uint8_t>& data) {
    if (data.size() < 8) {
        return std::nullopt;
    }

    MemoryAddress result;
    std::memcpy(&result.address, data.data(), 4);
    std::memcpy(&result.size, data.data() + 4, 4);

    // Convert from big-endian to host byte order if necessary
    // This assumes the UDS message uses big-endian format
    result.address = __builtin_bswap32(result.address);
    result.size = __builtin_bswap32(result.size);

    return result;
}

bool UDSServer::checkMemoryRange(uint32_t startAddress, uint32_t size, uint8_t& errorCode) {
    // Check if the memory range is valid for this ECU
    // This is a placeholder implementation. In a real ECU, you would check against actual memory boundaries.
    const uint32_t ECU_MEMORY_START = 0x00000000;
    const uint32_t ECU_MEMORY_END = 0x00100000;  // Assuming 1MB of addressable memory

    if (startAddress < ECU_MEMORY_START || startAddress + size > ECU_MEMORY_END) {
        errorCode = 0x31; // Request out of range
        return false;
    }

    // Check if we're in the correct diagnostic session
    if (currentSession != DiagnosticSession::EXTENDED && currentSession != DiagnosticSession::PROGRAMMING) {
        errorCode = 0x7E; // Service not supported in current session
        return false;
    }

    // Here you could add additional checks, such as:
    // - Check if the memory range is readable
    // - Check if security access has been granted

    return true;
}

std::vector<uint8_t> UDSServer::calculateChecksum(uint32_t startAddress, uint32_t size) {
    (void)startAddress; // Unused for now
    (void)size; // Unused for now
    std::vector<uint8_t> result(4, 0);
    return result;
}

bool UDSServer::checkVoltage() const {
    // Simulate voltage check (replace with actual implementation)
    return true;
}

bool UDSServer::checkTemperature() const {
    // Simulate temperature check (replace with actual implementation)
    return true;
}

bool UDSServer::checkSecurityAccess() const {
    // Simulate security access check (replace with actual implementation)
    return true;
}

bool UDSServer::checkSoftwareVersionCompatibility() const {
    // Simulate software version compatibility check (replace with actual implementation)
    return true;
}

bool UDSServer::checkHardwareVersionCompatibility() const {
    // Simulate hardware version compatibility check (replace with actual implementation)
    return true;
}

bool UDSServer::checkMemoryAvailability() const {
    // Simulate memory availability check (replace with actual implementation)
    return true; // Assuming we need at least 20% free memory
}

bool UDSServer::checkMemoryRange(uint32_t startAddress, uint32_t size) const {
    // Check if the memory range is valid for this ECU
    // This is a placeholder implementation. In a real ECU, you would check against actual memory boundaries.
    const uint32_t ECU_MEMORY_START = 0x00000000;
    const uint32_t ECU_MEMORY_END = 0x00100000;  // Assuming 1MB of addressable memory

    return (startAddress >= ECU_MEMORY_START && startAddress + size <= ECU_MEMORY_END);
}

std::vector<uint8_t> UDSServer::calculateChecksum(uint32_t startAddress, uint32_t size) const {
    // This is a placeholder implementation. In a real ECU, you would calculate the checksum
    // based on the actual memory contents.
    
    // For demonstration purposes, we'll return a simple CRC32 of the address and size
    uint32_t crc = startAddress ^ size;
    for (int i = 0; i < 32; ++i) {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xEDB88320;
        else
            crc >>= 1;
    }

    std::vector<uint8_t> result(4);
    result[0] = (crc >> 24) & 0xFF;
    result[1] = (crc >> 16) & 0xFF;
    result[2] = (crc >> 8) & 0xFF;
    result[3] = crc & 0xFF;

    return result;
}
