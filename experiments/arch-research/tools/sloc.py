import subprocess,sys,re
# Approximates scc code lines after clang-format --style=LLVM.
for f in sys.argv[1:]:
    src=subprocess.run(['clang-format','--style=LLVM',f],capture_output=True,text=True).stdout
    n=0; inblock=False
    for line in src.split('\n'):
        s=line.strip()
        if inblock:
            if '*/' in s: inblock=False; s=s.split('*/',1)[1].strip()
            else: continue
        if not s or s.startswith('//'): continue
        if s.startswith('/*'):
            if '*/' not in s: inblock=True
            continue
        n+=1
    print(n,f)
