from pathlib import Path
import sys

p = Path(sys.argv[1]) if len(sys.argv)>1 else Path('src/WebManager.cpp')
s = p.read_text(encoding='utf-8')
old = 'ethernetClient=ethernetServer.available();if(ethernetClient)client=&ethernetClient;'
new = 'ethernetClient=ethernetServer.accept();if(ethernetClient)client=&ethernetClient;'
if old not in s:
    raise SystemExit('No se encontró el código original esperado. No se modificó nada.')
s = s.replace(old,new,1)
old2='if((!client->connected()&&!client->available())||millis()-started>(firmwareRequest?30000UL:10000UL)){close();return;}'
new2='''// No retener sockets W5500 durante 10 segundos si el navegador
    // conecta pero no envía cabeceras. Mantener el margen para POST y OTA.
    const unsigned long timeout = firmwareRequest ? 30000UL :
                                  readingBody ? 15000UL :
                                  responding ? 10000UL : 2000UL;
    if((!client->connected()&&!client->available())||millis()-started>timeout){close();return;}'''
if old2 not in s:
    raise SystemExit('No se encontró el bloque de timeout esperado. No se modificó nada.')
s=s.replace(old2,new2,1)
old3='''        close();return;
    }
}

void WebManager::hardwareConfig(){'''
new3='''        close();return;
    }
    // Liberar el socket en este mismo ciclo al terminar la respuesta,
    // sin esperar otra iteración del bucle principal.
    if(responding && offset>=pending.length() && (!file || !file.available())) close();
}

void WebManager::hardwareConfig(){'''
if old3 not in s:
    raise SystemExit('No se encontró el final del bucle esperado. No se modificó nada.')
s=s.replace(old3,new3,1)
backup=p.with_suffix('.cpp.bak')
if not backup.exists(): backup.write_text(p.read_text(encoding='utf-8'),encoding='utf-8')
p.write_text(s,encoding='utf-8')
print(f'Actualizado: {p} (copia: {backup})')
