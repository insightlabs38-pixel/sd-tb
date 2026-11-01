#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sched.h>
#include <unistd.h>
#include <nmmintrin.h>
using namespace std;
struct Mapped {
 int fd=-1; const char* data=nullptr; size_t size=0;
 explicit Mapped(const string& path) {fd=open(path.c_str(),O_RDONLY);if(fd<0)throw runtime_error("open "+path);struct stat st{};if(fstat(fd,&st))throw runtime_error("stat");size=st.st_size;if(size){void*p=mmap(nullptr,size,PROT_READ,MAP_PRIVATE,fd,0);if(p==MAP_FAILED)throw runtime_error("mmap");data=(const char*)p;}}
 ~Mapped(){if(data)munmap((void*)data,size);if(fd>=0)close(fd);}
};
struct WordHash {
 size_t operator()(string_view w) const noexcept {
  uint64_t h=0x9e3779b97f4a7c15ULL ^ w.size(); size_t k=0;
  while(k+8<=w.size()){uint64_t x;memcpy(&x,w.data()+k,8);h=_mm_crc32_u64(h,x);k+=8;}
  if(k<w.size()){uint64_t x=0;memcpy(&x,w.data()+k,w.size()-k);h=_mm_crc32_u64(h,x);}
  return static_cast<size_t>(h ^ (h>>23));
 }
};
struct Answer {
 uint64_t count=0,sum=0;uint32_t first[10]{};unsigned used=0;
 void add(uint32_t d){++count;sum+=d;if(used<10)first[used++]=d;}
 void absorb(const Answer& v){count+=v.count;sum+=v.sum;uint32_t a[10];unsigned i=0,j=0,n=0;while(n<10&&(i<used||j<v.used)){if(j==v.used||(i<used&&first[i]<v.first[j]))a[n++]=first[i++];else a[n++]=v.first[j++];}used=n;copy_n(a,n,first);}
};
struct Query {bool phrase=false;vector<int> terms;string ops;int anchor=0;};
struct Scanner {
 deque<string> owned;
 unordered_map<string_view,int,WordHash> dict;
 vector<Query> queries;
 vector<vector<int>> heads;
 vector<int> phrase_slot;
 vector<vector<uint32_t>> index;
 size_t docs=0;
 int intern(string_view w){auto it=dict.find(w);if(it!=dict.end())return it->second;int id=owned.size();owned.emplace_back(w);dict.emplace(owned.back(),id);return id;}
 void parse(const string &path){Mapped file(path);const char*p=file.data,*end=p+file.size;while(p<end){const char*nl=(const char*)memchr(p,'\n',end-p);if(!nl)nl=end;string_view row(p,nl-p);Query q;bool phrase=row.starts_with("PHRASE \"");if(phrase){q.phrase=true;row.remove_prefix(8);if(row.empty()||row.back()!='"')throw runtime_error("phrase syntax");row.remove_suffix(1);}else if(row.starts_with("BOOL "))row.remove_prefix(5);else throw runtime_error("query syntax");size_t a=0;bool term=true;while(a<row.size()){auto b=row.find(' ',a);if(b==string_view::npos)b=row.size();auto w=row.substr(a,b-a);if(q.phrase||term)q.terms.push_back(intern(w));else q.ops.push_back(w[0]);term=!term;a=b+1;}
 if(q.terms.empty()||(!q.phrase&&q.ops.size()+1!=q.terms.size()))throw runtime_error("query shape");queries.push_back(move(q));p=nl==end?end:nl+1;}
 heads.resize(owned.size());phrase_slot.assign(queries.size(),-1);int pc=0;
 for(size_t i=0;i<queries.size();++i){auto &q=queries[i];if(!q.phrase)continue;int best=0;for(size_t j=1;j<q.terms.size();++j)if(owned[q.terms[j]].size()>owned[q.terms[best]].size())best=j;q.anchor=best;heads[q.terms[best]].push_back(i);phrase_slot[i]=pc++;}
 }
 struct Worker {vector<vector<uint32_t>> postings;vector<int32_t> last;vector<int32_t> lastphrase;vector<Answer> phrases;size_t documents=0;};
 static bool isword(unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}
 void scan(Mapped &file,int nworkers,vector<Worker>&workers){const char*begin=file.data,*end=begin+file.size;vector<thread> threads;vector<exception_ptr> errors(nworkers);size_t nt=owned.size();int npc=count_if(phrase_slot.begin(),phrase_slot.end(),[](int x){return x>=0;});
 auto run=[&](int wid){try{Worker&w=workers[wid];w.postings.resize(nt);w.last.assign(nt,-1);w.lastphrase.assign(npc,-1);w.phrases.resize(npc);const char *lo=begin+file.size*wid/nworkers,*hi=begin+file.size*(wid+1)/nworkers;if(wid){while(lo<end&&lo[-1]!='\n')++lo;}if(wid+1<nworkers){while(hi<end&&hi[-1]!='\n')++hi;}vector<int> tok;tok.reserve(1200);char buffer[64];for(const char*p=lo;p<hi;){const char*nl=(const char*)memchr(p,'\n',hi-p);if(!nl)nl=hi;const char*tab=(const char*)memchr(p,'\t',nl-p);if(!tab)throw runtime_error("missing tab");uint32_t id=0;auto [ep,ec]=from_chars(p,tab,id);if(ec!=errc()||ep!=tab)throw runtime_error("bad id");++w.documents;tok.clear();for(const char*s=tab+1;s<nl;){if(!isword((unsigned char)*s)){++s;continue;}int len=0;while(s<nl&&isword((unsigned char)*s)){unsigned char c=(unsigned char)*s++;if(len<32)buffer[len]=c>='A'&&c<='Z'?c+32:c;++len;}if(len>32){tok.push_back(-1);continue;}auto it=dict.find(string_view(buffer,len));int t=it==dict.end()?-1:it->second;tok.push_back(t);if(t>=0&&w.last[t]!=static_cast<int32_t>(id)){w.last[t]=id;w.postings[t].push_back(id);}}
 for(size_t j=0;j<tok.size();++j){int t=tok[j];if(t<0)continue;for(int qi:heads[t]){const auto&q=queries[qi];int pi=phrase_slot[qi];if(w.lastphrase[pi]==static_cast<int32_t>(id)||j<static_cast<size_t>(q.anchor)||j-q.anchor+q.terms.size()>tok.size())continue;bool ok=true;for(size_t k=0;k<q.terms.size();++k)if(tok[j-q.anchor+k]!=q.terms[k]){ok=false;break;}if(ok){w.lastphrase[pi]=id;w.phrases[pi].add(id);}}}p=nl==hi?hi:nl+1;
 }}catch(...){errors[wid]=current_exception();}};
 for(int j=1;j<nworkers;++j)threads.emplace_back(run,j);run(0);for(auto&t:threads)t.join();for(auto &e:errors)if(e)rethrow_exception(e);
 for(auto&w:workers)docs+=w.documents;
 }
 void merge(vector<Worker>&workers){index.resize(owned.size());for(size_t t=0;t<owned.size();++t){size_t len=0;for(auto&w:workers)len+=w.postings[t].size();auto&v=index[t];v.reserve(len);for(auto&w:workers){auto &p=w.postings[t];v.insert(v.end(),p.begin(),p.end());vector<uint32_t>().swap(p);}}
 }
 static void combine(const vector<uint32_t>&a,const vector<uint32_t>&b,vector<uint32_t>&out,char op){out.clear();if(op=='A'){size_t i=0,j=0;while(i<a.size()&&j<b.size()){if(a[i]==b[j]){out.push_back(a[i]);++i;++j;}else if(a[i]<b[j])++i;else ++j;}}else if(op=='N'){size_t i=0,j=0;while(i<a.size()){while(j<b.size()&&b[j]<a[i])++j;if(j==b.size()||a[i]!=b[j])out.push_back(a[i]);++i;}}else{size_t i=0,j=0;while(i<a.size()||j<b.size()){if(j==b.size()||(i<a.size()&&a[i]<b[j]))out.push_back(a[i++]);else if(i==a.size()||b[j]<a[i])out.push_back(b[j++]);else{out.push_back(a[i]);++i;++j;}}}}
 vector<Answer> execute(vector<Worker>&workers,int nworkers){
  vector<Answer> out(queries.size());
  for(size_t i=0;i<queries.size();++i)if(queries[i].phrase){
   int pi=phrase_slot[i];for(auto&w:workers)out[i].absorb(w.phrases[pi]);
  }
  atomic<size_t> next{0};
  vector<thread> threads;
  auto task=[&]{
   vector<uint32_t> a,b;
   for(size_t qi;(qi=next.fetch_add(1))<queries.size();){
    const auto&q=queries[qi];if(q.phrase)continue;
    int last_or=-1;
    for(size_t k=0;k<q.ops.size();++k)if(q.ops[k]=='O')last_or=k;
    int anchor=last_or<0?q.terms[0]:-1;
    for(size_t k=last_or+1;k<q.ops.size();++k)
     if(q.ops[k]=='A'&&(anchor<0||index[q.terms[k+1]].size()<index[anchor].size()))
      anchor=q.terms[k+1];
    if(anchor>=0&&index[anchor].size()<=1024){
     for(uint32_t doc:index[anchor]){
      bool state=binary_search(index[q.terms[0]].begin(),index[q.terms[0]].end(),doc);
      for(size_t k=0;k<q.ops.size();++k){
       char op=q.ops[k];
       if(!state&&op!='O')continue;
       const auto &rhs=index[q.terms[k+1]];
       bool match=binary_search(rhs.begin(),rhs.end(),doc);
       if(op=='A')state=state&&match;
       else if(op=='O')state=state||match;
       else state=state&&!match;
      }
      if(state)out[qi].add(doc);
     }
    }else{
     a=index[q.terms[0]];
     for(size_t k=0;k<q.ops.size();++k){
      combine(a,index[q.terms[k+1]],b,q.ops[k]);a.swap(b);
     }
     for(uint32_t doc:a)out[qi].add(doc);
    }
   }
  };
  for(int i=1;i<nworkers;++i)threads.emplace_back(task);
  task();for(auto&t:threads)t.join();return out;
 }
 void run(const string&dir,const string&outdir){auto stamp=[](){return chrono::steady_clock::now();};auto t0=stamp();parse(dir+"/queries.txt");auto t1=stamp();Mapped file(dir+"/docs.txt");cpu_set_t set;int n=sched_getaffinity(0,sizeof(set),&set)==0?CPU_COUNT(&set):4;n=max(1,min(12,n));if(file.size<5000000)n=1;vector<Worker> w(n);scan(file,n,w);auto t2=stamp();merge(w);auto t3=stamp();auto result=execute(w,n);auto t4=stamp();filesystem::create_directories(outdir);ofstream f(outdir+"/results.txt",ios::binary);for(auto&a:result){f<<a.count<<' '<<a.sum;for(unsigned j=0;j<a.used;++j)f<<' '<<a.first[j];f<<'\n';}if(!f)throw runtime_error("write failure");auto t5=stamp();if(getenv("B_PROFILE")){auto ms=[](auto a,auto b){return chrono::duration<double,milli>(b-a).count();};cerr<<"PHASE_MS parse="<<ms(t0,t1)<<" scan="<<ms(t1,t2)<<" merge="<<ms(t2,t3)<<" eval="<<ms(t3,t4)<<" output="<<ms(t4,t5)<<" docs="<<docs<<" workers="<<n<<'\n';}}
};
int main(int argc,char**argv){string dir="/data",out="/out";for(int i=1;i+1<argc;i+=2){if(!strcmp(argv[i],"--data"))dir=argv[i+1];else if(!strcmp(argv[i],"--out"))out=argv[i+1];else return 2;}try{Scanner().run(dir,out);}catch(const exception&e){cerr<<e.what()<<'\n';return 1;}return 0;}
