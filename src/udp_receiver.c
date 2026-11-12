
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

#include "../include/packet.h"

#define RECEIVER_PORT 9000
#define HEADER_SIZE 15
#define MAX_DATAGRAM_SIZE (HEADER_SIZE + MAX_PAYLOAD_SIZE)

int main(void)
{
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    uint8_t buffer[MAX_DATAGRAM_SIZE];
    unsigned long valid_packets = 0;
    unsigned long invalid_packets = 0;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(RECEIVER_PORT);

    if (bind(
            sock,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) == SOCKET_ERROR)
    {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("UDP receiver listening on port %d...\n", RECEIVER_PORT);
    printf("Waiting for packets and then PACKET_END.\n");

    int finished = 0;

    while (!finished)
    {
        int client_len = sizeof(client_addr);

        int received = recvfrom(
            sock,
            (char *)buffer,
            sizeof(buffer),
            0,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (received == SOCKET_ERROR)
        {
            printf("Receive failed: %d\n", WSAGetLastError());
            break;
        }

        if (received < HEADER_SIZE)
        {
            printf("[INVALID] Datagram shorter than header\n");
            invalid_packets++;
            continue;
        }

        Packet packet = {0};

        if (deserialize_packet(&packet, buffer) != 0)
        {
            printf("[INVALID] Deserialization failed\n");
            invalid_packets++;
            continue;
        }

        if (received != HEADER_SIZE + packet.payload_length)
        {
            printf("[INVALID] Datagram length mismatch\n");
            invalid_packets++;
            continue;
        }

        if (!packet_validate(&packet))
        {
            printf("[INVALID] Checksum failed; sequence=%u\n",
                   (unsigned int)packet.sequence_number);
            invalid_packets++;
            continue;
        }

        if (packet.type == PACKET_END)
        {
            printf("[END] Received end packet, sequence=%u\n",
                   (unsigned int)packet.sequence_number);
            finished = 1;
            continue;
        }

        printf("[RECEIVED] Type=%d Sequence=%u Ack=%u Length=%u\n",
               (int)packet.type,
               (unsigned int)packet.sequence_number,
               (unsigned int)packet.acknowledgement_number,
               (unsigned int)packet.payload_length);

        printf("  Payload: %.*s\n",
               (int)packet.payload_length,
               (const char *)packet.payload);

        printf("  Checksum: VALID\n");
        valid_packets++;
    }

    printf("\n=== Receiver Summary ===\n");
    printf("Valid data packets: %lu\n", valid_packets);
    printf("Invalid packets: %lu\n", invalid_packets);

    closesocket(sock);
    WSACleanup();

    return finished ? 0 : 1;
}
