# Wiring Bulk Capacitors on a Breadboard

This guide explains how to wire bulk capacitors (e.g., 470 µF electrolytic capacitors) on a breadboard to stabilize power rails for your ESP32 and servos.

---

## **Components Needed**
1. **Electrolytic Capacitors** (e.g., 470 µF, 16V or higher).
2. **Breadboard**.
3. **Jumper Wires**.
4. **Power Supply** (e.g., 5V for servos, USB for ESP32).

---

## **Wiring Steps**

### **1. Identify the Capacitor Terminals**
- **Electrolytic Capacitors** are polarized:
  - The **longer leg** is the **positive (+)** terminal.
  - The **shorter leg** is the **negative (-)** terminal.
  - The negative terminal is also marked with a stripe on the capacitor body.

### **2. Place the Capacitor on the Breadboard**
- Insert the **positive (+)** terminal into the breadboard row connected to the **power rail (5V or 3.3V)**.
- Insert the **negative (-)** terminal into the breadboard row connected to the **ground rail (GND)**.

### **3. Connect the Power Rails**
- Use jumper wires to connect the breadboard’s power rail to the **5V** or **3.3V** output of your power supply.
- Use another jumper wire to connect the ground rail to the **GND** of your power supply.

### **4. Add Multiple Capacitors**
- Place additional capacitors along the power and ground rails to stabilize voltage at different points.
- Ensure each capacitor’s positive terminal is connected to the power rail and the negative terminal to the ground rail.

---

## **Example Breadboard Layout**
```plaintext
Breadboard Layout:

+-------------------+  <-- Power Rail (5V or 3.3V)
|                   |
|  +   +   +   +   |  <-- Positive (+) terminals of capacitors
|  |   |   |   |   |
|  -   -   -   -   |  <-- Negative (-) terminals of capacitors
|                   |
+-------------------+  <-- Ground Rail (GND)
```

---

## **Tips**
1. Place capacitors as close as possible to the components they are stabilizing (e.g., servos, ESP32).
2. Use multiple capacitors along the power rail for better stability.
3. Double-check the polarity of electrolytic capacitors before powering on the circuit to avoid damage.

---

Let me know if you need further clarification or a more detailed diagram!