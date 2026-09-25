from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include "InputStateQueue.h"
#include <cassert>
int main(){
 InputStateQueue q;
 assert(q.next(0)==-1);
 q.set(0,true);q.set(0,false);q.set(1,true);
 assert(q.next(0)==0 && !q.value(0));
 assert(q.pending(0)); // failed publish: don't clear
 assert(q.next(1)==1);q.clear(1);
 assert(q.next(999)==-1);assert(q.next(1000)==0);q.clear(0);
 assert(q.next(1001)==-1);
 q.set(0,true);assert(q.next(1100)==0);
 q.set(0,false);assert(q.next(1101)==0); // latest state replaces failed value
 q.clear(0);q.set(7,true);assert(q.next(0xfffffff0U)==7);
 assert(q.next(0x10U)==-1);assert(q.next(0x400U)==7);
 q.set(31,true);assert(q.pending(31));
 q.set(32,true);assert(!q.pending(32));
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(source)
 subprocess.run(['c++','-std=c++17',f'-I{root / "src"}',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('OK: latest state, fair delivery, failure retry, invalid channel and rollover')
