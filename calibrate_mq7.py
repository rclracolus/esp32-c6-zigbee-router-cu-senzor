#!/usr/bin/env python3
"""
Script pentru calibrare senzor MQ-7
Calculează R0 bazat pe citiri în aer curat
"""

import serial
import time
import re
import statistics

def calibrate_mq7(port='/dev/ttyUSB0', baudrate=115200, samples=50):
    """
    Calibrează senzorul MQ-7 în aer curat
    
    Args:
        port: Port serial al ESP32-C6
        baudrate: Baud rate (default 115200)
        samples: Număr de eșantioane pentru medie
    """
    print("╔════════════════════════════════════════╗")
    print("║    MQ-7 Calibration Tool               ║")
    print("║    ESP32-C6 Zigbee Router              ║")
    print("╚════════════════════════════════════════╝\n")
    
    print(f"Conectare la {port}...")
    
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        time.sleep(2)
        print("✓ Conectat!\n")
    except Exception as e:
        print(f"✗ Eroare: {e}")
        return
    
    print("INSTRUCȚIUNI:")
    print("1. Plasează senzorul în aer curat (fără CO)")
    print("2. Așteaptă minim 10 minute încălzire")
    print("3. Colectăm {} eșantioane...\n".format(samples))
    
    input("Apasă ENTER când ești gata...")
    
    voltage_readings = []
    adc_readings = []
    
    print(f"\nColectare date ({samples} eșantioane):")
    print("-" * 50)
    
    pattern = re.compile(r'MQ-7: ADC=(\d+), Voltage=(\d+)mV')
    
    while len(voltage_readings) < samples:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            match = pattern.search(line)
            if match:
                adc = int(match.group(1))
                voltage = int(match.group(2))
                
                voltage_readings.append(voltage)
                adc_readings.append(adc)
                
                progress = len(voltage_readings)
                print(f"[{progress:3d}/{samples}] ADC={adc:4d}, V={voltage:4d}mV", end='\r')
                
        except KeyboardInterrupt:
            print("\n\n✗ Întrerupt de utilizator")
            ser.close()
            return
        except Exception as e:
            continue
    
    ser.close()
    
    print("\n" + "-" * 50)
    print("\n✓ Colectare completă!\n")
    
    # Calculare statistici
    avg_voltage = statistics.mean(voltage_readings)
    std_voltage = statistics.stdev(voltage_readings)
    avg_adc = statistics.mean(adc_readings)
    
    print("REZULTATE CALIBRARE:")
    print("=" * 50)
    print(f"Tensiune medie:    {avg_voltage:.2f} mV")
    print(f"Deviație standard: {std_voltage:.2f} mV")
    print(f"ADC mediu:         {avg_adc:.2f}")
    
    # Calculare Rs și R0
    # Presupunem: Vc = 5V, RL = 10kΩ (verifică schema ta!)
    Vc = 5000  # mV
    RL = 10000  # Ohm
    
    print(f"\nParametri circuite (verifică schema!):")
    print(f"Vc (alimentare): {Vc/1000:.1f} V")
    print(f"RL (load):       {RL/1000:.1f} kΩ")
    
    # Rs = [(Vc * RL) / Vout] - RL
    Rs = ((Vc * RL) / avg_voltage) - RL
    
    # Din datasheet MQ-7: Rs/R0 = 26.5 la 100ppm CO în aer curat
    # Deci R0 = Rs / 26.5
    R0 = Rs / 26.5
    
    print(f"\n{'=' * 50}")
    print(f"Rs (în aer curat): {Rs:.2f} Ω ({Rs/1000:.2f} kΩ)")
    print(f"R0 (calibrat):     {R0:.2f} Ω ({R0/1000:.2f} kΩ)")
    print(f"{'=' * 50}\n")
    
    # Generare cod pentru ESP32
    print("ADAUGĂ ÎN CODUL TĂU:")
    print("-" * 50)
    print(f"#define MQ7_R0 {R0:.2f}f  // Ohm (calibrat în aer curat)")
    print(f"#define MQ7_RL {RL:.2f}f  // Ohm (rezistență load)")
    print(f"#define MQ7_VC {Vc:.2f}f  // mV (tensiune circuit)\n")
    
    print("FUNCȚIE DE CONVERSIE:")
    print("-" * 50)
    print("""
float mq7_get_co_ppm(uint32_t voltage_mv) {
    // Calculare Rs
    float Rs = ((MQ7_VC * MQ7_RL) / voltage_mv) - MQ7_RL;
    
    // Calculare ratio
    float ratio = Rs / MQ7_R0;
    
    // Din datasheet: Rs/R0 = 26.5 * (ppm/100)^(-0.46)
    // Rezolvat pentru ppm: ppm = 100 * (ratio / 26.5)^(1/-0.46)
    float ppm = 100.0f * powf(ratio / 26.5f, 1.0f / -0.46f);
    
    return (ppm > 0) ? ppm : 0;
}
""")
    
    # Generare tabel de referință
    print("\nTABEL REFERINȚĂ (bazat pe calibrare):")
    print("-" * 50)
    print("Voltage (mV) | CO (ppm) aproximativ")
    print("-" * 50)
    
    test_voltages = [500, 1000, 1500, 2000, 2500, 3000]
    for v in test_voltages:
        Rs_test = ((Vc * RL) / v) - RL
        ratio_test = Rs_test / R0
        ppm_test = 100.0 * pow(ratio_test / 26.5, 1 / -0.46)
        print(f"{v:4d} mV      | {ppm_test:6.1f} ppm")
    
    print("-" * 50)
    print("\n✓ Calibrare completă!")
    print("\nNOTĂ: Pentru precizie maximă:")
    print("- Lasă senzorul să se încălzească 24-48h")
    print("- Repetă calibrarea după încălzire")
    print("- Testează cu sursă cunoscută de CO\n")

if __name__ == "__main__":
    import sys
    
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
    calibrate_mq7(port=port, samples=50)
