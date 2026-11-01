import re,sys,collections,math,pickle,os
d=sys.argv[1]; N=None
qs=[l.rstrip('\n') for l in open(d+'/queries.txt')]
bq=[(q[5:].split(' ')[0::2], q[5:].split(' ')[1::2]) for q in qs if q.startswith('BOOL ')]
terms=set(t for ts,_ in bq for t in ts)
cache=d+'/df.pkl'
if os.path.exists(cache): df,N=pickle.load(open(cache,'rb'))
else:
    tok=re.compile(rb'[a-z0-9]+'); df=collections.Counter(); N=0; tb=set(t.encode() for t in terms)
    for line in open(d+'/docs.txt','rb'):
        N+=1
        for x in set(tok.findall(line.split(b'\t',1)[1].lower())):
            if x in tb: df[x.decode()]+=1
    pickle.dump((df,N),open(cache,'wb'))
W=N/64
for dense_div in (4,8,16,32,64):
    th=N/dense_div
    dense=[t for t in terms if df[t]>th]
    bitmapMB=len(dense)*N/8/1e6
    pairs_sparse=sum(df[t] for t in terms if df[t]<=th)
    # eval cost model: words for bitmap ops, elements for list ops
    work=0
    for ts,ops in bq:
        # running result: ('b', None) bitmap or ('l', size)
        f=df[ts[0]]; r=('b',f) if f>th else ('l',f)
        if r[0]=='b': work+=W/4  # copy
        for op,t in zip(ops,ts[1:]):
            f=df[t]; tb_=f>th
            if op=='OR':
                if r[0]=='b' or tb_: work+=W/4 + (0 if tb_ else f) + (r[1] if r[0]=='l' else 0); r=('b',None)
                else: work+=r[1]+f; r=('l',r[1]+f)
            else:
                if r[0]=='l':
                    if tb_: work+=r[1]
                    else: work+=min(r[1]+f, r[1]*max(1,math.log2(max(2,f/max(1,r[1])))) )
                    r=('l', r[1])  # upper bound size
                else:
                    work+= W/4 if tb_ else f
                    if not tb_ and op=='AND': r=('l',f)
        work+= W/4 if r[0]=='b' else 0
    print('dense df>N/%d: %d bitmaps %.1fMB, sparse pairs %.1fM (x2B=%.1fMB), eval work %.0fM units'%(dense_div,len(dense),bitmapMB,pairs_sparse/1e6,pairs_sparse*2/1e6,work/1e6))
