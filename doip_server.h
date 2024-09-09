#ifndef DOIP_SERVER_H
#define DOIP_SERVER_H

#include <vector>
#include <array>
#include <netinet/in.h>
#include "uds_server.h"

class DoIPServer {
public:
    DoIPServer();
    void run();

private:
    void processDoIPMessage(int clientSocket, const uint8_t* data, size_t length);
    void sendVehicleIdentificationResponse(int clientSocket);
    void handleRoutingActivationRequest(int clientSocket, const uint8_t* payload, uint32_t length);
    void sendNegativeResponse(int clientSocket, uint16_t payloadType, uint8_t responseCode);

    int serverSocket;
    struct sockaddr_in serverAddr;
    std::vector<uint8_t> buffer;
    bool routingActivated;
    std::array<uint8_t, 6> vin;
    UDSServer udsServer;
};

#endif // DOIP_SERVER_H
