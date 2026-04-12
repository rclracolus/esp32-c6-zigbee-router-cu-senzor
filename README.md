# ESP32-C6 Zigbee Router cu MQ-7 CO Sensor

Proiect Zigbee router cu detector de monoxid de carbon (CO) și buton de pairing.

## 🔧 Hardware Necesar

### ESP32-C6
- ESP32-C6-DevKitC-1 sau similar
- Suportă Zigbee nativ (IEEE 802.15.4)

### Senzor MQ-7
- **MQ-7**: Detector de monoxid de carbon (CO)
- **Alimentare**: 5V (poate funcționa și pe 3.3V dar cu sensibilitate redusă)
- **Output**: Analogic (0-3.3V sau 0-5V)

### Componente Adiționale
- Buton tactil (push button)
- LED (opțional, pentru indicare status)
- Rezistență 10kΩ pentru pull-up (dacă butonul nu are)
- Rezistență 220Ω pentru LED

## 📐 Schema de Conectare

```
ESP32-C6          MQ-7 Sensor
---------         -----------
  3.3V    ------>   VCC (sau 5V pentru sensibilitate maximă)
  GND     ------>   GND
  GPIO0   <------   AOUT (ieșire analogică)

ESP32-C6          Buton Pairing
---------         --------------
  GPIO9   ------>   Pin 1
  GND     <------   Pin 2
  (Pull-up intern activat în cod)

ESP32-C6          LED Status (opțional)
---------         ------------------
  GPIO8   ------>   Anod (+) --[220Ω]--
  GND     <------   Catod (-)
```

### Pinout ESP32-C6

| Pin | Funcție | Descriere |
|-----|---------|-----------|
| GPIO0 | ADC1_CH0 | Citire analogică MQ-7 |
| GPIO9 | Buton | Buton pairing (apăsare lungă 3s) |
| GPIO8 | LED | Indicator status (opțional) |

## 🚀 Instalare și Compilare

### 1. Pregătire Mediu ESP-IDF

```bash
# Instalează ESP-IDF (dacă nu e deja instalat)
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.1.2  # sau versiunea mai nouă

# Instalează toolchain pentru ESP32-C6
./install.sh esp32c6

# Activează mediul
. ./export.sh
```

### 2. Clonează sau Creează Proiectul

```bash
# Navighează la directorul proiectului
cd /path/to/esp32c6_zigbee_router
```

### 3. Configurare

```bash
# Setează target-ul
idf.py set-target esp32c6

# Configurare opțională
idf.py menuconfig
```

**Configurări importante în menuconfig:**
- `Component config → Zigbee → Zigbee role` : Router
- `Component config → Zigbee → Max children` : 10 (default)
- `Component config → ADC` : Verifică setările ADC

### 4. Compilare și Flash

```bash
# Compilare
idf.py build

# Flash pe ESP32-C6 (înlocuiește /dev/ttyUSB0 cu portul tău)
idf.py -p /dev/ttyUSB0 flash

# Monitor serial
idf.py -p /dev/ttyUSB0 monitor

# Sau toate deodată
idf.py -p /dev/ttyUSB0 flash monitor
```

## 📱 Utilizare

### Pornire Inițială

La prima pornire, device-ul va:
1. ✓ Inițializa stack-ul Zigbee
2. ✓ Căuta rețele Zigbee disponibile
3. ✓ Se va conecta automat la prima rețea găsită

LED-ul va clipi pentru a indica statusul:
- **3 clipiri rapide**: Pornire cu succes
- **1 clipire lungă**: Conectat la rețea Zigbee
- **10 clipiri rapide**: Eroare de inițializare

### Buton de Pairing

**Apăsare scurtă** (< 3 secunde):
- LED clipește de 2 ori
- Afișează info în serial monitor

**Apăsare lungă** (≥ 3 secunde):
- LED clipește de 5 ori
- Deschide rețeaua pentru pairing timp de 180 secunde
- Alte device-uri se pot conecta la router în acest interval

```
[INFO] Long press detected (3124ms) - Starting pairing mode
[INFO] Network opened for 180 seconds for new devices to join
```

### Monitorizare MQ-7

Senzorul citește nivelul de CO la fiecare 2 secunde:

```
[INFO] MQ-7: ADC=1234, Voltage=1500mV, CO=454ppm, Alarm=NO
[INFO] MQ-7: ADC=2345, Voltage=2850mV, CO=863ppm, Alarm=YES
```

**Alarmă CO**:
- Prag: 800 ppm (configurabil în cod)
- La depășire: LED clipește de 3 ori rapid
- Status trimis via Zigbee

## 🔬 Calibrare MQ-7

⚠️ **IMPORTANT**: Codul folosește o conversie simplificată. Pentru detecție precisă de CO:

### 1. Perioadă de Încălzire
- MQ-7 necesită 24-48 ore de încălzire continuă pentru stabilizare
- Prima pornire: 60 secunde minimum (în cod: 10 secunde pentru test)

### 2. Calibrare în Aer Curat
```c
// Modifică în cod pentru calibrare
// Citește Rs în aer curat (fără CO)
// R0 = Rs / 26.5  (din datasheet la 100ppm CO)
```

### 3. Formula Precisă de Conversie

Conform datasheet MQ-7:
```
Rs/R0 = 26.5 * (ppm/100)^(-0.46)

Unde:
- Rs = Rezistența senzorului în condiții curente
- R0 = Rezistența senzorului în aer curat
- ppm = Concentrație CO în parts per million
```

Implementare în cod:
```c
// Calculare Rs din tensiune
float Vc = 5.0;  // Tensiune circuit (5V sau 3.3V)
float Rl = 10000.0;  // Rezistență load (verifică schema MQ-7)
float Rs = ((Vc * Rl) / voltage) - Rl;

// Calculare ppm
float ratio = Rs / R0;  // R0 trebuie calibrat
float ppm = 100 * pow(ratio / 26.5, 1/-0.46);
```

### 4. Valori Tipice CO

| Concentrație | Efect |
|--------------|-------|
| 0-9 ppm | Nivel normal |
| 10-29 ppm | Expunere prelungită: oboseală |
| 30-70 ppm | Cefalee, greață (2-3 ore) |
| 100-200 ppm | Cefalee severă (2-3 ore) |
| 400+ ppm | Pericol de viață |

**Prag recomandat alarmă**: 50-100 ppm pentru uz casnic

## 📊 Atribute Zigbee Expuse

Device-ul expune următoarele clustere Zigbee:

### Endpoint 10: MQ-7 Sensor

**Basic Cluster (0x0000)**:
- Manufacturer: "Espressif"
- Model: "MQ-7.Router.1"

**Analog Input Cluster (0x000C)** - Nivel CO:
- `present_value` (0x0055): Valoare CO în ppm (uint16)
- `description` (0x001C): "CO Level"

**Binary Input Cluster (0x000F)** - Alarmă:
- `present_value` (0x0055): Status alarmă (bool)

## 🔍 Debugging

### Verificare Conexiune Zigbee

```bash
# În monitor serial
[INFO] ✓ Joined network successfully
[INFO]   Extended PAN ID: xx:xx:xx:xx:xx:xx:xx:xx
[INFO]   PAN ID: 0xXXXX
[INFO]   Channel: 15
[INFO]   Short Address: 0xXXXX
```

### Verificare MQ-7

```bash
# LED-ul clipește de 3 ori → Alarmă CO activă
# Verifică în serial:
[INFO] MQ-7: ADC=2890, Voltage=3100mV, CO=939ppm, Alarm=YES
```

### Probleme Frecvente

**MQ-7 nu detectează CO**:
- Verifică alimentarea (5V recomandată)
- Așteaptă încălzirea completă (24-48h pentru precizie)
- Verifică conexiunea GPIO0 la AOUT

**Butonul nu funcționează**:
- Verifică conexiunea la GPIO9
- Pull-up este activat intern în cod

**Nu se conectează la rețea Zigbee**:
- Verifică că ai un coordinator Zigbee activ
- Channel-ul trebuie să fie același (11-26)
- Resetează device-ul și încearcă din nou

## 🛠️ Personalizare

### Modificare Prag Alarmă CO

```c
// În zigbee_router_mq7.c, linia ~17:
#define MQ7_ALARM_THRESHOLD     800  // Schimbă la 50-100 pentru uz real
```

### Modificare Interval Citire

```c
// În zigbee_router_mq7.c, linia ~16:
#define MQ7_SAMPLE_PERIOD_MS    2000  // Millisecunde
```

### Modificare GPIO-uri

```c
// În zigbee_router_mq7.c, liniile 13-15:
#define PAIRING_BUTTON_GPIO     GPIO_NUM_9
#define MQ7_ANALOG_GPIO         ADC_CHANNEL_0  // GPIO0
#define LED_INDICATOR_GPIO      GPIO_NUM_8
```

## 📚 Referințe

- [ESP32-C6 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)
- [MQ-7 Datasheet](https://www.sparkfun.com/datasheets/Sensors/Biometric/MQ-7.pdf)
- [ESP-Zigbee SDK](https://github.com/espressif/esp-zigbee-sdk)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/)

## 📝 Licență

Acest cod este furnizat ca exemplu educațional. Folosește-l și modifică-l după nevoi!

## ⚠️ Disclaimer

Acest proiect este pentru scopuri educaționale. Pentru detecție critică de CO (siguranță vieții), folosește detectoare certificate și calibrate profesional!
