#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>

#include "channel.h"
#include "../include/packet.h"

#define PROXY_PORT 9001
#define RECEIVER_PORT 9000
#define HEADER_SIZE 15
#define MAX_DATAGRAM_SIZE (HEADER_SIZE + MAX_PAYLOAD_SIZE)
#define HOLD_TIMEOUT_MS 200
#define RESULTS_DIR "results"
#define CSV_PATH "results/channel_stats.csv"

static volatile sig_atomic_t stop_requested = 0;

typedef struct
{
    uint64_t received;
    uint64_t forwarded;
    uint64_t dropped;
    uint64_t duplicated;
    uint64_t corrupted;
    uint64_t reordered;
    uint64_t invalid;
} ProxyStats;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void usage(const char *program)
{
    printf("Usage: %s [loss] [duplication] [reordering] "
           "[corruption] [min_delay_ms] [max_delay_ms] [--auto] [--seed N]\n",
           program);
}

static int parse_probability(const char *text, double *value)
{
    char *end = NULL;
    double parsed = strtod(text, &end);

    if (text == end || *end != '\0' ||
        parsed < 0.0 || parsed > 1.0)
        return 0;

    *value = parsed;
    return 1;
}

static int parse_delay(const char *text, unsigned int *value)
{
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);

    if (text == end || *end != '\0' || parsed > 60000)
        return 0;

    *value = (unsigned int)parsed;
    return 1;
}

static void ensure_csv_header(void)
{
    FILE *file = fopen(CSV_PATH, "r");

    if (file != NULL)
    {
        fclose(file);
        return;
    }

    file = fopen(CSV_PATH, "w");

    if (file == NULL)
    {
        perror("Could not create CSV file");
        return;
    }

    fprintf(file,
        "timestamp,loss_probability,duplication_probability,"
        "reordering_probability,corruption_probability,"
        "min_delay_ms,max_delay_ms,received,forwarded,dropped,"
        "duplicated,corrupted,reordered,invalid\n");

    fclose(file);
}

static void write_csv_summary(
    const ChannelConfig *config,
    const ProxyStats *stats)
{
    FILE *file = fopen(CSV_PATH, "a");

    if (file == NULL)
    {
        perror("Could not append to CSV file");
        return;
    }

    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    char timestamp[32] = "unknown";

    if (local != NULL)
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", local);

    fprintf(file,
        "%s,%.3f,%.3f,%.3f,%.3f,%u,%u,"
        "%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        timestamp,
        config->loss_probability,
        config->duplication_probability,
        config->reordering_probability,
        config->corruption_probability,
        config->min_delay_ms,
        config->max_delay_ms,
        (unsigned long long)stats->received,
        (unsigned long long)stats->forwarded,
        (unsigned long long)stats->dropped,
        (unsigned long long)stats->duplicated,
        (unsigned long long)stats->corrupted,
        (unsigned long long)stats->reordered,
        (unsigned long long)stats->invalid);

    fclose(file);
    printf("[CSV] Summary appended to %s\n", CSV_PATH);
}

static int send_datagram(
    SOCKET sock,
    const struct sockaddr_in *receiver,
    const uint8_t *data,
    size_t length)
{
    int sent = sendto(
        sock, (const char *)data, (int)length, 0,
        (const struct sockaddr *)receiver, sizeof(*receiver));

    if (sent == SOCKET_ERROR)
    {
        printf("[ERROR] Forward failed: %d\n", WSAGetLastError());
        return 0;
    }

    return 1;
}

static void forward_packet(
    SOCKET sock,
    const struct sockaddr_in *receiver,
    const uint8_t *data,
    size_t length,
    const ChannelConfig *config,
    ProxyStats *stats)
{
    uint8_t working[MAX_DATAGRAM_SIZE];

    if (data == NULL || length == 0 || length > sizeof(working))
    {
        stats->invalid++;
        return;
    }

    if (channel_should_drop(config))
    {
        stats->dropped++;
        printf("[DROP] Packet discarded\n");
        return;
    }

    memcpy(working, data, length);

    if (channel_should_corrupt(config) &&
        channel_corrupt_packet(working, length))
    {
        stats->corrupted++;
        printf("[CORRUPT] Packet bytes modified\n");
    }

    unsigned int delay = channel_get_delay(config);
    if (delay > 0)
        Sleep(delay);

    if (!send_datagram(sock, receiver, working, length))
        return;

    stats->forwarded++;
    printf("[FORWARD] %zu bytes\n", length);

    if (channel_should_duplicate(config))
    {
        if (send_datagram(sock, receiver, working, length))
        {
            stats->duplicated++;
            stats->forwarded++;
            printf("[DUPLICATE] Extra copy forwarded\n");
        }
    }
}

int main(int argc, char *argv[])
{
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in proxy_addr, receiver_addr;
    ChannelConfig config;
    ProxyStats stats = {0};
    int auto_exit_on_end = 0;
    int positional_count = 0;
    int seed_was_set = 0;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    channel_config_init(&config);

    /*
     * The six original positional arguments remain compatible.
     * Optional flags can appear after them in either order:
     *   --auto
     *   --seed N
     */
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--auto") == 0)
        {
            auto_exit_on_end = 1;
            continue;
        }

        if (strcmp(argv[i], "--seed") == 0)
        {
            char *end = NULL;
            unsigned long parsed;

            if (i + 1 >= argc)
            {
                usage(argv[0]);
                printf("Missing value after --seed.\n");
                return 1;
            }

            parsed = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' ||
                parsed == 0 || parsed > 0xFFFFFFFFUL)
            {
                usage(argv[0]);
                printf("Seed must be an integer from 1 to 4294967295.\n");
                return 1;
            }

            config.seed = (uint32_t)parsed;
            seed_was_set = 1;
            continue;
        }

        switch (positional_count)
        {
            case 0:
                if (!parse_probability(argv[i], &config.loss_probability))
                    goto invalid_configuration;
                break;
            case 1:
                if (!parse_probability(argv[i], &config.duplication_probability))
                    goto invalid_configuration;
                break;
            case 2:
                if (!parse_probability(argv[i], &config.reordering_probability))
                    goto invalid_configuration;
                break;
            case 3:
                if (!parse_probability(argv[i], &config.corruption_probability))
                    goto invalid_configuration;
                break;
            case 4:
                if (!parse_delay(argv[i], &config.min_delay_ms))
                    goto invalid_configuration;
                break;
            case 5:
                if (!parse_delay(argv[i], &config.max_delay_ms))
                    goto invalid_configuration;
                break;
            default:
                goto invalid_configuration;
        }

        positional_count++;
    }

    if (!channel_config_valid(&config))
        goto invalid_configuration;

    channel_init(&config);
    printf("Random seed: %u%s\n", config.seed,
           seed_was_set ? " (explicit)" : " (default)");

    goto configuration_valid;

invalid_configuration:
    usage(argv[0]);
    printf("Invalid channel configuration.\n");
    return 1;

configuration_valid:
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
    proxy_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    proxy_addr.sin_port = htons(PROXY_PORT);

    if (bind(sock, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr))
        == SOCKET_ERROR)
    {
        printf("Proxy bind failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    receiver_addr.sin_port = htons(RECEIVER_PORT);

    ensure_csv_header();

    printf("UDP channel listening on port %d\n", PROXY_PORT);
    printf("Forwarding to 127.0.0.1:%d\n", RECEIVER_PORT);
    printf("Loss=%.2f Duplication=%.2f Reordering=%.2f "
           "Corruption=%.2f Delay=%u-%u ms\n",
           config.loss_probability, config.duplication_probability,
           config.reordering_probability, config.corruption_probability,
           config.min_delay_ms, config.max_delay_ms);

    uint8_t incoming[MAX_DATAGRAM_SIZE];
    uint8_t held[MAX_DATAGRAM_SIZE];
    size_t held_length = 0;
    int has_held = 0;

    while (!stop_requested)
    {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);

        struct timeval timeout = {0, HOLD_TIMEOUT_MS * 1000};
        int ready = select(0, &read_set, NULL, NULL, &timeout);

        if (ready == SOCKET_ERROR)
        {
            int error = WSAGetLastError();

            if (error == WSAEINTR)
                continue;

            printf("[ERROR] select failed: %d\n", error);
            break;
        }

        if (ready == 0)
        {
            if (has_held)
            {
                printf("[TIMEOUT] Releasing held packet\n");
                forward_packet(sock, &receiver_addr, held,
                               held_length, &config, &stats);
                has_held = 0;
                held_length = 0;
            }
            continue;
        }

        struct sockaddr_in sender_addr;
        int sender_length = sizeof(sender_addr);

        int received = recvfrom(
            sock, (char *)incoming, sizeof(incoming), 0,
            (struct sockaddr *)&sender_addr, &sender_length);

        if (received == SOCKET_ERROR)
        {
            int error = WSAGetLastError();

            if (error == WSAECONNRESET)
            {
                printf("[WARNING] Ignoring UDP connection-reset event\n");
                continue;
            }

            printf("[ERROR] Receive failed: %d\n", error);
            continue;
        }

        stats.received++;

        if (received < HEADER_SIZE)
        {
            stats.invalid++;
            printf("[DROP] Datagram shorter than packet header\n");
            continue;
        }

        Packet packet = {0};

        if (deserialize_packet(&packet, incoming) != 0 ||
            received != HEADER_SIZE + packet.payload_length)
        {
            stats.invalid++;
            printf("[DROP] Invalid packet format\n");
            continue;
        }

        int is_end = packet.type == PACKET_END;

        if (has_held)
        {
            if (!is_end && channel_should_reorder(&config))
            {
                stats.reordered++;
                printf("[REORDER] Incoming packet before held packet\n");

                forward_packet(sock, &receiver_addr, incoming,
                               (size_t)received, &config, &stats);
                forward_packet(sock, &receiver_addr, held,
                               held_length, &config, &stats);
            }
            else
            {
                forward_packet(sock, &receiver_addr, held,
                               held_length, &config, &stats);
                forward_packet(sock, &receiver_addr, incoming,
                               (size_t)received, &config, &stats);
            }

            has_held = 0;
            held_length = 0;
        }
        else if (!is_end && channel_should_reorder(&config))
        {
            memcpy(held, incoming, (size_t)received);
            held_length = (size_t)received;
            has_held = 1;
            printf("[HOLD] Packet buffered for possible reordering\n");
        }
        else
        {
            forward_packet(sock, &receiver_addr, incoming,
                           (size_t)received, &config, &stats);
        }

        if (is_end)
        {
            if (auto_exit_on_end)
            {
                printf("[END] End packet processed; exiting automated mode\n");
                stop_requested = 1;
            }
            else
            {
                printf("[END] End packet processed; proxy remains available\n");
            }
        }
    }

    /* Don't leave a packet stranded when stopping the proxy. */
    if (has_held)
    {
        printf("[SHUTDOWN] Releasing held packet\n");
        forward_packet(sock, &receiver_addr, held,
                       held_length, &config, &stats);
    }

    printf("\n=== Channel Summary ===\n");
    printf("Datagrams received: %llu\n", (unsigned long long)stats.received);
    printf("Datagrams forwarded: %llu\n", (unsigned long long)stats.forwarded);
    printf("Datagrams dropped: %llu\n", (unsigned long long)stats.dropped);
    printf("Extra copies sent: %llu\n", (unsigned long long)stats.duplicated);
    printf("Datagrams corrupted: %llu\n", (unsigned long long)stats.corrupted);
    printf("Reordering events: %llu\n", (unsigned long long)stats.reordered);
    printf("Invalid datagrams: %llu\n", (unsigned long long)stats.invalid);

    write_csv_summary(&config, &stats);

    closesocket(sock);
    WSACleanup();
    return 0;
}
