
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

#include "../include/packet.h"

#define PROXY_PORT 9001
#define DATA_PACKET_COUNT 10
#define HEADER_SIZE 15
#define MAX_DATAGRAM_SIZE (HEADER_SIZE + MAX_PAYLOAD_SIZE)

static int send_packet(
    SOCKET sock,
    const struct sockaddr_in *destination,
    Packet *packet)
{
    uint8_t buffer[MAX_DATAGRAM_SIZE];

    packet->checksum = calculate_checksum(packet);

    int packet_size = serialize_packet(packet, buffer);
    if (packet_size < 0)
    {
        printf("[ERROR] Serialization failed\n");
        return 0;
    }

    int sent = sendto(
        sock,
        (const char *)buffer,
        packet_size,
        0,
        (const struct sockaddr *)destination,
        sizeof(*destination)
    );

    if (sent == SOCKET_ERROR)
    {
        printf("[ERROR] Send failed: %d\n", WSAGetLastError());
        return 0;
    }

    printf("[SENT] Type=%d Sequence=%u Bytes=%d\n",
           (int)packet->type,
           (unsigned int)packet->sequence_number,
           sent);

    return 1;
}

int main(void)
{
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in proxy_addr;

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

    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    proxy_addr.sin_port = htons(PROXY_PORT);

    int success = 1;

    for (uint32_t sequence = 1;
         sequence <= DATA_PACKET_COUNT;
         sequence++)
    {
        Packet packet = {0};

        packet.type = PACKET_DATA;
        packet.sequence_number = sequence;
        packet.acknowledgement_number = 0;

        int payload_length = snprintf(
            (char *)packet.payload,
            sizeof(packet.payload),
            "Hello UDP - packet %u",
            (unsigned int)sequence
        );

        if (payload_length < 0 ||
            (size_t)payload_length >= sizeof(packet.payload))
        {
            printf("Payload creation failed\n");
            success = 0;
            break;
        }

        packet.payload_length = (uint16_t)payload_length;

        if (!send_packet(sock, &proxy_addr, &packet))
        {
            success = 0;
            break;
        }

        Sleep(50);
    }

    if (success)
    {
        Packet end_packet = {0};
        end_packet.type = PACKET_END;
        end_packet.sequence_number = DATA_PACKET_COUNT + 1;
        end_packet.acknowledgement_number = 0;
        end_packet.payload_length = 0;

        success = send_packet(sock, &proxy_addr, &end_packet);
    }

    closesocket(sock);
    WSACleanup();

    if (success)
        printf("Finished sending data packets and END packet.\n");

    return success ? 0 : 1;
}
