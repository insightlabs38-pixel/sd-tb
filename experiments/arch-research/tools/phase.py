import sys
# phase.py src dst: wraps main's phases with wall-clock laps printed to stderr.
s=open(sys.argv[1]).read()
def rep(a,b):
    global s
    assert a in s, a
    s=s.replace(a,b,1)
rep('using namespace std;','using namespace std;\n#include <chrono>\nstatic double nowms(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}')
rep('    Engine engine;','    double t0=nowms(); Engine engine;')
rep('    engine.parse(queries, qsize, workers.size());','    engine.parse(queries, qsize, workers.size()); double t1=nowms();')
rep('    engine.plan(docs, dsize);','    engine.plan(docs, dsize); double t2=nowms();')
rep('    engine.scan(docs, dsize, workers);','    engine.scan(docs, dsize, workers); double t3=nowms();')
rep('    engine.write(out, workers);','    engine.write(out, workers); double t4=nowms(); fprintf(stderr,"parse %.1f plan %.1f scan %.1f write %.1f\\n",t1-t0,t2-t1,t3-t2,t4-t3);')
open(sys.argv[2],'w').write(s)
