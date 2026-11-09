#!/usr/bin/env python3
"""Attach 2 custom characteristics to the standard Improv BLE service.

Runs in the integrated Weatherdisplay project, against local .pio/libdeps. Requires 'pio pkg install' first.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parent
matches = list((root / '.pio' / 'libdeps').rglob('ImprovWiFiBLE.cpp'))
if len(matches) != 1:
    sys.exit('FEL: hittade inte exakt en ImprovWiFiBLE.cpp. Kor pio pkg install forst.')
p = matches[0]
s = p.read_text()
marker = '// WX_CUSTOM_CONFIG_INTEGRATED'
if marker in s:
    print('BLE-tillagget ar redan installerat.')
    sys.exit(0)
needle = '  ch_rpc_cmd_->setCallbacks(this);'
assert s.count(needle) == 1, 'Bibliotekskoden har andrats; stoppar utan att gora nagot'
# Keep Improv intact. Add the weather custom characteristics before service->start().
s = s.replace(needle, needle + '''

  // WX_CUSTOM_CONFIG_INTEGRATED: custom SSID paging + Swedish locality upload.
  extern void wxBLEInitExtra(NimBLEService* service);
  wxBLEInitExtra(service_);
''', 1)
p.write_text(s)
print('OK: egna BLE-egenskaper tillagda i testbiblioteket:',p)
