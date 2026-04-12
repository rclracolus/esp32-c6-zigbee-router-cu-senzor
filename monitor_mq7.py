#!/usr/bin/env python3
"""
Simulator pentru testare detector CO fără gaz real
Monitorizează serial și afișează grafic nivelurile de CO
"""

import serial
import time
import re
from datetime import datetime
import sys

try:
    import matplotlib.pyplot as plt
    import matplotlib.animation as animation
    from collections import deque
    HAS_PLOT = True
except ImportError:
    HAS_PLOT = False
    print("⚠️  matplotlib nu e instalat - doar afișare text")
    print("   Pentru grafic: pip install matplotlib")

class MQ7Monitor:
    def __init__(self, port='/dev/ttyUSB0', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        
        # Data storage
        self.max_points = 100
        self.timestamps = deque(maxlen=self.max_points)
        self.co_levels = deque(maxlen=self.max_points)
        self.adc_values = deque(maxlen=self.max_points)
        self.voltages = deque(maxlen=self.max_points)
        
        # Stats
        self.alarm_count = 0
        self.max_co = 0
        self.avg_co = 0
        
    def connect(self):
        """Conectează la ESP32-C6"""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            time.sleep(2)
            print(f"✓ Conectat la {self.port}")
            return True
        except Exception as e:
            print(f"✗ Eroare conectare: {e}")
            return False
    
    def parse_line(self, line):
        """Parse date MQ-7 din serial"""
        # Pattern: MQ-7: ADC=1234, Voltage=1500mV, CO=454ppm, Alarm=NO
        pattern = r'MQ-7: ADC=(\d+), Voltage=(\d+)mV, CO=(\d+)ppm, Alarm=(\w+)'
        match = re.search(pattern, line)
        
        if match:
            adc = int(match.group(1))
            voltage = int(match.group(2))
            co = int(match.group(3))
            alarm = match.group(4) == 'YES'
            
            return {
                'timestamp': datetime.now(),
                'adc': adc,
                'voltage': voltage,
                'co': co,
                'alarm': alarm
            }
        return None
    
    def update_data(self, data):
        """Actualizează datele pentru grafic"""
        self.timestamps.append(data['timestamp'])
        self.co_levels.append(data['co'])
        self.adc_values.append(data['adc'])
        self.voltages.append(data['voltage'])
        
        if data['alarm']:
            self.alarm_count += 1
        
        self.max_co = max(self.max_co, data['co'])
        
        if len(self.co_levels) > 0:
            self.avg_co = sum(self.co_levels) / len(self.co_levels)
    
    def print_stats(self, data):
        """Afișează statistici text"""
        bar_length = 50
        bar_fill = int((data['co'] / 1000) * bar_length)
        bar = '█' * bar_fill + '░' * (bar_length - bar_fill)
        
        alarm_indicator = '🔴 ALARMĂ!' if data['alarm'] else '🟢 OK'
        
        print(f"\r{alarm_indicator} | CO: {data['co']:4d}ppm [{bar}] | "
              f"V: {data['voltage']:4d}mV | ADC: {data['adc']:4d} | "
              f"Max: {self.max_co:4d}ppm | Avg: {self.avg_co:.1f}ppm | "
              f"Alarme: {self.alarm_count:3d}", end='', flush=True)
    
    def monitor_text(self):
        """Monitorizare doar text (fără grafic)"""
        print("\n" + "=" * 80)
        print("📊 MQ-7 CO Monitor - Text Mode")
        print("=" * 80)
        print("Apasă Ctrl+C pentru a opri\n")
        
        try:
            while True:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                data = self.parse_line(line)
                
                if data:
                    self.update_data(data)
                    self.print_stats(data)
                    
        except KeyboardInterrupt:
            print("\n\n✓ Monitorizare oprită")
            self.print_summary()
    
    def print_summary(self):
        """Afișează sumar final"""
        print("\n" + "=" * 80)
        print("📈 SUMAR")
        print("=" * 80)
        print(f"Eșantioane colectate: {len(self.co_levels)}")
        print(f"CO maxim detectat:    {self.max_co} ppm")
        print(f"CO mediu:             {self.avg_co:.2f} ppm")
        print(f"Alarme declanșate:    {self.alarm_count}")
        
        if len(self.co_levels) > 0:
            print(f"\nDistribuție niveluri CO:")
            ranges = [
                (0, 50, "Sigur"),
                (50, 100, "Atenție"),
                (100, 200, "Pericol moderat"),
                (200, 400, "Pericol serios"),
                (400, 1000, "Pericol critic")
            ]
            
            for low, high, label in ranges:
                count = sum(1 for co in self.co_levels if low <= co < high)
                percent = (count / len(self.co_levels)) * 100
                bar = '█' * int(percent / 2)
                print(f"  {low:3d}-{high:3d} ppm ({label:20s}): {bar} {percent:.1f}%")
        
        print("=" * 80 + "\n")
    
    def animate_plot(self, frame):
        """Update grafic matplotlib"""
        line = self.ser.readline().decode('utf-8', errors='ignore').strip()
        data = self.parse_line(line)
        
        if data:
            self.update_data(data)
            self.print_stats(data)
            
            # Update grafic
            if len(self.timestamps) > 1:
                # Convertește timestamps la secunde relative
                start_time = self.timestamps[0]
                times = [(t - start_time).total_seconds() for t in self.timestamps]
                
                # Update linii
                self.line_co.set_data(times, list(self.co_levels))
                self.line_voltage.set_data(times, list(self.voltages))
                
                # Update axe
                self.ax1.set_xlim(times[0], times[-1])
                self.ax1.set_ylim(0, max(max(self.co_levels) * 1.2, 100))
                
                self.ax2.set_xlim(times[0], times[-1])
                self.ax2.set_ylim(0, max(max(self.voltages) * 1.2, 1000))
                
                # Highlight zona de alarmă
                alarm_threshold = 800
                self.ax1.axhline(y=alarm_threshold, color='r', linestyle='--', 
                               linewidth=1, alpha=0.5, label='Prag alarmă')
    
    def monitor_plot(self):
        """Monitorizare cu grafic matplotlib"""
        print("\n" + "=" * 80)
        print("📊 MQ-7 CO Monitor - Graphic Mode")
        print("=" * 80)
        print("Închide fereastra grafică pentru a opri\n")
        
        # Setup figura
        self.fig, (self.ax1, self.ax2) = plt.subplots(2, 1, figsize=(12, 8))
        self.fig.suptitle('ESP32-C6 MQ-7 CO Sensor Monitor', fontsize=16, fontweight='bold')
        
        # Grafic CO
        self.ax1.set_xlabel('Timp (secunde)')
        self.ax1.set_ylabel('CO (ppm)', color='red')
        self.ax1.tick_params(axis='y', labelcolor='red')
        self.ax1.grid(True, alpha=0.3)
        self.line_co, = self.ax1.plot([], [], 'r-', linewidth=2, label='Nivel CO')
        self.ax1.legend(loc='upper left')
        
        # Grafic Voltage
        self.ax2.set_xlabel('Timp (secunde)')
        self.ax2.set_ylabel('Tensiune (mV)', color='blue')
        self.ax2.tick_params(axis='y', labelcolor='blue')
        self.ax2.grid(True, alpha=0.3)
        self.line_voltage, = self.ax2.plot([], [], 'b-', linewidth=2, label='Tensiune ADC')
        self.ax2.legend(loc='upper left')
        
        plt.tight_layout()
        
        # Animație
        ani = animation.FuncAnimation(self.fig, self.animate_plot, 
                                     interval=100, cache_frame_data=False)
        
        try:
            plt.show()
        except KeyboardInterrupt:
            pass
        finally:
            print("\n✓ Monitorizare oprită")
            self.print_summary()

def main():
    if len(sys.argv) > 1:
        port = sys.argv[1]
    else:
        port = '/dev/ttyUSB0'
    
    monitor = MQ7Monitor(port=port)
    
    if not monitor.connect():
        print("\n⚠️  Nu s-a putut conecta la ESP32-C6")
        print("   Verifică:")
        print("   - Portul serial: ls /dev/ttyUSB* sau /dev/ttyACM*")
        print("   - Permisiuni: sudo usermod -aG dialout $USER")
        print("   - Device-ul e pornit și conectat")
        return
    
    print("\nModuri disponibile:")
    print("  [1] Text - Afișare simplă în terminal")
    print("  [2] Grafic - Plot live cu matplotlib")
    
    if not HAS_PLOT:
        print("\n⚠️  matplotlib nu e disponibil - doar mod text")
        mode = '1'
    else:
        mode = input("\nAlege mod (1/2) [1]: ").strip() or '1'
    
    try:
        if mode == '2' and HAS_PLOT:
            monitor.monitor_plot()
        else:
            monitor.monitor_text()
    finally:
        if monitor.ser:
            monitor.ser.close()
            print("\n✓ Conexiune închisă")

if __name__ == "__main__":
    main()
