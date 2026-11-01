import sys
# k.py T_seconds M_bytes S_lines [D]: official K with box reference constants.
T,M,S=float(sys.argv[1]),float(sys.argv[2]),float(sys.argv[3]); D=float(sys.argv[4]) if len(sys.argv)>4 else 1
print('%.3f'%((56.352376/T)**0.5*min(2199961600/M,50)**0.2*min(91/S,1)**0.15*(1/D)**0.15))
