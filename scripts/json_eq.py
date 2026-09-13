import json, sys
got = json.load(open(sys.argv[1]))
want = json.load(open(sys.argv[2]))
sys.exit(0 if got == want else 1)
