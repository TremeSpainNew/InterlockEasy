# Interlock Easy

Firmware base para un módulo de entradas/salidas MQTT orientado a enclavamientos,
con hardware configurable y perfil inicial para Waveshare ESP32-S3-ETH-8DI-8RO.

## Hardware configurable

Edita `data/config.json` o abre **Hardware** en la web para definir de 0 a 32 entradas y de 0 a 32 relés,
mezclando GPIO, TCA9554, PCF8574, PCF8575 y MCP23017. El resumen, MQTT,
las señales y los formularios usan los canales configurados.
Consulta [el formato y los ejemplos](examples/README.md) antes de cambiar el mapa.
El cableado descrito a continuación corresponde al perfil Waveshare incluido.

## Filosofía

Interlock Easy es un proyecto independiente de EasySIM.

La placa utiliza Ethernet W5500 como conexión de red principal. Cada entrada y
cada salida puede configurarse individualmente para trabajar con MQTT.

### Entradas DI1..DI8

Cada entrada podrá configurar:

- Habilitada/deshabilitada
- Nombre
- Topic MQTT
- Payload activo
- Payload inactivo
- Inversión lógica
- Retain

Ejemplo:

    Topic: cv/RFP/CV1/estado
    Activa: OCUPADO
    Inactiva: LIBRE

### Salidas RO1..RO8

Cada salida podrá configurar:

- Habilitada/deshabilitada
- Nombre
- Topic de mando
- Payload ON
- Payload OFF
- Publicación de estado
- Topic de estado
- Payload de estado ON/OFF
- Retain

Ejemplo:

    Topic mando: aguja/RFP/A1/mando
    ON: NORMAL
    OFF: DESVIADA

## Ethernet W5500

- INT: GPIO12
- MOSI: GPIO13
- MISO: GPIO14
- SCLK: GPIO15
- CS: GPIO16

## Entradas

- DI1: GPIO4
- DI2: GPIO5
- DI3: GPIO6
- DI4: GPIO7
- DI5: GPIO8
- DI6: GPIO9
- DI7: GPIO10
- DI8: GPIO11

## Relés

RO1..RO8 utilizan EXIO1..EXIO8 en esta placa.

Control mediante TCA9554 en dirección I²C `0x20`, SDA GPIO42 y SCL GPIO41,
a 100 kHz. RO1..RO8 corresponden a los bits 0..7, activos a nivel alto.
Se carga OFF antes de configurar los ocho pines como salidas.

Si falla la inicialización, los mandos quedan bloqueados hasta reiniciar.
Si falla una escritura I²C, no se actualiza el estado interno ni se publica
el cambio por MQTT; un mando posterior puede reintentar la escritura.
El estado representa el último mando aceptado por I²C, no una medida de los
contactos físicos del relé.

Referencias: [Waveshare](https://docs.waveshare.net/ESP32-S3-ETH-8DI-8RO/),
[configuración de la placa en ESPHome](https://devices.esphome.io/devices/waveshare-esp32-s3-eth-8di-8ro/)
y [registros TCA9554](https://www.ti.com/lit/ds/symlink/tca9554.pdf).

## Verificación

- Compilar: `pio run` (incluye Ethernet 2.0.2).
- Pruebas de relés con I²C simulado: `python3 tests/test_relays.py` (requiere `c++`).
- Pruebas del dashboard con DOM y API simulados: `node tests/test_dashboard.cjs`.
- Pruebas de configuración/NVS simulada: `python3 tests/test_config.py` (requiere las dependencias instaladas con `pio run`).
- Pruebas del editor web: `node tests/test_config_ui.cjs`.
- Pendiente en hardware: comprobar apagado al arrancar y accionar cada canal
  individualmente, verificando que los demás conservan su estado.

## Configuración persistente

Preferences/NVS:

    interlock

Client ID MQTT inicial:

    InterlockEasy-IO

## Web y API de estado

El servidor HTTP escucha en el puerto 80 de la IP Ethernet asignada por DHCP.
Sirve desde LittleFS el resumen (`/` y `/index.html`), las páginas de
configuración y los recursos compartidos CSS/JS. El resumen consulta `GET /api/status`
cada segundo, después de completar la petición anterior, y marca «Sin datos»
si pierde la conexión. Muestra las entradas y salidas configuradas, la IP,
la conexión MQTT y la disponibilidad del controlador de relés.

La API devuelve `uptimeMs`, `ethernet` (`connected`, `ip`), `mqtt`
(`connected`, `configured`), `filesystemReady`, `outputsReady`, `inputs`
y `outputs`. Cada canal incluye `channel` (1..N, hasta 32), `name`, `enabled`
(habilitación MQTT) y `state`. Las salidas tienen `state: null` cuando
el controlador no se ha inicializado. No se exponen credenciales.

Para instalar firmware y web en la placa:

```sh
pio run -t upload
pio run -t uploadfs
pio device monitor
```

Abre `http://<IP mostrada por serie>/` desde la misma red. Para generar solo
la imagen web, sin cargarla: `pio run -t buildfs`. LittleFS se monta sin
formatear automáticamente; si falta la página, HTTP devuelve 503 con las
instrucciones de carga y la API de estado sigue disponible.

El servidor atiende una conexión cada vez, limita las cabeceras a 2048 bytes
y cada conexión a 10 segundos. Admite GET para consulta y POST en
`/api/config` para guardar. Otras rutas devuelven 404 y otros métodos 405.
Los mandos manuales desde la web quedan pendientes. El arranque aún espera a DHCP y los intentos de conexión
MQTT pueden retrasar las actualizaciones HTTP.

Pendiente en hardware: abrir la web, comprobar `/api/status`, cambiar entradas
y verificar pérdida y recuperación de comunicación. Las pruebas locales
no sustituyen la validación del W5500 y los relés en la placa.

## Configuración desde el navegador

La navegación se divide en cinco páginas:

- `/index.html`: resumen de conexiones, entradas, salidas y señales.
- `/mqtt.html`: broker y credenciales.
- `/inputs.html`: configuración de las entradas, JSON, inversión y antirrebote.
- `/outputs.html`: configuración de salidas independientes.
- `/signals.html`: focos, relés, aspectos y parpadeo.

Cada página de configuración carga sus ajustes automáticamente y permite
**Recargar configuración**. Al guardar se consulta la configuración actual y se
sustituye solo la sección editada, conservando las otras secciones. Guardar señales
también deshabilita los mandos individuales de sus relés asignados. Dos clientes
que editen simultáneamente la misma sección siguen sujetos al último guardado.

Los recursos `style.css`, `dashboard.js` y `config.js` se sirven localmente, sin
CDN. Las rutas están enumeradas explícitamente en el servidor; para instalar esta
separación hay que actualizar **firmware y LittleFS**. Prueba de las páginas:
`node tests/test_pages.cjs`.

Edita el broker o despliega los canales en la página correspondiente.
**Guardar y aplicar** valida y guarda en NVS una instantánea completa antes de
cambiar la configuración activa. Los relés conservan su último estado; deshabilitar
un canal deshabilita su uso por MQTT, no fuerza su salida a OFF.

- Broker: host (vacío desactiva MQTT), puerto 1..65535, Client ID, usuario,
  contraseña y keep alive 5..3600 s.
- Entradas: habilitación MQTT, nombre, topic, payloads ON/OFF, inversión y retain.
- Salidas: habilitación MQTT, nombre, topic y payloads de mando, publicación
  de estado, topic/payloads de estado y retain.
- Límites en bytes: nombre y Client ID 64; host, usuario, contraseña y
  topics 128; payloads 256. En payloads se permiten saltos de línea y tabuladores.
  Se rechazan los demás caracteres de control, comodines en topics,
  payloads ON/OFF iguales y topics publicados que coincidan con mandos habilitados.
- La contraseña no se devuelve en `GET /api/config`: `mqtt.passwordSet` indica
  si existe. Al guardar, omitir `mqtt.password` conserva la actual y enviar
  `"password": ""` la borra. En el formulario, dejar el campo vacío la conserva;
  la casilla de borrado permite eliminarla explícitamente.

`POST /api/config` requiere `Content-Type: application/json`, `Content-Length`
y `X-Interlock: 1`. El cuerpo (máximo 57344 bytes) tiene los mismos campos que
GET, con tantos elementos en `inputs` y `outputs` como canales del perfil de hardware. El guardado responde
`{"saved":true}`; los errores de validación o persistencia se muestran en la web.
No hay autenticación HTTP ni TLS: el editor está disponible para los clientes
que puedan acceder a la red del módulo. La cabecera adicional bloquea formularios
web de otros orígenes, pero no es una contraseña de acceso.

Tras guardar, MQTT desconecta la sesión anterior y reintenta a los 5 segundos,
renueva las suscripciones y publica todos los estados al conectar. La inversión
se aplica también al estado de entradas. Los estados de salida solo se publican
si el controlador está inicializado. Los mensajes retenidos en topics antiguos
no se borran automáticamente al cambiar la configuración.

La instantánea se guarda en la clave NVS `config_v1`; se sigue leyendo la
configuración antigua por claves individuales cuando no existe una instantánea.

## Payloads JSON

Cada entrada y cada mando de salida permite activar **Payloads en formato JSON**.
La publicación de estado de los relés tiene su propia casilla **Publicar estado en JSON**.
Las configuraciones anteriores siguen en modo texto por defecto.

Para publicar, introduce documentos completos (máximo 256 bytes por payload):

- ON: `{"estado":true,"origen":"DI1"}`
- OFF: `{"estado":false,"origen":"DI1"}`

Se envían tal como se han escrito, sin envolverlos en otra cadena JSON.
Se validan al guardar y se rechazan ON/OFF equivalentes aunque cambie el orden
de las claves. Los campos JSON admiten objetos, arrays y valores escalares.

Para recibir `{"datos":{"estado":true},"timestamp":123}` en un relé:

- Activa **Payloads en formato JSON**.
- Campo JSON recibido: `datos.estado`.
- Payload ON: `true`; payload OFF: `false`.

Si el valor es texto, usa comillas JSON: `"ON"` y `"OFF"`. El booleano `true`,
el número `1` y el texto `"1"` se distinguen. Una ruta vacía compara todo el
mensaje, sin depender del orden de claves ni de los espacios. Con ruta se
comparan únicamente los valores seleccionados; los campos adicionales fuera
de esa ruta no influyen. Las rutas seleccionan claves de objetos separadas por
puntos, no índices de arrays ni claves que contengan puntos.

Los mensajes inválidos, campos ausentes o valores no coincidentes no accionan
el relé. El buffer MQTT es de 2048 bytes; el cuerpo recibido se limita a 1792
bytes y el parser también limita la complejidad/profundidad del documento.
La API añade `payloadJson` a ambos tipos de canal y `jsonPath`/`stateJson` a
salidas. Los payloads siguen siendo cadenas dentro del JSON de configuración.

Prueba local: `python3 tests/test_json_payload.py`.

Los GPIO de DI1–DI8 se inicializan con `INPUT_PULLUP` para mantener un nivel
alto de reposo. Referencia: [configuración ESPHome de esta placa](https://devices.esphome.io/devices/waveshare-esp32-s3-eth-8di-8ro/).

Las entradas están invertidas por defecto: nivel GPIO bajo = activa y alto =
inactiva. Se puede cambiar **Invertir entrada** por canal. Los valores de inversión
ya guardados en NVS se conservan al actualizar el firmware.

## Pruebas desde el resumen

En `/index.html`, cada relé independiente tiene botones ON/OFF que actúan sobre
el hardware, incluso si MQTT está deshabilitado. Los relés reservados por una
señal no admiten mandos individuales; la tarjeta de señal ofrece sus aspectos.
Las escrituras I²C fallidas se muestran como error y no confirman un estado nuevo.
Los mandos MQTT siguen activos y pueden sustituir una prueba de relé o señal.

Cada entrada tiene **Simular activa**, **Simular inactiva** y **Lectura física**.
La simulación sustituye el estado lógico durante 60 segundos, sin modificar GPIO,
inversión ni NVS. Se identifica como **SIMULADA** y publica por MQTT si el canal
está habilitado. La lectura física y el antirrebote continúan en segundo plano.
Al cancelar o expirar se publica el estado físico filtrado. Reiniciar cancela las
simulaciones; cerrar el navegador no las cancela inmediatamente (expiran en 60 s).

API `POST /api/test`, con JSON y `X-Interlock: 1`:

- `{"type":"relay","channel":1,"state":true}`: activar RO1.
- `{"type":"input","channel":1,"state":false}`: simular DI1 inactiva.
- `{"type":"input","channel":1,"state":null}`: restaurar DI1 física.
- `{"type":"signal","channel":1,"aspect":"VíaLibre"}`: probar el aspecto de la primera señal.

Los canales de entrada y relé son 1..N (hasta 32); los índices de señal son 1..8. Una señal conserva el aspecto de prueba hasta otro mando,
reinicio o cambio de configuración; los relés no tienen apagado temporizado.
La respuesta `{"applied":true}` confirma aplicación, no realimentación física.

## Antirrebote de entradas

Cada DI tiene **Antirrebote (ms)** en el editor web: 50 ms por defecto,
configurable entre 0 y 5000 ms; 0 desactiva el filtro. Se guarda en NVS mediante
`inputs[].debounceMs`. Las configuraciones antiguas usan 50 ms.

Se muestrean las entradas cada 5 ms cuando el bucle puede ejecutarse. Solo se
acepta un nuevo estado tras observarlo estable durante el intervalo configurado,
tanto al activar como al desactivar. Cada rebote reinicia la espera, de forma
independiente por canal y sin `delay()`. MQTT y el dashboard usan el estado
filtrado, también al reconectar. La inversión lógica se aplica después del filtro.

La primera lectura al arrancar establece el estado inicial sin espera; los
cambios posteriores se filtran. Pulsos inferiores al intervalo pueden descartarse.
Los tiempos dependen del muestreo y pueden aumentar si una operación de red
bloquea el bucle; no se detectan transiciones entre muestras. Si pasan más de
20 ms entre lecturas, se reinicia el tiempo de estabilidad del candidato: ese
periodo sin muestras no cuenta como antirrebote confirmado.

El escaneo registra cambios sin escribir en sockets. MQTT atiende una cola de
último estado por entrada, un canal por iteración, con reintento de los envíos
fallidos una vez por segundo y sin impedir que se prueben otros canales. Al
reconectar se programan los estados actuales de todas las entradas habilitadas.
La cola no es un historial de pulsos: varios cambios durante una desconexión se
consolidan en el estado más reciente. QoS 0 confirma aceptación por el cliente
MQTT, no recepción por el broker.

El resumen muestra el último nivel físico muestreado (`raw`), el estado de
filtrado (`filtering`), la inversión, el intervalo y si hay publicación pendiente
(`publishPending`). Las operaciones de red siguen siendo cooperativas y pueden
bloquear; esta mejora no convierte el muestreo en captura por interrupciones.
Prueba de publicaciones pendientes: `python3 tests/test_input_queue.py`.

Prueba: `python3 tests/test_debounce.py` (rebotes en ambos sentidos, intervalos
independientes, inversión, reconexión, filtro desactivado y desbordamiento del reloj).

## Señales de varios focos

Una señal agrupa de 1 a 8 focos, cada uno asignado a un RO físico diferente.
En **Configuración → Señales de varios focos → Añadir señal**, define nombre,
topic, campo JSON (`Aspecto` por defecto), focos y aspectos. Se permiten hasta
8 señales y 12 aspectos por señal, dentro de los relés disponibles en el perfil de hardware (hasta 32).

Cada aspecto define para cada foco **Apagado**, **Fijo** o **Parpadeo**. El valor
se escribe como texto sin comillas, respetando acentos y mayúsculas. Por ejemplo,
una señal de 4 focos podría asignar rojo/amarillo/verde/blanco a RO1/RO2/RO3/RO4:
`Parada` enciende solo el rojo y `VíaLibre` solo el verde. Son ejemplos; la tabla
real se configura según el cableado y los aspectos de la instalación.

El mensaje `{"Aspecto":"VíaLibre","AspectoAnterior":"Parada"}` selecciona
el aspecto por `Aspecto`, ignorando `AspectoAnterior`. Las rutas anidadas también
funcionan. Los aspectos desconocidos y los mensajes inválidos mantienen el último
estado. Los mensajes repetidos no reinician la fase de parpadeo.

Los relés de señales habilitadas son exclusivos: no pueden pertenecer a otra
señal habilitada ni aceptar mandos individuales. El formulario deshabilita MQTT
individual para esos RO al guardar; la API rechaza conflictos. Los demás relés
siguen disponibles para otras señales o salidas independientes. El dashboard
muestra la señal, su aspecto y sus focos, e identifica los RO asignados.

Cada cambio de grupo usa una escritura al registro del TCA9554, conservando los
bits de las otras señales. Esto no garantiza simultaneidad mecánica de contactos.
El estado se actualiza solo si la escritura I²C es aceptada. Durante la ejecución
se reintentan escrituras fallidas en los siguientes ciclos.

El intervalo de parpadeo configura cada fase ON/OFF (250..10000 ms; por defecto
500 ms ON y 500 ms OFF). Todos los focos intermitentes de una señal comparten fase.
Es temporización cooperativa: operaciones de red bloqueantes pueden retrasarla.
La vista web consulta cada segundo y no reproduce necesariamente cada destello.

Al arrancar los relés están apagados hasta recibir un aspecto. Una desconexión
MQTT conserva el aspecto, incluido el parpadeo mientras el bucle puede ejecutarse.
Guardar configuración detiene las animaciones y conserva los últimos estados de
relés hasta recibir otro aspecto; no se restaura automáticamente el mando individual
al eliminar una señal. El aspecto mostrado es el último mando aceptado, no una
confirmación óptica o de los contactos físicos.

La API añade `signals`, una lista opcional en configuraciones antiguas. Cada señal
incluye `enabled`, `name`, `topic`, `jsonPath`, `blinkMs`, `lights` (`name`, `relay`
1..N, hasta 32) y `aspects` (`value`, `on`, `blink`). `on` y `blink` contienen números de foco
locales 1..N, no números físicos RO. No pueden solaparse. Para eliminar todas las
señales se envía `signals: []`; omitir el campo si ya hay señales se rechaza.

Pruebas: `python3 tests/test_signals.py`, `python3 tests/test_config.py` y
`node tests/test_config_ui.cjs`. Pendiente comprobar focos y cadencias en la placa.

## Próximos componentes

- Mandos manuales desde la web
- DHCP / IP estática
- Prueba de conexión MQTT
- Importación/exportación de configuración
