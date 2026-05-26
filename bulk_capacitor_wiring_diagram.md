# Visual Guide: Wiring Bulk Capacitors on a Breadboard

This guide provides a visual representation of how to wire bulk capacitors (e.g., 470 µF electrolytic capacitors) on a breadboard to stabilize power rails for your ESP32 and servos.

---

## **Diagram Description**
The diagram below shows how to connect bulk capacitors to the power and ground rails of a breadboard. Each capacitor is placed across the power rail (5V or 3.3V) and the ground rail (GND).

---

## **Wiring Diagram**
```mermaid
graph TD
    subgraph Breadboard
        Power_Rail[Power Rail (5V or 3.3V)]
        Ground_Rail[Ground Rail (GND)]
        Capacitor1[470 µF Capacitor 1]
        Capacitor2[470 µF Capacitor 2]
        Capacitor3[470 µF Capacitor 3]
    end

    Power_Rail -->|Positive (+) Terminal| Capacitor1
    Ground_Rail -->|Negative (-) Terminal| Capacitor1

    Power_Rail -->|Positive (+) Terminal| Capacitor2
    Ground_Rail -->|Negative (-) Terminal| Capacitor2

    Power_Rail -->|Positive (+) Terminal| Capacitor3
    Ground_Rail -->|Negative (-) Terminal| Capacitor3
```

---

## **Steps to Connect Capacitors**
1. **Identify the Terminals:**
   - The **longer leg** of the capacitor is the **positive (+)** terminal.
   - The **shorter leg** is the **negative (-)** terminal.

2. **Insert the Capacitor:**
   - Place the **positive (+)** terminal into the breadboard row connected to the **power rail (5V or 3.3V)**.
   - Place the **negative (-)** terminal into the breadboard row connected to the **ground rail (GND)**.

3. **Repeat for Additional Capacitors:**
   - Add more capacitors along the power and ground rails as needed.

---

## **Tips**
1. Place capacitors as close as possible to the components they are stabilizing (e.g., servos, ESP32).
2. Use multiple capacitors along the power rail for better stability.
3. Double-check the polarity of electrolytic capacitors before powering on the circuit to avoid damage.

---

Let me know if you need further clarification or additional visuals!