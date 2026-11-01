import re,sys,collections
d=sys.argv[1]; B=int(sys.argv[2]) if len(sys.argv)>2 else 2048
qs=[l.rstrip('\n') for l in open(d+'/queries.txt')]
bq=[]
for q in qs:
    if q.startswith('BOOL '):
        w=q[5:].split(' '); bq.append((w[0::2],w[1::2]))
terms=set(t for ts,_ in bq for t in ts)
tok=re.compile(rb'[a-z0-9]+')
blocks=collections.defaultdict(int); df=collections.Counter(); doc=0
tb={t.encode():t for t in terms}
for line in open(d+'/docs.txt','rb'):
    s=set(tok.findall(line.split(b'\t',1)[1].lower()))
    b=doc//B
    for x in s:
        if x in tb: blocks[tb[x]] |= 1<<b; df[tb[x]]+=1
    doc+=1
nb=(doc+B-1)//B
tot_all=0; tot_guard=0; hist=collections.Counter()
for ts,ops in bq:
    tot_all+=nb
    best=None
    for j in range(len(ts)):
        if j>0 and ops[j-1]!='AND': continue
        G=[ts[j]]+[ts[i] for i in range(j+1,len(ts)) if ops[i-1]=='OR']
        cost=sum(df[t] for t in G)
        if best is None or cost<best[0]: best=(cost,G)
    m=0
    for t in best[1]: m|=blocks[t]
    c=bin(m).count('1'); tot_guard+=c
    hist[min(c*10//nb,10)]+=1
print('blocks',nb,'evals all',tot_all,'guarded',tot_guard,'ratio %.3f'%(tot_guard/tot_all))
print('hist of fraction of blocks evaluated (deciles)',sorted(hist.items()))
