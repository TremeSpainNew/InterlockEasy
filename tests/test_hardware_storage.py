from pathlib import Path
import tempfile,subprocess
ROOT=Path(__file__).resolve().parents[1]
scope={'__file__':str(ROOT/'tests/test_hardware.py')}
exec((ROOT/'tests/test_hardware.py').read_text().split('source=')[0],scope)
stubs=scope['stubs'].copy()
stubs['LittleFS.h']=r'''#pragma once
#include <map>
#include <cstring>
inline std::map<std::string,String> files;
inline bool shortWrite=false,renameFailure=false;
class File {
 String path;size_t pos=0;bool valid=false;
public:
 File(){} File(const char* p,bool v):path(p),valid(v){}
 operator bool()const{return valid;}
 size_t size(){return valid?files[path].size():0;}
 size_t print(const String& s){files[path]=shortWrite?String(s.substr(0,s.size()/2)):s;return files[path].size();}
 void setTimeout(unsigned long){}
 void flush(){} void close(){valid=false;}
 String readString(){return files[path];}
 int read(){return pos<size()?uint8_t(files[path][pos++]):-1;}
 size_t readBytes(char* buffer,size_t n){size_t count=0;while(count<n && pos<size())buffer[count++]=files[path][pos++];return count;}
};
struct FSStub {
 bool begin(bool){return true;}
 bool exists(const char* p){return files.count(p);}
 File open(const char* p,const char* mode){if(mode[0]=='w')files[p]="";return File(p,exists(p));}
 bool remove(const char* p){return files.erase(p);}
 bool rename(const char* a,const char* b){if(renameFailure)return false;files[b]=files[a];files.erase(a);return true;}
};
inline FSStub LittleFS;
'''
source=r'''
#include "HardwareConfig.h"
#include <LittleFS.h>
#include <cassert>
int main(){
 Hardware.defaults();Hardware.filesystemReady=true;
 DynamicJsonDocument doc(24576);Hardware.toJson(doc);
 HardwareConfig parsed;String error;
 assert(parsed.parse(doc.as<JsonVariantConst>(),error));assert(parsed.outputs.size()==8);
 files["/config.json"]="old";
 doc["outputCount"]=9;
 assert(!Hardware.saveJson(doc.as<JsonVariantConst>(),error));assert(files["/config.json"]=="old");
 doc["outputCount"]=8;doc["outputs"][0]["activeLow"]=true;
 shortWrite=true;assert(!Hardware.saveJson(doc.as<JsonVariantConst>(),error));assert(files["/config.json"]=="old");
 shortWrite=false;renameFailure=true;assert(!Hardware.saveJson(doc.as<JsonVariantConst>(),error));assert(files["/config.json"]=="old");
 renameFailure=false;assert(Hardware.saveJson(doc.as<JsonVariantConst>(),error));
 assert(!Hardware.outputs[0].activeLow);assert(!files.count("/config.json.tmp"));
 HardwareConfig reboot;assert(reboot.begin());assert(reboot.outputs[0].activeLow);
 Hardware.filesystemReady=false;assert(!Hardware.saveJson(doc.as<JsonVariantConst>(),error));
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)
 for name,value in stubs.items():
  (p/name).parent.mkdir(exist_ok=True,parents=True);(p/name).write_text(value)
 (p/'test.cpp').write_text(source)
 subprocess.run(['c++','-std=c++17',f'-I{p}',f'-I{ROOT/"src"}',f'-I{ROOT/".pio/libdeps/esp32-s3-devkitc-1/ArduinoJson/src"}',str(ROOT/'src/HardwareConfig.cpp'),str(ROOT/'src/HardwareConfigStorage.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: hardware JSON roundtrip, invalid map, failed writes/rename preserve previous file, live map unchanged, reboot applies file')
