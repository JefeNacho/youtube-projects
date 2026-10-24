#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"

/**
 * ==============================================================================
 * EXTRACTOR MFCC (Mel-Frequency Cepstral Coefficients)
 * Optimizado para ESP32-S3 (SRAM 512KB, arquitectura Xtensa LX7)
 * ==============================================================================
 */

class MFCCExtractor {
public:
    MFCCExtractor() {
        _initialized = false;
    }

    void init() {
        if (_initialized) return;

        // 1. Precalcular Ventana de Hamming: w[n] = 0.54 - 0.46 * cos(2*pi*n / (N-1))
        for (int i = 0; i < FFT_SIZE; i++) {
            _hammingWindow[i] = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * (float)i / (float)(FFT_SIZE - 1));
        }

        // 2. Precalcular Banco de Filtros Mel (26 filtros triangulares entre 300 Hz y 8000 Hz)
        _initMelFilterBank(300.0f, 8000.0f);

        // 3. Precalcular Matriz DCT-II (13 coeficientes x 26 filtros)
        for (int k = 0; k < NUM_MFCC_COEFFS; k++) {
            for (int m = 0; m < NUM_MEL_FILTERS; m++) {
                _dctMatrix[k][m] = cosf((float)M_PI * (float)k * ((float)m + 0.5f) / (float)NUM_MEL_FILTERS);
            }
        }

        _initialized = true;
    }

    /**
     * Calcula la matriz de coeficientes MFCC a partir del búfer de audio PCM de 1 segundo.
     * @param audioBuffer Puntero a las 16000 muestras PCM de 16 bits
     * @param outMfccMatrix Puntero a la matriz resultante [NUM_FRAMES][NUM_MFCC_COEFFS]
     */
    void extract(const int16_t* audioBuffer, float outMfccMatrix[NUM_FRAMES][NUM_MFCC_COEFFS]) {
        if (!_initialized) init();

        double mean = 0.0;
        double sumSquares = 0.0;
        for (int i = 0; i < TOTAL_SAMPLES; i++) mean += audioBuffer[i];
        mean /= TOTAL_SAMPLES;
        for (int i = 0; i < TOTAL_SAMPLES; i++) {
            double centered = audioBuffer[i] - mean;
            sumSquares += centered * centered;
        }
        float rms = (float)sqrt(sumSquares / TOTAL_SAMPLES);
        float gain = rms > 100.0f ? MFCC_TARGET_RMS / rms : 1.0f;

        static float frame[FFT_SIZE];
        static float realFFT[FFT_SIZE];
        static float imagFFT[FFT_SIZE];
        static float powerSpectrum[FFT_SIZE / 2 + 1];
        static float melEnergies[NUM_MEL_FILTERS];

        for (int f = 0; f < NUM_FRAMES; f++) {
            int startSample = f * FRAME_STEP;

            // Pre-énfasis: y[n] = x[n] - 0.97 * x[n-1] + Ventana Hamming
            float prevSample = (startSample > 0) ? (float)audioBuffer[startSample - 1] : 0.0f;
            for (int i = 0; i < FFT_SIZE; i++) {
                float currentSample = ((float)audioBuffer[startSample + i] - (float)mean) * gain;
                currentSample = constrain(currentSample, -32768.0f, 32767.0f);
                float emphasized = currentSample - 0.97f * prevSample;
                prevSample = currentSample;
                frame[i] = emphasized * _hammingWindow[i];
                realFFT[i] = frame[i];
                imagFFT[i] = 0.0f;
            }

            // FFT Radix-2
            _computeFFT(realFFT, imagFFT, FFT_SIZE);

            // Espectro de Potencia: P(k) = (real^2 + imag^2) / FFT_SIZE
            for (int k = 0; k <= FFT_SIZE / 2; k++) {
                powerSpectrum[k] = (realFFT[k] * realFFT[k] + imagFFT[k] * imagFFT[k]) / (float)FFT_SIZE;
                if (powerSpectrum[k] < 1e-6f) powerSpectrum[k] = 1e-6f; // Evitar log(0)
            }

            // Filtrado Mel + Logaritmo
            for (int m = 0; m < NUM_MEL_FILTERS; m++) {
                float energy = 0.0f;
                int startBin = _filterStartBin[m];
                int centerBin = _filterCenterBin[m];
                int stopBin = _filterStopBin[m];

                // Rama ascendente del triángulo
                for (int k = startBin; k < centerBin; k++) {
                    float weight = (float)(k - startBin) / (float)(centerBin - startBin);
                    energy += powerSpectrum[k] * weight;
                }
                // Rama descendente del triángulo
                for (int k = centerBin; k <= stopBin; k++) {
                    float weight = (float)(stopBin - k) / (float)(stopBin - centerBin);
                    energy += powerSpectrum[k] * weight;
                }

                melEnergies[m] = logf(fmaxf(energy, 1e-6f));
            }

            // Transformada de Coseno Discreta (DCT-II) para obtener 13 MFCCs
            for (int k = 0; k < NUM_MFCC_COEFFS; k++) {
                float sum = 0.0f;
                for (int m = 0; m < NUM_MEL_FILTERS; m++) {
                    sum += melEnergies[m] * _dctMatrix[k][m];
                }
                // Liftering sinusoidal para suavizar coeficientes de alto orden
                float lifter = 1.0f + 11.0f * sinf((float)M_PI * (float)k / 22.0f);
                outMfccMatrix[f][k] = sum * lifter;
            }
        }
    }

    /**
     * Calcula la energía RMS del búfer para Voice Activity Detection (VAD).
     */
    static float calculateRMS(const int16_t* buffer, int numSamples) {
        if (numSamples <= 0) return 0.0f;
        double sum = 0.0;
        double sumSquares = 0.0;
        for (int i = 0; i < numSamples; i++) {
            sum += (double)buffer[i];
        }
        double mean = sum / (double)numSamples;
        for (int i = 0; i < numSamples; i++) {
            double centeredSample = (double)buffer[i] - mean;
            sumSquares += centeredSample * centeredSample;
        }
        return (float)sqrt(sumSquares / (double)numSamples);
    }

private:
    bool _initialized;
    float _hammingWindow[FFT_SIZE];
    float _dctMatrix[NUM_MFCC_COEFFS][NUM_MEL_FILTERS];
    int _filterStartBin[NUM_MEL_FILTERS];
    int _filterCenterBin[NUM_MEL_FILTERS];
    int _filterStopBin[NUM_MEL_FILTERS];

    static float _hzToMel(float hz) {
        return 2595.0f * log10f(1.0f + hz / 700.0f);
    }

    static float _melToHz(float mel) {
        return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
    }

    void _initMelFilterBank(float lowFreqHz, float highFreqHz) {
        float lowMel = _hzToMel(lowFreqHz);
        float highMel = _hzToMel(highFreqHz);
        float melDelta = (highMel - lowMel) / (float)(NUM_MEL_FILTERS + 1);

        int bins[NUM_MEL_FILTERS + 2];
        for (int i = 0; i < NUM_MEL_FILTERS + 2; i++) {
            float currentMel = lowMel + (float)i * melDelta;
            float freqHz = _melToHz(currentMel);
            bins[i] = (int)floorf((float)(FFT_SIZE + 1) * freqHz / (float)AUDIO_SAMPLE_RATE);
            if (bins[i] > FFT_SIZE / 2) bins[i] = FFT_SIZE / 2;
        }

        for (int m = 0; m < NUM_MEL_FILTERS; m++) {
            _filterStartBin[m]  = bins[m];
            _filterCenterBin[m] = bins[m + 1];
            _filterStopBin[m]   = bins[m + 2];
            // Asegurar que haya al menos 1 bin de diferencia
            if (_filterCenterBin[m] <= _filterStartBin[m]) _filterCenterBin[m] = _filterStartBin[m] + 1;
            if (_filterStopBin[m] <= _filterCenterBin[m]) _filterStopBin[m] = _filterCenterBin[m] + 1;
        }
    }

    // FFT Cooley-Tukey Radix-2 In-Place
    void _computeFFT(float* real, float* imag, int n) {
        // Bit-reversal permutation
        int j = 0;
        for (int i = 0; i < n - 1; i++) {
            if (i < j) {
                float tempR = real[i]; real[i] = real[j]; real[j] = tempR;
                float tempI = imag[i]; imag[i] = imag[j]; imag[j] = tempI;
            }
            int k = n >> 1;
            while (k <= j) {
                j -= k;
                k >>= 1;
            }
            j += k;
        }

        // Mariposas de Cooley-Tukey
        for (int len = 2; len <= n; len <<= 1) {
            float angle = -2.0f * (float)M_PI / (float)len;
            float wlenR = cosf(angle);
            float wlenI = sinf(angle);
            for (int i = 0; i < n; i += len) {
                float wR = 1.0f;
                float wI = 0.0f;
                for (int k = 0; k < len / 2; k++) {
                    float uR = real[i + k];
                    float uI = imag[i + k];
                    float vR = real[i + k + len / 2] * wR - imag[i + k + len / 2] * wI;
                    float vI = real[i + k + len / 2] * wI + imag[i + k + len / 2] * wR;

                    real[i + k] = uR + vR;
                    imag[i + k] = uI + vI;
                    real[i + k + len / 2] = uR - vR;
                    imag[i + k + len / 2] = uI - vI;

                    float nextWR = wR * wlenR - wI * wlenI;
                    float nextWI = wR * wlenI + wI * wlenR;
                    wR = nextWR;
                    wI = nextWI;
                }
            }
        }
    }
};
