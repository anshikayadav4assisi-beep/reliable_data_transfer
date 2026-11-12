
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "../emulator/channel.h"

/* Test 1: Default configuration */
static int test_default_configuration(void)
{
    ChannelConfig config;
    channel_config_init(&config);

    return config.loss_probability == 0.0 &&
           config.duplication_probability == 0.0 &&
           config.reordering_probability == 0.0 &&
           config.corruption_probability == 0.0 &&
           config.min_delay_ms == 0 &&
           config.max_delay_ms == 0 &&
           config.seed == 1;
}

/* Test 2: Invalid configuration rejection */
static int test_invalid_configuration(void)
{
    ChannelConfig config;
    channel_config_init(&config);

    if (!channel_config_valid(&config))
        return 0;

    config.loss_probability = -0.1;
    if (channel_config_valid(&config))
        return 0;

    channel_config_init(&config);
    config.loss_probability = 1.1;
    if (channel_config_valid(&config))
        return 0;

    channel_config_init(&config);
    config.duplication_probability = -0.1;
    if (channel_config_valid(&config))
        return 0;

    channel_config_init(&config);
    config.reordering_probability = 1.1;
    if (channel_config_valid(&config))
        return 0;

    channel_config_init(&config);
    config.corruption_probability = -0.1;
    if (channel_config_valid(&config))
        return 0;

    channel_config_init(&config);
    config.min_delay_ms = 100;
    config.max_delay_ms = 50;
    if (channel_config_valid(&config))
        return 0;

    return !channel_config_valid(NULL);
}

/* Test 3: Loss probability boundaries */
static int test_loss_probability_boundaries(void)
{
    ChannelConfig config;
    channel_config_init(&config);
    channel_init(&config);

    config.loss_probability = 0.0;

    for (int i = 0; i < 100; i++)
    {
        if (channel_should_drop(&config))
            return 0;
    }

    config.loss_probability = 1.0;

    for (int i = 0; i < 100; i++)
    {
        if (!channel_should_drop(&config))
            return 0;
    }

    return !channel_should_drop(NULL);
}

/* Test 4: Seed reproducibility */
static int test_seed_reproducibility(void)
{
    ChannelConfig config;
    channel_config_init(&config);
    config.seed = 12345;

    channel_init(&config);

    double first_sequence[5];

    for (int i = 0; i < 5; i++)
        first_sequence[i] = channel_random();

    channel_init(&config);

    for (int i = 0; i < 5; i++)
    {
        if (first_sequence[i] != channel_random())
            return 0;
    }

    return 1;
}

/* Test 5: Delay range */
static int test_delay_range(void)
{
    channel_init(NULL);

    for (int i = 0; i < 1000; i++)
    {
        unsigned int delay = channel_random_delay(10, 50);

        if (delay < 10 || delay > 50)
            return 0;
    }

    if (channel_random_delay(25, 25) != 25)
        return 0;

    if (channel_random_delay(50, 10) != 50)
        return 0;

    ChannelConfig config;
    channel_config_init(&config);
    config.min_delay_ms = 20;
    config.max_delay_ms = 40;

    for (int i = 0; i < 1000; i++)
    {
        unsigned int delay = channel_get_delay(&config);

        if (delay < 20 || delay > 40)
            return 0;
    }

    return channel_get_delay(NULL) == 0;
}

/* Test 6: Packet corruption */
static int test_packet_corruption(void)
{
    uint8_t packet[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t original[sizeof(packet)];

    for (size_t i = 0; i < sizeof(packet); i++)
        original[i] = packet[i];

    if (!channel_corrupt_packet(packet, sizeof(packet)))
        return 0;

    int changed = 0;

    for (size_t i = 0; i < sizeof(packet); i++)
    {
        if (packet[i] != original[i])
            changed = 1;
    }

    if (!changed)
        return 0;

    if (channel_corrupt_packet(NULL, sizeof(packet)))
        return 0;

    return !channel_corrupt_packet(packet, 0);
}

/* Test 7: Packet duplication */
static int test_packet_duplication(void)
{
    uint8_t original[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t duplicate[sizeof(original)] = {0};

    if (!channel_duplicate_packet(
            original, sizeof(original),
            duplicate, sizeof(duplicate)))
        return 0;

    for (size_t i = 0; i < sizeof(original); i++)
    {
        if (original[i] != duplicate[i])
            return 0;
    }

    uint8_t small_buffer[2];

    if (channel_duplicate_packet(
            original, sizeof(original),
            small_buffer, sizeof(small_buffer)))
        return 0;

    if (channel_duplicate_packet(
            NULL, sizeof(original),
            duplicate, sizeof(duplicate)))
        return 0;

    return !channel_duplicate_packet(
        original, 0, duplicate, sizeof(duplicate));
}

/* Test 8: Buffer store and release */
static int test_buffer_store_and_release(void)
{
    uint8_t original[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t output[sizeof(original)] = {0};
    size_t output_length = 0;

    ChannelPacketBuffer buffer;
    channel_buffer_init(&buffer);

    if (buffer.occupied != 0)
        return 0;

    if (!channel_buffer_store(
            &buffer, original, sizeof(original)))
        return 0;

    if (buffer.occupied != 1)
        return 0;

    if (channel_buffer_store(
            &buffer, original, sizeof(original)))
        return 0;

    if (!channel_buffer_release(
            &buffer, output, sizeof(output), &output_length))
        return 0;

    if (output_length != sizeof(original))
        return 0;

    for (size_t i = 0; i < sizeof(original); i++)
    {
        if (output[i] != original[i])
            return 0;
    }

    return buffer.occupied == 0;
}

/* Test 9: Invalid buffer operations */
static int test_buffer_invalid_operations(void)
{
    ChannelPacketBuffer buffer;
    channel_buffer_init(&buffer);

    uint8_t packet[] = {0xAA, 0xBB, 0xCC};
    uint8_t output[sizeof(packet)];
    size_t output_length = 0;

    if (channel_buffer_store(NULL, packet, sizeof(packet)))
        return 0;

    if (channel_buffer_store(&buffer, NULL, sizeof(packet)))
        return 0;

    if (channel_buffer_store(&buffer, packet, 0))
        return 0;

    uint8_t oversized[CHANNEL_MAX_PACKET_SIZE + 1] = {0};

    if (channel_buffer_store(
            &buffer, oversized, sizeof(oversized)))
        return 0;

    if (channel_buffer_release(
            &buffer, output, sizeof(output), &output_length))
        return 0;

    if (!channel_buffer_store(
            &buffer, packet, sizeof(packet)))
        return 0;

    if (channel_buffer_release(
            &buffer, output, 1, &output_length))
        return 0;

    if (buffer.occupied != 1)
        return 0;

    if (channel_buffer_release(
            &buffer, NULL, sizeof(output), &output_length))
        return 0;

    return !channel_buffer_release(
        &buffer, output, sizeof(output), NULL);
}

/* Test 10: Statistics initialization */
static int test_stats_initialization(void)
{
    ChannelStats stats = {1, 2, 3, 4, 5};

    channel_stats_init(&stats);

    if (stats.packets_received != 0 ||
        stats.packets_dropped != 0 ||
        stats.packets_duplicated != 0 ||
        stats.packets_corrupted != 0 ||
        stats.packets_reordered != 0)
        return 0;

    channel_stats_init(NULL);
    return 1;
}

/* Test 11: Actual pair-reordering helper */
static int test_packet_reordering(void)
{
    uint8_t packet_a[] = {0xA1, 0xA2, 0xA3};
    uint8_t packet_b[] = {0xB1, 0xB2};

    uint8_t first_output[CHANNEL_MAX_PACKET_SIZE];
    uint8_t second_output[CHANNEL_MAX_PACKET_SIZE];

    size_t first_length = 0;
    size_t second_length = 0;

    ChannelPacketBuffer buffer;
    channel_buffer_init(&buffer);

    /* Buffer packet A first */
    if (!channel_buffer_store(
            &buffer, packet_a, sizeof(packet_a)))
        return 0;

    /* Packet B should be delivered before buffered packet A */
    if (!channel_reorder_pair(
            &buffer,
            packet_b, sizeof(packet_b),
            first_output, sizeof(first_output), &first_length,
            second_output, sizeof(second_output), &second_length))
        return 0;

    /* First output must be B */
    if (first_length != sizeof(packet_b))
        return 0;

    for (size_t i = 0; i < sizeof(packet_b); i++)
    {
        if (first_output[i] != packet_b[i])
            return 0;
    }

    /* Second output must be A */
    if (second_length != sizeof(packet_a))
        return 0;

    for (size_t i = 0; i < sizeof(packet_a); i++)
    {
        if (second_output[i] != packet_a[i])
            return 0;
    }

    /* Successful reordering must empty the buffer */
    if (buffer.occupied != 0)
        return 0;

    /* Reordering without a buffered packet must fail */
    if (channel_reorder_pair(
            &buffer,
            packet_b, sizeof(packet_b),
            first_output, sizeof(first_output), &first_length,
            second_output, sizeof(second_output), &second_length))
        return 0;

    return 1;
}

/* Print test result */
static void run_test(const char *name, int result)
{
    printf("[%s] %s\n", result ? "PASS" : "FAIL", name);
}

int main(void)
{
    int passed = 0;
    int result;
    const int total = 11;

    printf("=== Channel Emulator Tests ===\n");

    result = test_default_configuration();
    run_test("Default configuration", result);
    passed += result;

    result = test_invalid_configuration();
    run_test("Invalid configuration rejected", result);
    passed += result;

    result = test_loss_probability_boundaries();
    run_test("Loss probability boundaries", result);
    passed += result;

    result = test_seed_reproducibility();
    run_test("Seed reproducibility", result);
    passed += result;

    result = test_delay_range();
    run_test("Delay range", result);
    passed += result;

    result = test_packet_corruption();
    run_test("Packet corruption", result);
    passed += result;

    result = test_packet_duplication();
    run_test("Packet duplication", result);
    passed += result;

    result = test_buffer_store_and_release();
    run_test("Buffer store and release", result);
    passed += result;

    result = test_buffer_invalid_operations();
    run_test("Buffer invalid operations", result);
    passed += result;

    result = test_stats_initialization();
    run_test("Statistics initialization", result);
    passed += result;

    result = test_packet_reordering();
    run_test("Packet reordering", result);
    passed += result;

    printf("\nResult: %d/%d tests passed\n", passed, total);

    return passed == total ? 0 : 1;
}
