# Perfiles de hardware

El perfil activo es `data/config.json`. Se carga al arrancar desde LittleFS; los cambios locales requieren `pio run -t uploadfs` y reinicio. También se puede editar en la página **Hardware** de la web; al guardar, el módulo valida el archivo y se reinicia automáticamente. El guardado escribe y verifica un archivo temporal antes de sustituir `config.json`. Descarga una copia para actualizar el archivo local: `uploadfs` sobrescribe los cambios realizados desde la web.

La red se elige con `network`. Para Ethernet usa `{"type":"ethernet"}` y conserva el bloque `ethernet` con los pines W5500. Para Wi-Fi usa, por ejemplo:

```json
"network": {
  "type": "wifi",
  "ssid": "MiRed",
  "password": "MiClave",
  "hostname": "InterlockEasy"
}
```

Al guardar desde la web el módulo se reinicia automáticamente. Si no logra conectarse al Wi-Fi en 30 segundos, crea el punto de acceso `InterlockEasy-Setup` con contraseña `InterlockEasy`; conéctate y abre `http://192.168.4.1/hardware.html` para corregir el archivo. El SSID y la contraseña quedan almacenados en `config.json` y se muestran en el editor de Hardware.

- `../data/config.json`: Waveshare original, 8 entradas GPIO y 8 relés TCA9554.
- `config-mixed-16di-32ro.json`: 16 entradas PCF8575 y 32 salidas repartidas entre MCP23017, PCF8574 y TCA9554. Es un ejemplo de cableado diferente: no cargar en la Waveshare sin adaptar el hardware.

`inputCount` y `outputCount` deben coincidir con la longitud de `inputs` y `outputs` (0–64 de cada tipo). La posición en esos arrays define DI1…DIn y RO1…ROn. Cada canal tiene `module` (índice desde cero en `modules`) y `pin` (bit del expansor o GPIO del procesador).

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

`i2c` define `sda` y `scl`; frecuencia fija de 100 kHz. `ethernet` admite `type: "w5500"` y los GPIO `sclk`, `miso`, `mosi`, `cs`. Si se omite Ethernet se conservan los pines Waveshare. El bloque `network` selecciona Ethernet, WiFi o ausencia de red en esclavos.

Si falta el archivo o LittleFS, se utiliza el perfil Waveshare por compatibilidad. Un archivo existente ilegible o inválido detiene la inicialización y muestra el error por serie. Un fallo al inicializar un expansor bloquea los mandos de salida hasta reiniciar. Una lectura I²C fallida marca las entradas afectadas como no disponibles; al recuperarse deben superar otra vez el antirrebote. Las escrituras confirmadas de otros módulos se conservan si una escritura posterior falla: una señal repartida entre chips no tiene actualización atómica.

MQTT y señales permanecen en NVS. Al cambiar cantidades se conservan los ajustes por número de canal, se añaden canales deshabilitados y se eliminan de la configuración activa las señales que mencionen relés fuera del nuevo rango. Revisa y guarda desde la web después de cambiar hardware: cambiar el mapa de un canal conserva su función MQTT. El archivo describe hardware, no contiene credenciales MQTT.

La capa lógica ya es independiente de la disposición de E/S. El firmware se compila actualmente para ESP32 Arduino y valida GPIO con el SDK ESP32: cambiar de placa ESP32 requiere seleccionar el `board` adecuado en PlatformIO y ajustar este perfil. Otro tipo de procesador requiere portar las dependencias de plataforma (GPIO, Wire, almacenamiento y red); el JSON no sustituye esa compilación.

Referencias de los controladores: [PCF8574](https://www.ti.com/product/PCF8574), [PCF8575](https://www.ti.com/lit/ds/symlink/pcf8575.pdf), [MCP23017](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23017-Data-Sheet-DS20001952.pdf).

## Maestro y esclavo Modbus RTU (mismo firmware)

- `config-modbus-master-64di-64ro.json`: 8 DI/RO locales y siete placas esclavas
  de 8 DI/RO, direcciones 1 a 7; total 64 entradas y 64 salidas.
- `config-modbus-slave-8di-8ro.json`: ejemplo para cada placa remota. Cambiar
  `modbus.address` por una dirección única. No requiere Ethernet ni WiFi
  (`network.type: "none"`); se puede configurar WiFi para disponer de web/OTA.

Cargar el **mismo firmware** en todas las placas. Copiar el ejemplo apropiado a
`data/config.json`, adaptar hardware/pines y subir LittleFS a cada placa.
Los ejemplos están ajustados a la **ESP32-S3-ETH-8DI-8RO con bornes A/B**:
UART1, TX=GPIO17, RX=GPIO18 y `de: -1`. Su interfaz RS-485 integrada se usa
sin un GPIO de control DE/RE, como en el ejemplo oficial de Waveshare. No hace
falta añadir un transceptor externo ni conectar GPIO21 para controlar la dirección.

La variante **-C** incorpora CAN en lugar de RS-485 y no permite usar Modbus RTU
por sus bornes CAN-H/CAN-L. En otras placas con transceptor externo, adaptar
TX/RX y `de` al cableado; `de: -1` solo corresponde a interfaces que no necesitan
control de dirección por GPIO.

Referencias: [pines de Waveshare](https://www.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO)
y [demo oficial](https://files.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO/ESP32-S3-POE-ETH-8DI-8RO-Demo.zip)
(`WS_GPIO.h` y `WS_RS485.cpp`).

Bus común A/B y referencia GND, con terminación en ambos extremos del bus.
Baudios y paridad iguales en todos los nodos. `parity: "N"` usa 8N2;
`"E"`/`"O"` usan 8E1/8O1. Velocidades admitidas: 9600, 19200, 38400, 57600, 115200.

En el maestro, los módulos `{"type":"modbus","address":1}` representan esclavos.
El `pin` remoto es el índice lógico del esclavo empezando en 0: pin 0 = DI1 o RO1.
Las direcciones de entradas y salidas son independientes. Inversión física y
pullups se configuran en el esclavo; usar `activeLow:false` y `pullup:false`
en los canales remotos del maestro. La inversión lógica DI se configura en el maestro.

El maestro lee entradas discretas (FC02) y escribe grupos de bobinas (FC15).
El esclavo admite además lectura de bobinas (FC01) y escritura individual (FC05).
El maestro controla todas las salidas del esclavo desde RO1 hasta la más alta
mapeada; las intermedias no mapeadas se mantienen apagadas.

`timeoutMs` limita cada espera del maestro. `watchdogMs` apaga las salidas del
esclavo si deja de recibir escrituras válidas; las lecturas no lo reinician.
Configúralo por encima del tiempo de recorrido de todos los nodos, incluyendo
los desconectados. No se ejecutan MQTT, señales ni desvíos autónomos en el esclavo:
la lógica reside en el maestro. Los mandos de prueba web del esclavo se rechazan.

Las dos salidas de un desvío remoto deben pertenecer al mismo esclavo. Los
impulsos se temporizan desde la confirmación y su duración física incluye la
latencia RTU; no son temporizadores de precisión. El sondeo no captura todos los
pulsos rápidos: los cuenta-ejes deben usar entradas locales del maestro.
