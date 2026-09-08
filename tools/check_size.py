"""Leave at least 512 bytes of the ATmega32's SRAM for stack/interrupts."""
import subprocess
import sys

rows = subprocess.check_output([sys.argv[1], "-A", sys.argv[2]], text=True).splitlines()
sections = {}
for row in rows:
    fields = row.split()
    if len(fields) >= 2 and fields[0].startswith("."):
        sections[fields[0]] = int(fields[1])
text, data, bss = (sections.get(name, 0) for name in (".text", ".data", ".bss"))
flash = text + data
ram = data + bss
print(f"Flash: {flash}/32768 bytes; static SRAM: {ram}/2048; stack headroom: {2048 - ram}")
if flash > 32768 or ram > 1536:
    sys.exit("ATmega32 memory budget exceeded")
