```markdown
# MCU-A: The Commander 🕹️
This unit handles the physical movement and low-level safety of the MK-12.

### Responsibilities:
- **Motor Control:** Manages the L298N driver for locomotion.
- **Sonar Safety:** Monitors the HC-SR04 ultrasonic sensor.
- **Hardware Interrupts:** Executes the "Hard Stop" protocol if an object is detected within 20cm.

### Communication:
Receives high-level instructions (Auto/Manual) from MCU-B via Serial2.
```
