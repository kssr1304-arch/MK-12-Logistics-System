```markdown
# MK-12 Logistics System
**Autonomous Delivery Framework with Real-Time Cloud Synchronization**

## Project Overview
The MK-12 is an IoT-integrated logistics platform designed for precision payload delivery. Utilizing a dual-microcontroller architecture, the system separates high-level telemetry and IoT logic from low-level motor control to ensure maximum reliability and safety in industrial environments.

## 🌐 Live Control & Monitoring
*To demonstrate the system's real-time capabilities, you can access the live environment here:*
- **[Live Logistics Dashboard](https://kssr1304-arch.github.io/MK-12-Logistics-System/)**
- **System Status:** Synchronized with Google Cloud via Discord Bridge.

## Key Technical Features
- **Dual-MCU Architecture:** MCU-A (Commander) manages motor drivers and sonar-based safety interrupts; MCU-B (Brain) handles WiFi connectivity and NTP time synchronization.
- **Automated Data Pipeline:** Robot telemetry is converted to structured JSON and transmitted via Discord webhooks.
- **Cloud Archiving:** A Python-based bridge performs real-time ingestion of logs into a Google Sheets database.
- **Safety Protocol:** Hardware-level collision avoidance with active ultrasonic feedback loops.

## System Architecture
1. **Edge Device (ESP32):** Captures events and generates JSON-formatted telemetry.
2. **Communication Layer:** Data is tunneled through Discord for immediate visibility.
3. **Database Bridge:** A Python archiver script parses the stream and updates the Google Logistics Database.

## Repository Structure
- `/firmware`: Source code for the dual-microcontroller system.
- `/bridge-archiver`: Python-based middleware for database synchronization.
- `/archive`: Evolutionary prototypes demonstrating the project's development history.
- `index.html`: Source code for the live logistics monitoring dashboard.

## Setup and Installation
1. Flash the `/firmware` to the respective ESP32-S3 boards.
2. Install dependencies: `pip install -r bridge-archiver/requirements.txt`.
3. Configure `credentials.json` and the Discord API token in the bridge script.
4. Execute `python MK12_Live_Archiver.py` to activate the cloud link.

---
*Developed for the IDT Showcase 2026.*
```
