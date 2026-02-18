```markdown
# MK-12 Logistics System 🤖📦
**Autonomous Delivery Robot with Real-Time Discord-to-Cloud Archiving**

## * Overview
The MK-12 is an advanced IoT-enabled logistics robot designed for autonomous and manual payload delivery. It features a unique **Dual-MCU architecture** and a **Live Archiving Bridge** that pipes robot activity through Discord into a Google Sheets database for real-time inventory management.

## * Key Features
- **Dual-MCU Control:** MCU-A (Commander) handles motor control and sonar safety; MCU-B (Brain) handles WiFi, Discord Webhooks, and RFID logic.
- **Live Cloud Archiving:** Every action is logged as JSON to Discord and automatically archived to Google Sheets via a Python-based bridge.
- **Safety Protocol:** Ultrasonic-based collision avoidance (Hardware-level interrupt).
- **Hybrid Control:** Switchable between Autonomous line-following/RFID tasks and Manual joystick control.

## * System Architecture
1. **Robot (ESP32):** Detects event -> Sends JSON Webhook to Discord.
2. **Bridge (Python):** Listens to Discord channel -> Parses JSON -> Updates Google Sheets.
3. **User (Phone/PC):** Monitors live logistics data via Google Sheets app.

## * Project Structure
- `/firmware`: Source code for MCU-A (Motor Control) and MCU-B (IoT Logic).
- `/bridge-archiver`: Python script used to bridge Discord and Google Sheets.
- `/archive`: Historical evolutionary prototypes developed during the project.

## * Setup
1. Upload `/firmware` to respective ESP32 boards.
2. Install Python dependencies: `pip install -r bridge-archiver/requirements.txt`.
3. Add your `credentials.json` and `DISCORD_TOKEN` to the archiver script.
4. Run `python MK12_Live_Archiver.py`.

---
*Developed for the IDT Showcase 2026.*
```
