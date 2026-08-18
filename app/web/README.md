# Web preview host

This directory is a preview of the same C11 game engine used by Flutter. JavaScript
does not implement territory, trails, bots, turrets, camera or rendering.

From the repository root, with Clang/LLD's `wasm32` target available:

```sh
make wasm
node tools/wasm_smoke.mjs
python3 -m http.server 8080 --directory app/web
```

Then open `http://localhost:8080/`. `make bundle` additionally produces the optional
self-contained `ODPAR_Territorial_Domain.html`.

Generated WASM/HTML must always match `ODG_API_VERSION`. They are intentionally rebuilt
by CI rather than treated as engine source.
