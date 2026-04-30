#pragma once

#define LCD_H_RES 800
#define LCD_V_RES 480

// --- PINES I2C (Touch GT911 + Coprocesador STC8H) ---
#define I2C_SDA_PIN 15
#define I2C_SCL_PIN 16

// --- BUZZER INTEGRADO ---
#define BUZZER_PIN 8

// --- COPROCESADOR AUDIO/BACKLIGHT ---
// El usuario indicó dirección 0x30 (puede ser 8-bit, 7-bit es 0x18)
#define STC8_I2C_ADDR 0x18 // Probamos con 0x18 porque el código de ref usa 0x18
// O enviaremos comandos en bruto si no funciona

// --- COMANDOS I2C SEGÚN USUARIO ---
#define CMD_BUZZER_ON  246
#define CMD_BUZZER_OFF 247
#define CMD_SPEAKER_ON 248
#define CMD_SPEAKER_OFF 249
