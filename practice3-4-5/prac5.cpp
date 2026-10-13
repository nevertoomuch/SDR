#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <complex.h>
#include <math.h>   
#include <string.h> 

#define TX_GAIN_DB -20.0   
#define RX_GAIN_DB 30.0
#define TONE_HZ 0
#define SAMPLE_RATE  1e6

int main(int argc, char *argv[])
{
    int is_node_a = 1;
    const char *uri = "usb:";
    size_t iteration_count = 5000; 

    if (argc > 1) {
        if (strcmp(argv[1], "2") == 0 || strcmp(argv[1], "B") == 0 || strcmp(argv[1], "node_B") == 0) {
            is_node_a = 0;
        }
    }
    if (argc > 2) {
        uri = argv[2];
    }
    if (argc > 3) {
        iteration_count = (size_t)atol(argv[3]); 
    }

    double tx_freq = is_node_a ? 800e6 : 840e6;
    double rx_freq = is_node_a ? 840e6 : 800e6;

    printf("Запуск: %s \n", is_node_a ? "Узел А (TX 800 МГц, RX 840 МГц)" : "Узел Б (TX 840 МГц, RX 800 МГц)");
    printf("URI устройства: %s, буферов: %zu\n", uri, iteration_count);

    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", uri);
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");
    SoapySDRKwargs_set(&args, "loopback", "0");

    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (sdr == NULL)
    {
        printf("SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, SAMPLE_RATE) != 0) {
        printf("setSampleRate rx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, rx_freq, NULL) != 0) {
        printf("setFrequency rx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, SAMPLE_RATE) != 0) {
        printf("setSampleRate tx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, tx_freq, NULL) != 0) {
        printf("setFrequency tx fail: %s\n", SoapySDRDevice_lastError());
    }
    printf("SoapySDRDevice_getFrequency tx: %lf\n", SoapySDRDevice_getFrequency(sdr, SOAPY_SDR_TX, 0));

    size_t channels[] = {0};
    size_t channel_count = sizeof(channels) / sizeof(channels[0]);

    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (rxStream == NULL) {
        printf("setupStream rx fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    if (SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channels[0], RX_GAIN_DB) != 0) {
        printf("setGain rx fail: %s\n", SoapySDRDevice_lastError());
    }
    if (SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channels[0], TX_GAIN_DB) != 0) {
        printf("setGain tx fail: %s\n", SoapySDRDevice_lastError());
    }

    SoapySDRStream *txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, channels, channel_count, NULL);
    if (txStream == NULL) {
        printf("setupStream tx fail: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);
    printf("MTU - TX: %zu, RX: %zu\n", tx_mtu, rx_mtu);

    int16_t *tx_buff = (int16_t *)malloc(2 * tx_mtu * sizeof(int16_t));
    int16_t *rx_buffer = (int16_t *)malloc(2 * rx_mtu * sizeof(int16_t));

    for (size_t k = 0; k < tx_mtu; k++) {
        double ph = 2.0 * M_PI * TONE_HZ * (double)k / SAMPLE_RATE;
        tx_buff[2 * k]     = (int16_t)(2000.0 * cos(ph)); // I
        tx_buff[2 * k + 1] = (int16_t)(2000.0 * sin(ph)); // Q
    }

    for (size_t i = 0; i < 2; i++) {
        tx_buff[0 + i] = 0xffff;
        tx_buff[10 + i] = 0xffff;
    }

    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0);
    SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0);

    printf("Start test...\n");
    const long timeoutUs = 400000;
    long long last_time = 0;

    for (size_t buffers_read = 0; buffers_read < 128; ) {
        void *buffs[] = {rx_buffer};
        int flags;
        long long timeNs;
        int sr = SoapySDRDevice_readStream(sdr, rxStream, buffs, rx_mtu, &flags, &timeNs, timeoutUs);
        if (sr > 0) buffers_read++;
    }

    FILE *tx_file = fopen("txdata.pcm", "wb"); 
    FILE *rx_file = fopen("rxdata.pcm", "wb"); 

    for (size_t buffers_read = 0; buffers_read < iteration_count; buffers_read++) {
        void *rx_buffs[] = {rx_buffer};
        int flags;
        long long timeNs;

        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
        if (sr < 0) continue;

        if (buffers_read % 50 == 0) {
            int peak = 0;
            for (int i = 0; i < 2 * sr; i++) {
                int v = abs(rx_buffer[i]);
                if (v > peak) peak = v;
            }
            printf("Buffer: %zu - Samples: %i, Flags: %i, Time: %lli, TimeDiff: %lli, peak: %d\n",
                   buffers_read, sr, flags, timeNs, timeNs - last_time, peak);
        }

        if (rx_file) fwrite(rx_buffer, 2 * rx_mtu * sizeof(int16_t), 1, rx_file);
        last_time = timeNs;

        long long tx_time = timeNs + (4 * 1000 * 1000);

        for (size_t i = 0; i < 8; i++) {
            uint8_t tx_time_byte = (tx_time >> (i * 8)) & 0xff;
            tx_buff[2 + i] = tx_time_byte << 4;
        }

        if (tx_file) fwrite(tx_buff, 2 * tx_mtu * sizeof(int16_t), 1, tx_file);

        void *tx_buffs[] = {tx_buff};
        if (buffers_read >= 2) { 
            if (buffers_read == 2) printf("buffers_read: %zu -> Начало передачи TX\n", buffers_read);
            flags = SOAPY_SDR_HAS_TIME;
            int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const*)tx_buffs, tx_mtu, &flags, tx_time, timeoutUs);
            if (st < 0) {
                printf("TX Failed: %i (%s)\n", st, SoapySDRDevice_lastError());
            }
        }
    }

    if (tx_file) fclose(tx_file);
    if (rx_file) fclose(rx_file);

    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_closeStream(sdr, txStream);
    SoapySDRDevice_unmake(sdr);

    free(tx_buff);
    free(rx_buffer);

    printf("test complete!\n");
    return EXIT_SUCCESS;
}