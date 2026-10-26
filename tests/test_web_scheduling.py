"""Exercise the actual HTTP loop with several queued clients."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/WebManager.cpp").read_text()
loop = source[source.index("void WebManager::swapHttpSlot"):
              source.index("void WebManager::hardwareConfig()")]
stub = r'''
#include <string>
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <deque>
unsigned long now=0;
unsigned long millis(){return now;}
struct String:std::string {
 using std::string::string;
 bool endsWith(const char* tail)const {std::string s(tail);return size()>=s.size() && compare(size()-s.size(),s.size(),s)==0;}
};
struct Socket {bool open=true;String input;size_t cursor=0;String output;unsigned dispatches=0;unsigned id=0;unsigned stopDelay=0;};
std::deque<Socket*> incoming;
bool wifiMode=false;
struct Client {
 Socket* socket=nullptr;
 uint16_t timeout=1000;
 operator bool()const{return socket;}
 bool connected(){return socket && socket->open;}
 int available(){return socket?socket->input.size()-socket->cursor:0;}
 int read(){return socket->input[socket->cursor++];}
 int availableForWrite(){return wifiMode?0:512;}
 size_t write(const uint8_t* b,size_t n){if(wifiMode)n=std::min(n,size_t(1));socket->output.append((const char*)b,n);return n;}
 void setConnectionTimeout(uint16_t value){timeout=value;}
 void stop(){if(socket){socket->stopDelay+=timeout;socket->open=false;}}
};
using EthernetClient=Client;
using WiFiClient=Client;
struct Server {Client accept(){if(incoming.empty())return {};auto* s=incoming.front();incoming.pop_front();return Client{s};}};
struct File {
 operator bool()const{return false;}
 bool available(){return false;}
 size_t position(){return 0;}
 size_t read(uint8_t*,size_t){return 0;}
 void seek(size_t){}
 void close(){}
};
struct ConnectivityStub {bool wifi(){return wifiMode;}} Connectivity;
struct SerialStub {void println(const char*){}} Serial;
struct EspStub {void restart(){}} ESP;
struct WebManager {
 static constexpr uint8_t HTTP_SLOTS=4;
 struct HttpSlot {
  EthernetClient ethernetClient;WiFiClient wifiClient;File file;bool occupied=false;
  bool responding=false;unsigned long started=0;String request,pending;size_t offset=0;
  bool readingBody=false;size_t contentLength=0;String body;bool testRequest=false;
  bool hardwareRequest=false,firmwareRequest=false,filesystemRequest=false;
  size_t firmwareReceived=0;
 };
 HttpSlot httpSlots[HTTP_SLOTS];uint8_t nextHttpSlot=0;
 Server ethernetServer,wifiServer;EthernetClient ethernetClient;WiFiClient wifiClient;
 Client* client=nullptr;File file;bool responding=false;unsigned long started=0,restartAt=0;
 String request,pending,body;size_t offset=0,contentLength=0;
 bool readingBody=false,testRequest=false,hardwareRequest=false,firmwareRequest=false;
 bool filesystemRequest=false,restartPending=false;size_t firmwareReceived=0;
 void swapHttpSlot(HttpSlot&);void processHttpClient();void loop();
 void close(){if(client)client->stop();client=nullptr;responding=false;request.clear();pending.clear();offset=0;readingBody=false;contentLength=0;body.clear();testRequest=false;hardwareRequest=false;firmwareRequest=false;filesystemRequest=false;firmwareReceived=0;}
 void dispatch(){++client->socket->dispatches;responding=true;pending="OK";}
 void testControl(){}void saveHardwareConfig(){}void saveConfig(){}
 void respond(int,const char*,const char*,const String&){}
 void receiveFirmware(){}
};
'''
test = r'''
static void queue(Socket& socket){
 static unsigned id=0;socket.id=++id;
 socket.input="GET /api/status HTTP/1.1\r\nHost: esp\r\n\r\n";
 incoming.push_back(&socket);
}
int main(){
 // Eight requests exercise four concurrent slots and connections left pending
 // until a slot becomes free.
 WebManager web;
 Socket sockets[8];
 for(auto& socket:sockets)queue(socket);
 web.loop();
 for(unsigned i=0;i<4;i++)assert(sockets[i].dispatches==1);
 for(unsigned i=4;i<8;i++)assert(sockets[i].dispatches==0);
 for(int i=0;i<20;i++)web.loop();
 for(unsigned i=0;i<8;i++){
  auto& socket=sockets[i];
  assert(socket.output=="OK");
  assert(socket.dispatches==1);
  assert(!socket.open);
  assert(socket.stopDelay==0);
 }

 // An idle connection must not prevent a later client from being serviced.
 WebManager withIdle;
 Socket idle,ready;
 incoming.push_back(&idle);withIdle.loop();
 queue(ready);withIdle.loop();
 assert(ready.dispatches==1);
 now=3000;withIdle.loop();
 assert(ready.output=="OK");

 // A request that has started sending headers gets a longer inactivity window
 // than an idle speculative connection.
 WebManager partialHeader;
 Socket partial;
 partial.id=99;
 partial.input="GET /api/";
 incoming.push_back(&partial);
 partialHeader.loop();
 now=3000;
 partial.input+="status HTTP/1.1\r\nHost: esp\r\n\r\n";
 partialHeader.loop();
 assert(partial.dispatches==1);
 partialHeader.loop();
 assert(partial.output=="OK");

 // WiFi may report zero availableForWrite while still accepting short writes.
 wifiMode=true;now=0;
 WebManager wireless;
 Socket wifiRequest;queue(wifiRequest);
 wireless.loop();wireless.loop();
 assert(wifiRequest.output=="OK");
 assert(wifiRequest.dispatches==1);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / "test.cpp").write_text(stub + loop + test)
    subprocess.run(["c++", "-std=c++17", str(path / "test.cpp"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("OK: concurrent slots, queued clients, idle connection, and WiFi short writes")
