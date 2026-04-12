# Schema de Conectare ESP32-C6 + MQ-7

```
                                    ESP32-C6-DevKitC-1
                              ┌─────────────────────────────┐
                              │                             │
                              │         USB-C               │
                              │           │                 │
   MQ-7 Module                │           ▼                 │
┌────────────────┐            │                             │
│                │            │   ┌─────────────────────┐   │
│  VCC   ●───────┼────────────┼───┤ 3V3 (sau 5V)        │   │
│                │            │   └─────────────────────┘   │
│  GND   ●───────┼────────────┼───┤ GND                 │   │
│                │            │   └─────────────────────┘   │
│  DOUT  ○       │            │                             │
│                │            │   ┌─────────────────────┐   │
│  AOUT  ●───────┼────────────┼───┤ GPIO0 (ADC1_CH0)    │   │
│                │            │   └─────────────────────┘   │
└────────────────┘            │                             │
                              │                             │
   Buton Pairing              │   ┌─────────────────────┐   │
      ┌───┐                   │   │                     │   │
   ───┤   ├────●──────────────┼───┤ GPIO9 (BUTTON)      │   │
      └───┘    │              │   └─────────────────────┘   │
               │              │   (Pull-up intern)          │
              GND             │                             │
                              │                             │
   LED Status (opțional)      │   ┌─────────────────────┐   │
      Anod (+)                │   │                     │   │
        │                     │   │                     │   │
      ──┼──[220Ω]──●──────────┼───┤ GPIO8 (LED)         │   │
        │                     │   └─────────────────────┘   │
      Catod (-)               │                             │
        │                     │                             │
       GND                    └─────────────────────────────┘


Notă: Pentru sensibilitate maximă MQ-7, alimentează-l cu 5V
      ESP32-C6 poate furniza 5V pe pinul 5V (de la USB)
```

## Detalii Pini

| Component | Pin ESP32-C6 | Funcție | Descriere |
|-----------|-------------|---------|-----------|
| MQ-7 AOUT | GPIO0 | ADC1_CH0 | Citire analogică (0-3.3V) |
| MQ-7 VCC  | 3V3 sau 5V | Power | Alimentare senzor |
| MQ-7 GND  | GND | Ground | Masă comună |
| Buton | GPIO9 | Input + Pull-up | Pairing (3s = deschide rețea) |
| LED | GPIO8 | Output | Indicator status |

## Observații Importante

1. **MQ-7 Alimentare**: 
   - 5V: Sensibilitate maximă (recomandat)
   - 3.3V: Funcționează dar cu sensibilitate redusă
   
2. **ADC ESP32-C6**:
   - Rezoluție: 12-bit (0-4095)
   - Range: 0-3.3V (cu atenuare 11dB poate măsura până la ~3.1V)
   - Dacă MQ-7 e alimentat cu 5V, asigură-te că AOUT nu depășește 3.3V!

3. **Rezistență Load (RL) pe MQ-7**:
   - Majoritatea modulelor MQ-7 au RL = 10kΩ deja integrat
   - Verifică schema modulului tău specific

4. **Warmup MQ-7**:
   - Prima utilizare: 24-48 ore de încălzire continuă
   - Fiecare pornire: minimum 60 secunde
   - Pentru citiri stabile: 10 minute

5. **Buton**:
   - Pull-up intern este activat în cod
   - Nu necesită rezistență externă
   - Apăsare lungă (3s) = deschide rețea pentru pairing
