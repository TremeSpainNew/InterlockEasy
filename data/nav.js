(async () => {
  const nav = document.querySelector('nav');
  if (!nav) return;

  const tabs = new Map(
    ['mqtt', 'inputs', 'outputs', 'signals', 'alphanumerics', 'beacons', 'turnouts', 'detection'].map(
      name => [name, nav.querySelector(`a[href="/${name}.html"]`)]
    )
  );

  try {
    const response = await fetch('/api/config', { cache: 'no-store' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const config = await response.json();

    const enabled = (items, predicate = item => item.enabled) =>
      Array.isArray(items) && items.some(predicate);

    const visibility = {
      mqtt: true,
      inputs: enabled(config.inputs),
      outputs: enabled(config.outputs),
      signals: enabled(config.signals, item => item.enabled && !item.numeric),
      alphanumerics: enabled(config.signals, item => item.enabled && !!item.numeric),
      beacons: enabled(config.beacons),
      turnouts: enabled(config.turnouts),
      detection: enabled(config.cvs) || enabled(config.axleCounters)
    };

    for (const [name, link] of tabs) {
      if (link) link.hidden = !visibility[name];
    }
  } catch (error) {
    console.warn('No se pudo aplicar la configuración de navegación.', error);
  }
})();
