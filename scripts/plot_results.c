#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CSV_FILE "results/channel_stats.csv"
#define OUTPUT_DIR "results/plots"
#define MAX_ROWS 10000

typedef struct {
    double loss, duplication, reordering, corruption;
    unsigned int min_delay, max_delay;
    unsigned long long received, forwarded, dropped;
    unsigned long long duplicated, corrupted, reordered, invalid;
} Experiment;

static int read_experiments(Experiment data[], int capacity) {
    FILE *file = fopen(CSV_FILE, "r");
    if (!file) {
        perror("Cannot open results/channel_stats.csv");
        return -1;
    }

    char line[2048];
    int count = 0;

    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return 0;
    }

    while (fgets(line, sizeof(line), file)) {
        Experiment e = {0};
        char timestamp[128];

        int fields = sscanf(
            line,
            "%127[^,],%lf,%lf,%lf,%lf,%u,%u,"
            "%llu,%llu,%llu,%llu,%llu,%llu,%llu",
            timestamp,
            &e.loss, &e.duplication, &e.reordering, &e.corruption,
            &e.min_delay, &e.max_delay, &e.received, &e.forwarded,
            &e.dropped, &e.duplicated, &e.corrupted, &e.reordered,
            &e.invalid
        );

        if (fields == 14 && count < capacity) {
            data[count++] = e;
        }
    }

    fclose(file);
    return count;
}

static int is_loss_only(const Experiment *e) {
    return e->duplication == 0.0 && e->reordering == 0.0 &&
           e->corruption == 0.0 && e->min_delay == 0 && e->max_delay == 0;
}

static int is_duplication_only(const Experiment *e) {
    return e->loss == 0.0 && e->reordering == 0.0 &&
           e->corruption == 0.0 && e->min_delay == 0 && e->max_delay == 0;
}

static int is_delay_only(const Experiment *e) {
    return e->loss == 0.0 && e->duplication == 0.0 &&
           e->reordering == 0.0 && e->corruption == 0.0;
}

static void start_svg(FILE *f, const char *title, const char *ylabel,
                      double y_max, const char *suffix) {
    fprintf(f,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"900\" height=\"560\" viewBox=\"0 0 900 560\">\n"
        "<rect width=\"900\" height=\"560\" fill=\"white\"/>\n"
        "<text x=\"450\" y=\"35\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"22\">%s</text>\n"
        "<text x=\"25\" y=\"280\" transform=\"rotate(-90 25 280)\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"15\">%s</text>\n",
        title, ylabel);

    for (int i = 0; i <= 5; i++) {
        double value = y_max * i / 5.0;
        double y = 450.0 - value * 375.0 / y_max;
        fprintf(f,
            "<line x1=\"100\" y1=\"%.2f\" x2=\"830\" y2=\"%.2f\" stroke=\"#dddddd\"/>\n"
            "<text x=\"85\" y=\"%.2f\" text-anchor=\"end\" font-family=\"Arial\" font-size=\"12\">%.0f%s</text>\n",
            y, y, y + 4, value, suffix);
    }

    fprintf(f,
        "<line x1=\"100\" y1=\"75\" x2=\"100\" y2=\"450\" stroke=\"black\"/>\n"
        "<line x1=\"100\" y1=\"450\" x2=\"830\" y2=\"450\" stroke=\"black\"/>\n");
}

static void finish_svg(FILE *f, const char *xlabel) {
    fprintf(f,
        "<text x=\"465\" y=\"520\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"15\">%s</text>\n"
        "</svg>\n", xlabel);
}

static void plot_loss(const Experiment data[], int n) {
    FILE *f = fopen(OUTPUT_DIR "/loss_vs_observed.svg", "w");
    if (!f) { perror("loss plot"); return; }

    start_svg(f, "Configured Loss vs Mean Observed Drop Rate",
              "Mean observed drop rate (%)", 100.0, "%");

    const double levels[] = {0.0, 0.1, 0.3, 0.5};

    for (int k = 0; k < 4; k++) {
        double sum = 0.0;
        int trials = 0;

        for (int i = 0; i < n; i++) {
            if (!is_loss_only(&data[i]) ||
                fabs(data[i].loss - levels[k]) > 0.000001 ||
                data[i].received == 0) {
                continue;
            }

            sum += 100.0 * (double)data[i].dropped /
                   (double)data[i].received;
            trials++;
        }

        if (trials == 0) continue;

        double mean = sum / trials;
        double x = 100.0 + levels[k] * 730.0;
        double y = 450.0 - mean * 3.75;

        fprintf(f,
            "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"7\" fill=\"#2563eb\"/>\n"
            "<text x=\"%.2f\" y=\"%.2f\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"12\">%.2f%% (n=%d)</text>\n",
            x, y, x, y - 12.0, mean, trials);
    }

    for (int i = 0; i <= 5; i++) {
        double x = 100.0 + i * 146.0;
        fprintf(f,
            "<text x=\"%.0f\" y=\"470\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"12\">%d</text>\n",
            x, i * 20);
    }

    finish_svg(f, "Configured loss probability (%)");
    fclose(f);
    printf("Created %s/loss_vs_observed.svg\n", OUTPUT_DIR);
}

static void plot_duplication(const Experiment data[], int n) {
    FILE *f = fopen(OUTPUT_DIR "/duplication_vs_copies.svg", "w");
    if (!f) { perror("duplication plot"); return; }

    start_svg(f, "Configured Duplication vs Mean Extra Copies",
              "Mean extra copies per trial", 12.0, "");

    const double levels[] = {0.0, 0.25, 0.5, 0.75};

    for (int k = 0; k < 4; k++) {
        double sum = 0.0;
        int trials = 0;

        for (int i = 0; i < n; i++) {
            if (!is_duplication_only(&data[i]) ||
                fabs(data[i].duplication - levels[k]) > 0.000001) {
                continue;
            }

            sum += (double)data[i].duplicated;
            trials++;
        }

        if (trials == 0) continue;

        double mean = sum / trials;
        double x = 100.0 + levels[k] * 730.0;
        double y = 450.0 - fmin(mean, 12.0) * 31.25;

        fprintf(f,
            "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"7\" fill=\"#16a34a\"/>\n"
            "<text x=\"%.2f\" y=\"%.2f\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"12\">%.2f (n=%d)</text>\n",
            x, y, x, y - 12.0, mean, trials);
    }

    for (int i = 0; i <= 5; i++) {
        double x = 100.0 + i * 146.0;
        fprintf(f,
            "<text x=\"%.0f\" y=\"470\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"12\">%.1f</text>\n",
            x, i / 5.0);
    }

    finish_svg(f, "Configured duplication probability (0 to 1)");
    fclose(f);
    printf("Created %s/duplication_vs_copies.svg\n", OUTPUT_DIR);
}

static void plot_delay(const Experiment data[], int n) {
    FILE *f = fopen(OUTPUT_DIR "/configured_delay.svg", "w");
    if (!f) { perror("delay plot"); return; }

    start_svg(f, "Configured Channel Delay Ranges",
              "Configured delay (ms)", 300.0, "");

    const unsigned int mins[] = {0, 50, 100};
    const unsigned int maxs[] = {0, 100, 300};
    const char *labels[] = {"0-0 ms", "50-100 ms", "100-300 ms"};

    for (int k = 0; k < 3; k++) {
        int trials = 0;

        for (int i = 0; i < n; i++) {
            if (is_delay_only(&data[i]) &&
                data[i].min_delay == mins[k] &&
                data[i].max_delay == maxs[k]) {
                trials++;
            }
        }

        if (trials == 0) continue;

        double x = 220.0 + k * 250.0;
        double y_low = 450.0 - (double)mins[k] * 1.25;
        double y_high = 450.0 - (double)maxs[k] * 1.25;

        fprintf(f,
            "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"#9333ea\" stroke-width=\"8\"/>\n"
            "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"5\" fill=\"#7e22ce\"/>\n"
            "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"5\" fill=\"#7e22ce\"/>\n"
            "<text x=\"%.2f\" y=\"%.2f\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"13\">%s (n=%d)</text>\n",
            x, y_low, x, y_high, x, y_low, x, y_high,
            x, y_high - 12.0, labels[k], trials);
    }

    for (int i = 0; i < 3; i++) {
        double x = 220.0 + i * 250.0;
        fprintf(f,
            "<text x=\"%.0f\" y=\"470\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"12\">%d</text>\n",
            x, i);
    }

    finish_svg(f, "Delay configuration index (0, 1, 2)");
    fclose(f);
    printf("Created %s/configured_delay.svg\n", OUTPUT_DIR);
}

int main(void) {
    Experiment *data = calloc(MAX_ROWS, sizeof(Experiment));
    if (!data) {
        fprintf(stderr, "Memory allocation failed.\n");
        return 1;
    }

    int n = read_experiments(data, MAX_ROWS);
    if (n < 0) {
        free(data);
        return 1;
    }

#ifdef _WIN32
    if (system("if not exist results\\plots mkdir results\\plots") != 0) {
#else
    if (system("mkdir -p results/plots") != 0) {
#endif
        fprintf(stderr, "Could not create plot output directory.\n");
        free(data);
        return 1;
    }

    printf("Read %d CSV experiment rows.\n", n);
    printf("Filtering trials by impairment configuration.\n");
    plot_loss(data, n);
    plot_duplication(data, n);
    plot_delay(data, n);

    free(data);
    return 0;
}
