#!/usr/bin/env python3
"""Parche no destructivo para las páginas de configuración de Interlock Easy."""
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]).expanduser().resolve() if len(sys.argv) > 1 else Path.cwd()
DATA = ROOT / "data"
PAGES = ["mqtt.html", "inputs.html", "outputs.html", "signals.html",
         "alphanumerics.html", "beacons.html"]
# index.html no usa config.js; detección, desvíos y hardware tienen cargadores propios.

BOOTSTRAP = r"""<script>
/* INTERLOCK_CONFIG_BOOTSTRAP_V1
   Descarga los scripts en orden, reintenta errores de red y muestra fallos. */
(() => {
  const message = document.getElementById('config-message');
  const reload = document.getElementById('load-config');
  const form = document.getElementById('config-form');
  const scripts = ['/resources.js', '/config.js'];
  let loading = false;
  let loaded = false;
  const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

  function errorMessage(text) {
    if (message) message.textContent = text;
    if (form) form.hidden = true;
  }
  async function fetchScript(path) {
    let lastError;
    for (let attempt = 1; attempt <= 4; attempt++) {
      try {
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), 10000);
        let response;
        try {
          response = await fetch(path + '?v=' + Date.now(), {
            cache: 'no-store', signal: controller.signal
          });
        } finally {
          clearTimeout(timer);
        }
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const source = await response.text();
        if (!source.trim()) throw new Error('Respuesta vacía');
        return source;
      } catch (error) {
        lastError = error;
        if (attempt < 4) await wait(600 * attempt);
      }
    }
    throw new Error(path + ': ' + (lastError?.message || 'sin respuesta'));
  }
  function execute(source, name) {
    const node = document.createElement('script');
    node.textContent = source + '\n//# sourceURL=' + name;
    document.body.appendChild(node);
    node.remove();
  }
  async function initialize() {
    if (loading || loaded) return;
    loading = true;
    errorMessage('Cargando scripts de configuración…');
    try {
      // Descarga y ejecución secuenciales: evita ráfagas de sockets W5500.
      const resources = await fetchScript(scripts[0]);
      execute(resources, 'resources.js');
      if (typeof Resources === 'undefined') {
        throw new Error('resources.js no pudo inicializarse');
      }
      const config = await fetchScript(scripts[1]);
      execute(config, 'config.js');
      loaded = true;
      // config.js realiza su propia petición /api/config y pinta los formularios.
    } catch (error) {
      errorMessage('Error de carga: ' + error.message +
                   '. Pulsa «Recargar configuración» para reintentar.');
    } finally {
      loading = false;
    }
  }

  if (reload) {
    // Solo intercepta si el inicializador aún no llegó a ejecutarse.
    reload.addEventListener('click', event => {
      if (loaded) return; // config.js gestiona este botón normalmente.
      event.stopImmediatePropagation();
      initialize();
    }, true);
  }
  initialize();
})();
</script>"""

def patch_page(path):
    original = path.read_text(encoding='utf-8')
    if "INTERLOCK_CONFIG_BOOTSTRAP_V1" in original:
        return False
    # Solo editar páginas que realmente cargan los dos scripts.
    pattern = re.compile(
        r'<script\s+src=["\']/resources\.js["\']\s+defer\s*></script>\s*'
        r'<script\s+src=["\']/config\.js["\']\s+defer\s*></script>',
        re.IGNORECASE
    )
    updated, count = pattern.subn(lambda m: BOOTSTRAP, original, count=1)
    if count != 1:
        print(f'OMITIDO {path.name}: no se encontró el patrón esperado')
        return False
    backup = path.with_suffix(path.suffix + '.bak')
    if not backup.exists():
        backup.write_text(original, encoding='utf-8')
    path.write_text(updated, encoding='utf-8')
    print(f'CORREGIDO {path.name} (copia: {backup.name})')
    return True

if not DATA.is_dir():
    sys.exit(f'No existe {DATA}. Ejecuta el script desde la raíz del proyecto.')
count = sum(patch_page(DATA / name) for name in PAGES if (DATA / name).exists())
print(f'Páginas modificadas: {count}. Ejecuta: pio run -t uploadfs')
