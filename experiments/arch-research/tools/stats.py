import re,sys,collections
d=sys.argv[1]
qs=[l.rstrip('\n') for l in open(d+'/queries.txt')]
bt=collections.Counter(); pt=collections.Counter(); bq=[]; pq=[]
for q in qs:
    if q.startswith('BOOL '):
        w=q[5:].split(' '); terms=w[0::2]; ops=w[1::2]; bq.append((terms,ops)); bt.update(set(terms))
    else:
        terms=q[8:-1].split(' '); pq.append(terms); pt.update(set(terms))
allt=set(bt)|set(pt)
print('distinct bool terms',len(bt),'phrase terms',len(pt),'union',len(allt))
tok=re.compile(rb'[a-z0-9]+')
df=collections.Counter(); occ=collections.Counter(); ntok=0; ndoc=0; pairs_bool=0; pairs_all=0; qocc=0
lens=collections.Counter()
for line in open(d+'/docs.txt','rb'):
    t=line.split(b'\t',1)[1].lower(); ts=tok.findall(t); ndoc+=1; ntok+=len(ts)
    s=set(ts); pairs_all+=len(s)
    for x in s: df[x]+=1
    for x in ts: lens[len(x)]+=1
N=ndoc
print('docs',N,'tokens',ntok,'pairs',pairs_all)
bb=set(x.encode() for x in bt); pp=set(x.encode() for x in pt); aa=bb|pp
print('pairs bool terms',sum(df[x] for x in bb),' token occ that are query terms: need occ -> skip')
print('token length hist',sorted(lens.items())[:20])
for name,S_ in (('bool',bb),('phrase',pp)):
    bands=collections.Counter()
    for x in S_:
        f=df[x]; b='VC' if f>N/10 else 'C' if f>N/100 else 'M' if f>N/1000 else 'U' if f>N/10000 else 'R' if f>0 else 'absent'
        bands[b]+=1
    print(name,'distinct by band',dict(bands))
for th in (64,256,1024,4096):
    hot=[x for x in bb if df[x]>N/th]; print('bool terms with df>N/%d:'%th,len(hot),'pairs',sum(df[x] for x in hot))
