"""Compare the patched selector with the pinned algorithm and count scratch allocations.

The reference method is from Dynarmic a41c380 reg_alloc.cpp (0BSD,
Copyright2016 MerryMage). No SDK Git checkout is required. Actual candidate
method text is compiled unchanged against a deterministic register-state fixture.
"""
from pathlib import Path
import argparse, os, subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--sdk',type=Path,required=True)
parser.add_argument('--cxx',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--compiler-id',default='GNU')
args=parser.parse_args()
out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
new=(args.sdk/'src/dynarmic/backend/x64/reg_alloc.cpp').read_text()
old='HostLoc RegAlloc::SelectARegister(const std::vector<HostLoc>& desired_locations) const {\n    std::vector<HostLoc> candidates = desired_locations;\n\n    // Find all locations that have not been allocated..\n    const auto allocated_locs = std::partition(candidates.begin(), candidates.end(), [this](auto loc) {\n        return !this->LocInfo(loc).IsLocked();\n    });\n    candidates.erase(allocated_locs, candidates.end());\n    ASSERT_MSG(!candidates.empty(), "All candidate registers have already been allocated");\n\n    // Selects the best location out of the available locations.\n    // TODO: Actually do LRU or something. Currently we just try to pick something without a value if possible.\n\n    std::partition(candidates.begin(), candidates.end(), [this](auto loc) {\n        return this->LocInfo(loc).IsEmpty();\n    });\n\n    return candidates.front();\n}\n\nstd::optional<HostLoc> RegAlloc::ValueLocation('
def extract(text,method):
    body=text[text.index('HostLoc RegAlloc::SelectARegister('):text.index('\nstd::optional<HostLoc> RegAlloc::ValueLocation(')]
    return body.replace('RegAlloc::SelectARegister','Allocator::'+method)
source=r'''
// Differential check of actual pinned and patched SDK selection bodies.
#include <algorithm>
#include <array>
#include <span>
#include <vector>
#include <random>
#include <stdexcept>
#include <iostream>
#include <cstdlib>
#include <new>
static size_t allocations=0;
void* operator new(size_t n) {++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
using HostLoc=unsigned;
#define ASSERT_MSG(x,m) do {if(!(x))throw std::runtime_error(m);} while(false)
struct Info {bool locked=false,empty=false;bool IsLocked()const{return locked;}bool IsEmpty()const{return empty;}};
struct Allocator {
 std::array<Info,128> info{};
 const Info& LocInfo(HostLoc x)const{return info.at(x);}
 HostLoc old(const std::vector<HostLoc>& desired_locations)const;
 HostLoc current(std::span<const HostLoc> desired_locations)const;
};
'''+extract(old,'old')+'\n'+extract(new,'current')+r'''
int main() {
 Allocator a;std::mt19937 random(7341);size_t cases=0;
 auto check=[&](const std::vector<HostLoc>& list) {
  int before=-1,after=-1;
  try {before=int(a.old(list));}catch(const std::runtime_error&){}
  try {after=int(a.current(list));}catch(const std::runtime_error&){}
  if(before!=after)throw std::runtime_error("register selection changed");
  ++cases;
 };
 check({});
 for(unsigned n=1;n<=8;++n) {
  unsigned limit=1;for(unsigned i=0;i<n;++i)limit*=3;
  std::vector<HostLoc> list(n);for(unsigned i=0;i<n;++i)list[i]=i;
  for(unsigned pattern=0;pattern<limit;++pattern) {
   unsigned value=pattern;
   for(unsigned i=0;i<n;++i) {unsigned state=value%3;value/=3;a.info[i]={state==0,state==1};}
   check(list);std::reverse(list.begin(),list.end());check(list);
  }
 }
 for(unsigned n: {1,2,4,8,16,24,32,33,48,64}) {
  std::vector<HostLoc> list(n);for(unsigned i=0;i<n;++i)list[i]=i;
  for(unsigned pattern=0;pattern<2000;++pattern) {
   std::shuffle(list.begin(),list.end(),random);
   for(unsigned i=0;i<n;++i) {auto state=random()%3;a.info[i]={state==0,state==1};}
   check(list);
  }
  for(auto& info:a.info)info={false,true};
  auto count=allocations;a.old(list);const auto old_allocations=allocations-count;
  count=allocations;a.current(list);const auto new_allocations=allocations-count;
  if(old_allocations!=1 || new_allocations!=(n<=32?0u:1u))throw std::runtime_error("unexpected scratch allocation");
 }
 std::cout<<cases<<" selection/failure cases identical; ordinary candidate scratch uses zero heap allocations; oversized fallback retained.\n";
}
'''
path=out/'register-selection-test.cpp';path.write_text(source)
env=os.environ.copy();env['PATH']=str(args.cxx.parent)+os.pathsep+env.get('PATH','')
exe=out/('register-selection-test.exe' if os.name=='nt' else 'register-selection-test')
if args.compiler_id=='MSVC' or 'clang-cl' in args.cxx.name:
    command=[str(args.cxx),'/std:c++20','/O2','/EHsc',str(path),'/Fe:'+str(exe)]
else:
    command=[str(args.cxx),'-std=c++20','-O2','-Wall','-Wextra',str(path),'-o',str(exe)]
flags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0
subprocess.run(command,cwd=out,env=env,creationflags=flags,check=True)
subprocess.run([str(exe)],cwd=out,env=env,creationflags=flags,check=True)
