"""Exercise the actual HTTP loop with a speculative idle connection before a request."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/WebManager.cpp').read_text()
loop=source[source.index('void WebManager::loop()'):source.index('void WebManager::hardwareConfig()')]
stub=r'''
#include <string>
#include <cstdint>
#include <cassert>
#include <algorithm>
unsigned long now=0;
unsigned long millis(){return now;}
struct String:std::string {
 using std::string::string;
 bool endsWith(const char* tail)const {std::string s(tail);return size()>=s.size() && compare(size()-s.size(),s.size(),s)==0;}
};
struct Socket {bool open=true;String input;size_t cursor=0;String output;};
Socket idle,ready;
struct Client {
 Socket* socket=nullptr;
 operator bool()const{return socket;}
 void setConnectionTimeout(int){}
 bool connected(){return socket && socket->open;}
 int available(){return socket?socket->input.size()-socket->cursor:0;}
 int read(){return socket->input[socket->cursor++];}
 int availableForWrite(){return 512;}
 size_t write(const uint8_t* b,size_t n){socket->output.append((const char*)b,n);return n;}
};
struct Server {
 Client accept(){return {&idle};}
 Client available(){return ready.open && ready.cursor<ready.input.size()?Client{&ready}:Client{};}
};
struct File {
 operator bool()const{return false;}
 bool available(){return false;}
 size_t position(){return 0;}
 size_t read(uint8_t*,size_t){return 0;}
 void seek(size_t){}
};
struct WebManager {
 Server server;Client client;File file;
 unsigned long started=0;bool responding=false,readingBody=false,testRequest=false,hardwareRequest=false;
 size_t contentLength=0,offset=0;String request,body,pending;
 int dispatched=0;
 void close(){if(client.socket)client.socket->open=false;client={};responding=false;}
 void dispatch(){++dispatched;responding=true;pending="OK";}
 void testControl(){}void saveHardwareConfig(){}void saveConfig(){}
 void respond(int,const char*,const char*,const char*){}
 void loop();
};
'''
test=r'''
int main(){
 ready.input="GET /api/status HTTP/1.1\r\nHost: esp\r\n\r\n";
 WebManager web;
 web.loop();assert(web.dispatched==1);assert(idle.open);
 web.loop();web.loop();assert(ready.output=="OK" && !ready.open);
 // No request queued: do not occupy the sole worker with the idle socket.
 web.loop();assert(!web.client);
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.cpp').write_text(stub+loop+test)
 subprocess.run(['c++','-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: queued HTTP request is served despite earlier idle connection; response closes and worker remains free')
