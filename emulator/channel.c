
#include "channel.h"

#include <stddef.h>

/* Private random state */
static uint32_t random_state = 1;

/* Generate reproducible pseudo-random numbers using xorshift32 */
static uint32_t channel_random_u32(void)
{
    uint32_t x = random_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    if (x == 0)
        x = 1;

    random_state = x;
    return x;
}

void channel_config_init(ChannelConfig *config)
{
    if (config == NULL)
        return;

    config->loss_probability = 0.0;
    config->duplication_probability = 0.0;
    config->reordering_probability = 0.0;
    config->corruption_probability = 0.0;
    config->min_delay_ms = 0;
    config->max_delay_ms = 0;
    config->seed = 1;
}

int channel_config_valid(const ChannelConfig *config)
{
    if (config == NULL)
        return 0;

    if (config->loss_probability < 0.0 ||
        config->loss_probability > 1.0)
        return 0;

    if (config->duplication_probability < 0.0 ||
        config->duplication_probability > 1.0)
        return 0;

    if (config->reordering_probability < 0.0 ||
        config->reordering_probability > 1.0)
        return 0;

    if (config->corruption_probability < 0.0 ||
        config->corruption_probability > 1.0)
        return 0;

    return config->min_delay_ms <= config->max_delay_ms;
}

void channel_init(const ChannelConfig *config)
{
    if (config == NULL || config->seed == 0)
        random_state = 1;
    else
        random_state = config->seed;
}

void channel_stats_init(ChannelStats *stats)
{
    if (stats == NULL)
        return;

    stats->packets_received = 0;
    stats->packets_dropped = 0;
    stats->packets_duplicated = 0;
    stats->packets_corrupted = 0;
    stats->packets_reordered = 0;
}

double channel_random(void)
{
    return (double)channel_random_u32() / 4294967296.0;
}

unsigned int channel_random_delay(
    unsigned int min,
    unsigned int max)
{
    if (min >= max)
        return min;

    uint64_t range = (uint64_t)max - min + 1;
    return min + (unsigned int)(channel_random_u32() % range);
}

static int channel_event_occurs(double probability)
{
    if (probability <= 0.0)
        return 0;

    if (probability >= 1.0)
        return 1;

    return channel_random() < probability;
}

int channel_should_drop(const ChannelConfig *config)
{
    return config != NULL &&
           channel_event_occurs(config->loss_probability);
}

int channel_should_duplicate(const ChannelConfig *config)
{
    return config != NULL &&
           channel_event_occurs(config->duplication_probability);
}

int channel_should_reorder(const ChannelConfig *config)
{
    return config != NULL &&
           channel_event_occurs(config->reordering_probability);
}

int channel_should_corrupt(const ChannelConfig *config)
{
    return config != NULL &&
           channel_event_occurs(config->corruption_probability);
}

unsigned int channel_get_delay(const ChannelConfig *config)
{
    if (config == NULL)
        return 0;

    return channel_random_delay(
        config->min_delay_ms,
        config->max_delay_ms
    );
}

int channel_corrupt_packet(
    uint8_t *packet,
    size_t packet_length)
{
    if (packet == NULL || packet_length == 0)
        return 0;

    size_t byte_index =
        (size_t)(channel_random_u32() % packet_length);

    unsigned int bit_index = channel_random_u32() % 8;

    packet[byte_index] ^= (uint8_t)(1u << bit_index);
    return 1;
}

int channel_duplicate_packet(
    const uint8_t *packet,
    size_t packet_length,
    uint8_t *duplicate,
    size_t duplicate_capacity)
{
    if (packet == NULL || duplicate == NULL ||
        packet_length == 0 ||
        packet_length > duplicate_capacity)
        return 0;

    for (size_t i = 0; i < packet_length; i++)
        duplicate[i] = packet[i];

    return 1;
}

void channel_buffer_init(ChannelPacketBuffer *buffer)
{
    if (buffer == NULL)
        return;

    buffer->length = 0;
    buffer->occupied = 0;
}

int channel_buffer_store(
    ChannelPacketBuffer *buffer,
    const uint8_t *packet,
    size_t packet_length)
{
    if (buffer == NULL || packet == NULL ||
        packet_length == 0 ||
        packet_length > CHANNEL_MAX_PACKET_SIZE ||
        buffer->occupied)
        return 0;

    for (size_t i = 0; i < packet_length; i++)
        buffer->data[i] = packet[i];

    buffer->length = packet_length;
    buffer->occupied = 1;
    return 1;
}

int channel_buffer_release(
    ChannelPacketBuffer *buffer,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    if (buffer == NULL || output == NULL ||
        output_length == NULL || !buffer->occupied ||
        output_capacity < buffer->length)
        return 0;

    for (size_t i = 0; i < buffer->length; i++)
        output[i] = buffer->data[i];

    *output_length = buffer->length;
    buffer->length = 0;
    buffer->occupied = 0;
    return 1;
}

int channel_reorder_pair(
    ChannelPacketBuffer *buffer,
    const uint8_t *incoming_packet,
    size_t incoming_length,
    uint8_t *first_output,
    size_t first_capacity,
    size_t *first_length,
    uint8_t *second_output,
    size_t second_capacity,
    size_t *second_length)
{
    if (buffer == NULL || incoming_packet == NULL ||
        first_output == NULL || second_output == NULL ||
        first_length == NULL || second_length == NULL ||
        !buffer->occupied || buffer->length == 0 ||
        incoming_length == 0 ||
        incoming_length > CHANNEL_MAX_PACKET_SIZE ||
        first_capacity < incoming_length ||
        second_capacity < buffer->length)
        return 0;

    for (size_t i = 0; i < incoming_length; i++)
        first_output[i] = incoming_packet[i];

    for (size_t i = 0; i < buffer->length; i++)
        second_output[i] = buffer->data[i];

    *first_length = incoming_length;
    *second_length = buffer->length;

    buffer->length = 0;
    buffer->occupied = 0;
    return 1;
}
