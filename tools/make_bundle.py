#!/usr/bin/env python3
from pathlib import Path
import base64
root=Path(__file__).resolve().parents[1]
html=(root/'app/web/index.html').read_text(encoding='utf-8')
wasm=(root/'build/odpar_territorial_domain.wasm').read_bytes()
b64=base64.b64encode(wasm).decode('ascii')
needle='const EMBEDDED_WASM_BASE64 = null;'
if needle not in html:
    raise SystemExit('bundle marker missing')
html=html.replace(needle, f"const EMBEDDED_WASM_BASE64 = '{b64}';", 1)
(root/'app/web/ODPAR_Territorial_Domain.html').write_text(html,encoding='utf-8')
print(f'bundle={root / "app/web/ODPAR_Territorial_Domain.html"}')
print(f'wasm_bytes={len(wasm)}')
print(f'bundle_bytes={(root / "app/web/ODPAR_Territorial_Domain.html").stat().st_size}')
