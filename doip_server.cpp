#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "doip_server.h"

#define PORT 13400
#define BUFFER_SIZE (1024*64)
#define VIN {{0x31, 0x32, 0x33, 0x34, 0x35, 0x36}}

DoIPServer::DoIPServer()
    : buffer(BUFFER_SIZE),
      routingActivated(false),
      vin(VIN),
      udsServer()
{
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Failed to create socket" << std::endl;
        exit(EXIT_FAILURE);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Failed to bind socket" << std::endl;
        exit(EXIT_FAILURE);
    }

    if (listen(serverSocket, 3) < 0) {
        std::cerr << "Failed to listen" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "DoIP Server listening on port " << PORT << std::endl;
}

void DoIPServer::run() {
    while (true) {
        int clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket < 0) {
            std::cerr << "Failed to accept connection" << std::endl;
            continue;
        }

        std::cout << "Client connected" << std::endl;
        routingActivated = false;  // Reset routing activation for new connection

        while (true) {
            ssize_t bytesRead = recv(clientSocket, buffer.data(), BUFFER_SIZE, 0);
            if (bytesRead < 0) {
                std::cout << "Client disconnected" << std::endl;
                break;
            }
            if (bytesRead == 0) {
                continue;
            }

            // Process DoIP message
            processDoIPMessage(clientSocket, buffer.data(), bytesRead);
        }

        close(clientSocket);
    }
}

void DoIPServer::processDoIPMessage(int clientSocket, const uint8_t* data, size_t length) {
    if (length < 8) {
        std::cerr << "Invalid DoIP message: too short" << std::endl;
        return;
    }

    uint16_t protocolVersion = (data[0] << 8) | data[1];
    uint16_t payloadType = (data[2] << 8) | data[3];
    uint32_t payloadLength = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];

    std::cout << "Received DoIP message:" << std::endl;
    std::cout << "  Protocol Version: 0x" << std::hex << protocolVersion << std::endl;
    std::cout << "  Payload Type: 0x" << std::hex << payloadType << std::endl;
    std::cout << "  Payload Length: " << std::dec << payloadLength << std::endl;

    switch (payloadType) {
        case 0x0001:  // Vehicle Identification Request
            sendVehicleIdentificationResponse(clientSocket);
            break;
        case 0x0005:  // Routing Activation Request
            handleRoutingActivationRequest(clientSocket, data + 8, payloadLength);
            break;
        case 0x8001:  // Diagnostic Message
            if (routingActivated) {
                if (payloadLength < 4) {
                    sendNegativeResponse(clientSocket, payloadType, 0x04);  // Invalid payload length
                    return;
                }

                uint16_t sourceAddress = (data[8] << 8) | data[9];
                uint16_t targetAddress = (data[10] << 8) | data[11];

                // Extract UDS payload
                std::vector<uint8_t> udsRequest(data + 12, data + 8 + payloadLength);

                // Pass the request to the UDS server
                std::vector<uint8_t> udsResponse = udsServer.handleRequest(udsRequest);

                // Construct DoIP response
                std::vector<uint8_t> doipResponse;
                doipResponse.reserve(8 + 4 + udsResponse.size());

                // DoIP header
                doipResponse.push_back(0x02);  // Protocol version
                doipResponse.push_back(0xFD);
                doipResponse.push_back(0x80);  // Payload type (0x8001)
                doipResponse.push_back(0x01);
                uint32_t responseLength = 4 + udsResponse.size();  // source address + target address + UDS response
                doipResponse.push_back((responseLength >> 24) & 0xFF);
                doipResponse.push_back((responseLength >> 16) & 0xFF);
                doipResponse.push_back((responseLength >> 8) & 0xFF);
                doipResponse.push_back(responseLength & 0xFF);

                // Source and target addresses (swapped from request)
                doipResponse.push_back((targetAddress >> 8) & 0xFF);
                doipResponse.push_back(targetAddress & 0xFF);
                doipResponse.push_back((sourceAddress >> 8) & 0xFF);
                doipResponse.push_back(sourceAddress & 0xFF);

                // UDS response
                doipResponse.insert(doipResponse.end(), udsResponse.begin(), udsResponse.end());

                send(clientSocket, doipResponse.data(), doipResponse.size(), 0);
            } else {
                sendNegativeResponse(clientSocket, payloadType, 0x02);  // Routing not activated
            }
            break;
        default:
            std::cerr << "Unsupported payload type: 0x" << std::hex << payloadType << std::endl;
            sendNegativeResponse(clientSocket, payloadType, 0x00);  // Unknown payload type
    }
}

void DoIPServer::sendVehicleIdentificationResponse(int clientSocket) {
    std::vector<uint8_t> response = {
        0x02, 0xFD,  // Protocol version
        0x00, 0x04,  // Payload type (Vehicle Identification Response)
        0x00, 0x00, 0x00, 0x21,  // Payload length (33 bytes)
        0x01,  // VIN GID
        0x00,  // VIN logical address
        0xE0, 0x00  // EID
    };
    response.insert(response.end(), vin.begin(), vin.end());  // VIN
    response.insert(response.end(), 17, 0x00);  // Padding to 33 bytes

    send(clientSocket, response.data(), response.size(), 0);
}

void DoIPServer::handleRoutingActivationRequest(int clientSocket, const uint8_t* payload, uint32_t length) {
    if (length < 7) {  // Source address (2 bytes) + Activation type (1 byte) + Reserved (4 bytes)
        sendNegativeResponse(clientSocket, 0x0005, 0x04);  // Invalid payload length
        return;
    }

    uint16_t sourceAddress = (payload[0] << 8) | payload[1];
    uint8_t activationType = payload[2];

    // Here you would implement proper activation type handling and any required authentication
    (void)activationType;  // Avoid unused variable warning (remove this line if you use activationType)
    // For this example, we'll accept any activation request
    routingActivated = true;

    std::vector<uint8_t> response = {
        0x02, 0xFD,  // Protocol version
        0x00, 0x06,  // Payload type (Routing Activation Response)
        0x00, 0x00, 0x00, 0x09,  // Payload length (9 bytes)
        static_cast<uint8_t>(sourceAddress >> 8), static_cast<uint8_t>(sourceAddress & 0xFF),  // Client's logical address
        0x00, 0x00,  // Logical address of external test equipment
        0x10,  // Routing activation response code (0x10 = Routing succesfully activated)
        0x00, 0x00, 0x00, 0x00  // Reserved
    };

    send(clientSocket, response.data(), response.size(), 0);
}

void DoIPServer::sendNegativeResponse(int clientSocket, uint16_t payloadType, uint8_t responseCode) {
    std::vector<uint8_t> response = {
        0x02, 0xFD,  // Protocol version
        static_cast<uint8_t>((payloadType >> 8) | 0x80), static_cast<uint8_t>(payloadType & 0xFF),  // Negative response payload type
        0x00, 0x00, 0x00, 0x01,  // Payload length (1 byte)
        responseCode
    };

    send(clientSocket, response.data(), response.size(), 0);
}
