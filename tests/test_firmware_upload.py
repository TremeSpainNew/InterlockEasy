"""Compile the real OTA receiver and connection cleanup with a fake updater."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/WebManager.cpp').read_text()
dispatch=source[source.index('void WebManager::dispatch()'):source.index('void WebManager::config(')]
assert 'path=="/api/filesystem"' in dispatch
assert 'Update.begin(contentLength,filesystemRequest?U_SPIFFS:U_FLASH)' in dispatch
assert 'if(!Update.begin(contentLength,filesystemRequest?U_SPIFFS:U_FLASH))' in dispatch
assert 'firmwareRequest=false;\n                firmwareUploadActive=false;' in dispatch
receiver=source[source.index('void WebManager::receiveFirmware()'):]
assert 'if(!ok){firmwareUploadActive=false;' in receiver
cleanup=source[source.index('void WebManager::close()'):source.index('void WebManager::headers(')]
stub=r'''
#include <string>
#include <cstdint>
#include <cassert>
#include <algorithm>
using String=std::string;
unsigned long now=0;unsigned long millis(){return now;}
struct Client {
 std::string data;size_t pos=0;bool stopped=false;
 int available(){return data.size()-pos;}
 int read(){return pos<data.size()?uint8_t(data[pos++]):-1;}
 void stop(){stopped=true;}
};
struct File {operator bool(){return false;}void close(){}};
struct Updater {
 bool running=true,failWrite=false,failEnd=false,aborted=false,ended=false;
 std::string received;
 bool isRunning(){return running;}
 void abort(){aborted=true;running=false;}
 const char* errorString(){return "test failure";}
 size_t write(uint8_t* b,size_t n){if(failWrite)return 0;received.append((char*)b,n);return n;}
 bool end(){ended=true;running=false;return !failEnd;}
} Update;
struct WebManager {
 Client* client=nullptr;File file;String request,pending,body;
 size_t offset=0,contentLength=0,firmwareReceived=0;
 bool firmwareRequest=true,filesystemRequest=false,firmwareUploadActive=false,readingBody=true,responding=false,testRequest=false,hardwareRequest=false,restartPending=false;
 unsigned long started=0,restartAt=0;int status=0;String responseBody;
 void respond(int code,const char*,const char*,const String& value){status=code;responseBody=value;responding=true;}
 void receiveFirmware();void close();
};
'''
test=r'''
int main(){
 Client client;client.data=std::string(2500,'x');WebManager web;web.client=&client;web.contentLength=2500;web.firmwareUploadActive=true;
 now=50000;web.receiveFirmware();assert(web.firmwareReceived==1024 && !Update.ended && web.started==now);
 web.receiveFirmware();assert(web.firmwareReceived==2048 && !web.restartPending);
 web.receiveFirmware();assert(Update.received==client.data && Update.ended);
 assert(web.status==200 && web.restartPending && web.restartAt==now+2000);
 assert(web.responseBody.find("\"firmware\"")!=std::string::npos);
 web.close();assert(!Update.aborted && client.stopped);
 Update=Updater{};client=Client{};client.data="partial";web=WebManager{};web.client=&client;web.contentLength=100;web.firmwareUploadActive=true;
 web.receiveFirmware();web.close();assert(Update.aborted && !Update.ended && !web.restartPending && !web.firmwareUploadActive);
 Update=Updater{};Update.failWrite=true;client=Client{};client.data="bad";web=WebManager{};web.client=&client;web.contentLength=3;web.firmwareUploadActive=true;
 web.receiveFirmware();assert(web.status==400 && Update.aborted && !web.restartPending && !web.firmwareUploadActive);
 Update=Updater{};Update.failEnd=true;client=Client{};client.data="bad";web=WebManager{};web.client=&client;web.contentLength=3;web.firmwareUploadActive=true;
 web.receiveFirmware();assert(web.status==400 && !web.restartPending && !web.firmwareUploadActive);
 Update=Updater{};client=Client{};client.data="fs";web=WebManager{};web.client=&client;web.contentLength=2;web.filesystemRequest=true;web.firmwareUploadActive=true;
 web.receiveFirmware();assert(web.status==200 && web.restartPending);
 assert(web.responseBody.find("\"filesystem\"")!=std::string::npos);
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.cpp').write_text(stub+cleanup+receiver+test)
 subprocess.run(['c++','-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: streamed OTA, inactivity timer, complete image, abort, write/validation failures')
