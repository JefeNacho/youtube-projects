#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"

/**
 * ==============================================================================
 * COMPARADOR DTW (Dynamic Time Warping) con Banda de Sakoe-Chiba
 * ==============================================================================
 */

class DTWMatcher {
public:
    /**
     * Calcula la distancia de deformación temporal dinámica entre dos matrices MFCC.
     * @param seqA Matriz MFCC de prueba [NUM_FRAMES][NUM_MFCC_COEFFS]
     * @param seqB Matriz MFCC de referencia/enrolada [NUM_FRAMES][NUM_MFCC_COEFFS]
     * @return Distancia acumulada normalizada (menor valor = mayor similitud acústica)
     */
    static float computeDistance(
        const float seqA[NUM_FRAMES][NUM_MFCC_COEFFS],
        const float seqB[NUM_FRAMES][NUM_MFCC_COEFFS]
    ) {
        static float dtw[NUM_FRAMES][NUM_FRAMES];
        const float INF = 1e9f;

        // Inicializar matriz de costos
        for (int i = 0; i < NUM_FRAMES; i++) {
            for (int j = 0; j < NUM_FRAMES; j++) {
                dtw[i][j] = INF;
            }
        }

        // Costo del punto de partida
        dtw[0][0] = _euclideanDistance(seqA[0], seqB[0]);

        // Llenar primera fila y primera columna dentro de la banda Sakoe-Chiba
        for (int i = 1; i < NUM_FRAMES && i <= DTW_SAKOE_CHIBA_BAND; i++) {
            dtw[i][0] = dtw[i - 1][0] + _euclideanDistance(seqA[i], seqB[0]);
        }
        for (int j = 1; j < NUM_FRAMES && j <= DTW_SAKOE_CHIBA_BAND; j++) {
            dtw[0][j] = dtw[0][j - 1] + _euclideanDistance(seqA[0], seqB[j]);
        }

        // Programación Dinámica con restricción de banda Sakoe-Chiba
        for (int i = 1; i < NUM_FRAMES; i++) {
            int jStart = max(1, i - DTW_SAKOE_CHIBA_BAND);
            int jEnd   = min(NUM_FRAMES - 1, i + DTW_SAKOE_CHIBA_BAND);

            for (int j = jStart; j <= jEnd; j++) {
                float cost = _euclideanDistance(seqA[i], seqB[j]);
                float minPrev = dtw[i - 1][j]; // Inserción
                if (dtw[i][j - 1] < minPrev)     minPrev = dtw[i][j - 1];     // Eliminación
                if (dtw[i - 1][j - 1] < minPrev) minPrev = dtw[i - 1][j - 1]; // Coincidencia / Diagonal

                if (minPrev < INF) {
                    dtw[i][j] = cost + minPrev;
                }
            }
        }

        // Retornar costo acumulado en el destino normalizado por número de frames
        float totalDist = dtw[NUM_FRAMES - 1][NUM_FRAMES - 1];
        if (totalDist >= INF) return 999.0f; // Fuera de la banda de deformación permitida
        return totalDist / (float)NUM_FRAMES;
    }

    /**
     * Compara contra un conjunto de plantillas enroladas y retorna la menor distancia.
     */
    static float matchAgainstTemplates(
        const float inputMFCC[NUM_FRAMES][NUM_MFCC_COEFFS],
        const float enrolledTemplates[NUM_ENROLL_TEMPLATES][NUM_FRAMES][NUM_MFCC_COEFFS],
        int validTemplatesCount
    ) {
        if (validTemplatesCount <= 0) return 999.0f;

        float bestDistance = 999.0f;
        float secondBestDistance = 999.0f;
        for (int t = 0; t < validTemplatesCount; t++) {
            float dist = computeDistance(inputMFCC, enrolledTemplates[t]);
            if (dist < bestDistance) {
                secondBestDistance = bestDistance;
                bestDistance = dist;
            } else if (dist < secondBestDistance) {
                secondBestDistance = dist;
            }
        }
        if (validTemplatesCount >= 2) {
            // Las dos mejores plantillas deben estar cerca; no basta una sola coincidencia.
            return max(bestDistance, secondBestDistance);
        }
        return bestDistance;
    }

private:
    // Distancia Euclidiana entre dos vectores MFCC de 13 coeficientes
    static inline float _euclideanDistance(const float a[NUM_MFCC_COEFFS], const float b[NUM_MFCC_COEFFS]) {
        float sum = 0.0f;
        for (int k = 0; k < NUM_MFCC_COEFFS; k++) {
            float diff = a[k] - b[k];
            sum += diff * diff;
        }
        return sqrtf(sum);
    }
};
