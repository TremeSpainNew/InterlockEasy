# Perfiles de hardware

El perfil activo es `data/config.json`. Se carga al arrancar desde LittleFS; los cambios requieren `pio run -t uploadfs` y reinicio. También se puede editar en la página **Hardware** de la web. Guarda el JSON validado sin modificar el hardware activo; reinicia manualmente para aplicarlo. El guardado escribe y verifica un archivo temporal antes de sustituir `config.json`. Descarga una copia para actualizar el archivo local: `uploadfs` sobrescribe los cambios realizados desde la web.

- `../data/config.json`: Waveshare original, 8 entradas GPIO y 8 relés TCA9554.
- `config-mixed-16di-32ro.json`: 16 entradas PCF8575 y 32 salidas repartidas entre MCP23017, PCF8574 y TCA9554. Es un ejemplo de cableado diferente: no cargar en la Waveshare sin adaptar el hardware.

`inputCount` y `outputCount` deben coincidir con la longitud de `inputs` y `outputs` (0–32 de cada tipo). La posición en esos arrays define DI1…DIn y RO1…ROn. Cada canal tiene `module` (índice desde cero en `modules`) y `pin` (bit del expansor o GPIO del procesador).

| Tipo | Pines | Dirección I²C decimal |
|---|---|---|
| `gpio` | GPIO válido del procesador | No lleva dirección |
| `tca9554` | 0–7 | 32–39 (0x20–0x27) |
| `pcf8574` | 0–7 | 32–39 |
| `pcf8575` | 0–15; 8 es P10 | 32–39 |
| `mcp23017` | 0–7 = GPA; 8–15 = GPB | 32–39 |

Se admiten hasta 16 descriptores de módulo, con direcciones I²C únicas. PCF8574A y TCA9554A usan otra dirección y no están incluidos en estos tipos. No se pueden compartir pines entre entradas y salidas ni ocupar los GPIO de I²C o Ethernet. La validación comprueba GPIO del chip, pero el usuario debe evitar pines reservados en su placa para flash, PSRAM, USB o arranque.

Las salidas requieren `activeLow`: `true` activa el relé con nivel bajo; `false`, con alto. Al iniciar se escribe el estado OFF configurado. En PCF857x los pines arrancan altos por diseño: comprobar la polaridad de la placa de relés para evitar activación durante el arranque. Los expansores necesitan una etapa de potencia para las bobinas.

Las entradas requieren `pullup`. GPIO y MCP23017 permiten activarlo o desactivarlo; TCA9554 exige `false` y resistencias externas cuando proceda. PCF8574/75 exige `true`: sus entradas se liberan escribiendo uno y tienen polarización débil propia, sin registro de dirección. Para MCP23017 se rechazan entradas en los bits 7 y 15 (GPA7/GPB7), indicados como solo salida en la ficha DS20001952D. La inversión lógica y el antirrebote siguen configurándose por canal desde la web.

`i2c` define `sda` y `scl`; frecuencia fija de 100 kHz. `ethernet` admite `type: "w5500"` y los GPIO `sclk`, `miso`, `mosi`, `cs`. Si se omite Ethernet se conservan los pines Waveshare. La red sigue siendo W5500; no selecciona Wi-Fi ni otros controladores.

Si falta el archivo o LittleFS, se utiliza el perfil Waveshare por compatibilidad. Un archivo existente ilegible o inválido detiene la inicialización y muestra el error por serie. Un fallo al inicializar un expansor bloquea los mandos de salida hasta reiniciar. Una lectura I²C fallida marca las entradas afectadas como no disponibles; al recuperarse deben superar otra vez el antirrebote. Las escrituras confirmadas de otros módulos se conservan si una escritura posterior falla: una señal repartida entre chips no tiene actualización atómica.

MQTT y señales permanecen en NVS. Al cambiar cantidades se conservan los ajustes por número de canal, se añaden canales deshabilitados y se eliminan de la configuración activa las señales que mencionen relés fuera del nuevo rango. Revisa y guarda desde la web después de cambiar hardware: cambiar el mapa de un canal conserva su función MQTT. El archivo describe hardware, no contiene credenciales MQTT.

La capa lógica ya es independiente de la disposición de E/S. El firmware se compila actualmente para ESP32 Arduino y valida GPIO con el SDK ESP32: cambiar de placa ESP32 requiere seleccionar el `board` adecuado en PlatformIO y ajustar este perfil. Otro tipo de procesador requiere portar las dependencias de plataforma (GPIO, Wire, almacenamiento y red); el JSON no sustituye esa compilación.

Referencias de los controladores: [PCF8574](https://www.ti.com/product/PCF8574), [PCF8575](https://www.ti.com/lit/ds/symlink/pcf8575.pdf), [MCP23017](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-Data-Sheet-DS20001952.pdf).
