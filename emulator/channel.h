
#ifndef CHANNEL_H
#define CHANNEL_H

#include <stddef.h>
#include <stdint.h>

#define CHANNEL_MAX_PACKET_SIZE 2048

/* Configuration for the simulated network channel */
typedef struct
{
    double loss_probability;
    double duplication_probability;
    double reordering_probability;
    double corruption_probability;

    unsigned int min_delay_ms;
    unsigned int max_delay_ms;

    uint32_t seed;
} ChannelConfig;

/* Statistics maintained by the channel emulator */
typedef struct
{
    uint64_t packets_received;
    uint64_t packets_dropped;
    uint64_t packets_duplicated;
    uint64_t packets_corrupted;
    uint64_t packets_reordered;
} ChannelStats;

/* Temporary storage for one packet awaiting delivery */
typedef struct
{
    uint8_t data[CHANNEL_MAX_PACKET_SIZE];
    size_t length;
    int occupied;
} ChannelPacketBuffer;

/* Configuration functions */
void channel_config_init(ChannelConfig *config);
int channel_config_valid(const ChannelConfig *config);

/* Random-number generation */
void channel_init(const ChannelConfig *config);
double channel_random(void);
unsigned int channel_random_delay(
    unsigned int min,
    unsigned int max
);

/* Network impairment decisions */
int channel_should_drop(const ChannelConfig *config);
int channel_should_duplicate(const ChannelConfig *config);
int channel_should_reorder(const ChannelConfig *config);
int channel_should_corrupt(const ChannelConfig *config);
unsigned int channel_get_delay(const ChannelConfig *config);

/* Packet manipulation */
int channel_corrupt_packet(
    uint8_t *packet,
    size_t packet_length
);

int channel_duplicate_packet(
    const uint8_t *packet,
    size_t packet_length,
    uint8_t *duplicate,
    size_t duplicate_capacity
);

/* Packet buffering */
void channel_buffer_init(ChannelPacketBuffer *buffer);

int channel_buffer_store(
    ChannelPacketBuffer *buffer,
    const uint8_t *packet,
    size_t packet_length
);

int channel_buffer_release(
    ChannelPacketBuffer *buffer,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length
);

/* Reorder two packets: deliver the incoming packet first */
int channel_reorder_pair(
    ChannelPacketBuffer *buffer,
    const uint8_t *incoming_packet,
    size_t incoming_length,
    uint8_t *first_output,
    size_t first_capacity,
    size_t *first_length,
    uint8_t *second_output,
    size_t second_capacity,
    size_t *second_length
);

/* Statistics */
void channel_stats_init(ChannelStats *stats);

#endif
