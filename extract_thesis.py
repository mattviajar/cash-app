from pathlib import Path
import re, json
from pypdf import PdfReader

pdf_path = Path(r"E:\Opera\Compiled Thesis.pdf")
out_text = Path(r"e:\atm-learning-system\thesis_extracted_text.txt")
out_json = Path(r"e:\atm-learning-system\thesis_feature_hardware_hits.json")

reader = PdfReader(str(pdf_path))
all_pages = []
for i, p in enumerate(reader.pages, start=1):
    t = p.extract_text() or ""
    all_pages.append((i, t))

with out_text.open("w", encoding="utf-8") as f:
    for page, text in all_pages:
        f.write(f"\n\n===== PAGE {page} =====\n")
        f.write(text)

feature_terms = [
    "feature", "function", "module", "system", "dashboard", "login", "register",
    "account", "deposit", "coin", "transaction", "report", "history", "security",
    "authentication", "admin", "user", "faq", "reset password", "forgot password",
    "api", "status", "serial", "bridge", "iot", "notification", "real-time"
]

hardware_terms = [
    "hardware", "microcontroller", "esp32", "sensor", "coin acceptor", "coin slot",
    "arduino", "raspberry", "usb", "serial", "device", "module", "power supply",
    "breadboard", "jumper", "relay", "lcd", "display", "wifi", "router", "pc", "laptop"
]

combined = sorted(set(feature_terms + hardware_terms), key=len, reverse=True)
pattern = re.compile(r"(?i)\\b(" + "|".join(re.escape(t) for t in combined) + r")\\b")

hits = {"features": [], "hardware": []}
for page, text in all_pages:
    lines = [ln.strip() for ln in re.split(r"[\\r\\n]+", text) if ln.strip()]
    for ln in lines:
        if pattern.search(ln):
            lower_ln = ln.lower()
            kind = "hardware" if any(ht in lower_ln for ht in hardware_terms) else "features"
            hits[kind].append({"page": page, "line": ln})

for kind in list(hits.keys()):
    seen = set()
    dedup = []
    for item in hits[kind]:
        key = (item["page"], re.sub(r"\\s+", " ", item["line"].strip().lower()))
        if key in seen:
            continue
        seen.add(key)
        dedup.append(item)
    hits[kind] = dedup

out_json.write_text(json.dumps(hits, indent=2, ensure_ascii=False), encoding="utf-8")

print(f"Pages: {len(all_pages)}")
print(f"Feature hits: {len(hits['features'])}")
print(f"Hardware hits: {len(hits['hardware'])}")
print(out_text)
print(out_json)
